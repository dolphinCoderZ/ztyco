#include "scheduler.h"
#include "fiber.h"
#include "hook.h"
#include "log.h"
#include "macro.h"
#include "mutex.h"
#include "thread.h"
#include "util.h"
#include <cstddef>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

namespace zz {

static zz::Logger::ptr g_logger = ZZ_LOG_NAME("system");

// 当前线程的调度器，同一个调度器下的所有线程共享一个调度器实例
static thread_local Scheduler *t_scheduler = nullptr;
// 当前线程的调度协程
static thread_local Fiber *t_scheduler_fiber = nullptr;

// 调度器初始化主要是为caller线程参与调度时，做一些必要的初始化操作
/*
1.创建caller线程的主协程以激活协程功能
2.初始化caller线程的调度协程并保存起来
*/
Scheduler::Scheduler(size_t threads, bool use_caller, const std::string &name)
    : m_name(name) {
  ZZ_ASSERT(threads > 0);
  /**
   * caller线程的主协程不会被线程的调度协程调度
   * 线程的调度协程停止时，需要返回caller线程的主协程
   * 在user_caller情况下，把caller线程的主协程暂时保存起来，等调度协程结束时，再返回主协程
   */
  if (use_caller) {
    --threads; // caller线程参与调度消耗一个线程

    zz::Fiber::GetThis(); // 开启协程功能

    ZZ_ASSERT(GetThis() == nullptr);
    t_scheduler = this;

    // 初始化caller线程中的调度协程，但并没有开始调度，且不参与调度
    m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false));
    t_scheduler_fiber = m_rootFiber.get(); // 记录caller线程的调度协程

    // caller的线程信息
    zz::Thread::SetName(m_name);
    m_rootThreadid = zz::GetThreadId();
    m_threadIds.push_back(m_rootThreadid);
  } else { // 标识不使用caller线程
    m_rootThreadid = -1;
  }
  m_threadCount = threads;
}

Scheduler::~Scheduler() {
  ZZ_ASSERT(m_stopping);
  if (GetThis() == this) {
    t_scheduler = nullptr;
  }
}

Scheduler *Scheduler::GetThis() { return t_scheduler; }

void Scheduler::setThis() { t_scheduler = this; }

Fiber *Scheduler::GetMainFiber() { return t_scheduler_fiber; }

void Scheduler::start() {
  MutexType::Lock lock(m_mutex);
  if (!m_stopping) { // 已经开启
    return;
  }

  m_stopping = false;
  ZZ_ASSERT(m_threads.empty());
  // 创建调度线程，run会创建出调度线程的调度协程，即刻开始调度
  for (size_t i = 0; i < m_threadCount; ++i) {
    m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this),
                                  m_name + "_" + std::to_string(i)));
    m_threadIds.push_back(m_threads[i]->getId());
  }
}

void Scheduler::stop() {
  m_autoStop = true;
  if (m_rootFiber && m_threadCount == 0 &&
      (m_rootFiber->getState() == Fiber::TERM ||
       m_rootFiber->getState() == Fiber::INIT)) {
    ZZ_LOG_INFO(g_logger) << this << " stopped";
    m_stopping = true;
    if (stopping()) {
      return;
    }
  }

  if (m_rootThreadid != -1) {
    ZZ_ASSERT(GetThis() == this);
  } else {
    ZZ_ASSERT(GetThis() != this);
  }

  m_stopping = true;

  // 通知其他调度线程的调度协程退出
  for (size_t i = 0; i < m_threadCount; ++i) {
    tickle();
  }

  // 通知caller线程的调度协程退出
  if (m_rootFiber) {
    tickle();
  }
  if (m_rootFiber) {
    if (!stopping()) {
      // caller线程调度协程的调度时机与其他线程调度协程的调度时机不同
      m_rootFiber->resume(); // caller线程的调度协程启动调度
    }
  }

  // 等待线程池中所有线程结束(也就是线程中调度协程调度结束)
  std::vector<Thread::ptr> thrs;
  {
    MutexType::Lock lock(m_mutex);
    thrs.swap(m_threads);
  }
  for (auto &it : thrs) {
    it->join();
  }
}

bool Scheduler::stopping() {
  MutexType::Lock lock(m_mutex);
  return m_autoStop && m_stopping && m_tasks.empty() &&
         m_activeThreadCount == 0;
}

void Scheduler::tickle() { ZZ_LOG_INFO(g_logger) << "tickle"; }

void Scheduler::run() {
  ZZ_LOG_DEBUG(g_logger) << m_name << " run";
  zz::set_hook_enable(true);

  setThis();
  // 若当前线程不是caller线程，那么当前线程的调度协程由主协程充当
  if (zz::GetThreadId() != m_rootThreadid) {
    // GetThis会创建该线程的主协程，也是该线程的调度协程
    t_scheduler_fiber = Fiber::GetThis().get();
  }
  // 若当前线程是caller线程，那么其主协程和调度协程在调度器初始化时就创建完毕了

  // 没有任务时，执行idle协程，不让线程终止
  Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
  Fiber::ptr func_fiber;
  ScheduleTask task;

  while (true) {
    task.reset(); // 下次取任务之前，重置任务数据

    bool tickle_me = false; // 是否tickle其他线程进行任务调度
    bool is_active = false;
    {
      MutexType::Lock lock(m_mutex);
      auto it = m_tasks.begin();
      while (it != m_tasks.end()) {
        // 指定了线程，但当前不在指定线程，标记需要通知其他线程进行调度
        if (it->threadId != -1 && it->threadId != zz::GetThreadId()) {
          ++it;
          tickle_me = true;
          continue;
        }

        // 找到一个指定当前线程的任务
        task = *it;
        m_tasks.erase(it++);
        ++m_activeThreadCount;
        is_active = true;
        break;
      }
      tickle_me |= it != m_tasks.end(); // 若任务队列还有剩余，标记继续通知
    } // 获取任务立即解锁

    if (tickle_me) { // 通知其他线程
      tickle();
    }

    // 执行所取到的任务
    if (task.fiber && (task.fiber->getState() != Fiber::TERM) &&
        task.fiber->getState() != Fiber::EXCEPT) {
      task.fiber->swapIn(); // 换入执行

      --m_activeThreadCount; // 执行完回来，活跃线程-1
      // 判断是否还需要执行
      if (task.fiber->getState() == Fiber::READY) {
        schedule(task.fiber);
      } else if (task.fiber->getState() != Fiber::TERM &&
                 task.fiber->getState() != Fiber::EXCEPT) {
        task.fiber->m_state = Fiber::HOLD;
      }
      task.reset();
    } else if (task.func) { // 若是函数，则包装成协程进行调度
      if (func_fiber) {
        func_fiber->resetFunc(task.func);
      } else {
        // 将函数包装成协程(保存运行栈帧)
        func_fiber.reset(new Fiber(task.func));
      }
      task.reset();

      func_fiber->swapIn();
      --m_activeThreadCount;

      // 执行完之后，根据状态判断是否需要继续调度
      if (func_fiber->getState() == Fiber::READY) {
        schedule(func_fiber);
        func_fiber.reset();
      } else if (func_fiber->getState() == Fiber::EXCEPT ||
                 func_fiber->getState() == Fiber::TERM) {
        func_fiber->resetFunc(nullptr);
      } else {
        func_fiber->m_state = Fiber::HOLD;
        func_fiber.reset();
      }
    } else {                   // 若取到的任务无效
      if (is_active) {         // 恢复取任务前的状态
        --m_activeThreadCount; // 取任务时+1，取到无效任务应该-1
        continue;
      }
      if (idle_fiber->getState() == Fiber::TERM) {
        ZZ_LOG_INFO(g_logger) << "idle fiber term";
        break;
      }

      // 没有任务可调度时，执行idle协程
      ++m_idleThreadCount;
      idle_fiber->swapIn();
      --m_idleThreadCount;

      if (idle_fiber->getState() != Fiber::TERM &&
          idle_fiber->getState() != Fiber::EXCEPT) {
        idle_fiber->m_state = Fiber::HOLD;
      }
    }
  }
}

void Scheduler::idle() {
  ZZ_LOG_INFO(g_logger) << "idle";
  while (!stopping()) {
    zz::Fiber::YieldToHold();
  }
}

// 切换到另一个线程
void Scheduler::switchTo(int thread) {
  ZZ_ASSERT(Scheduler::GetThis() != nullptr);

  if (Scheduler::GetThis() == this) {
    if (thread == -1 || thread == zz::GetThreadId()) {
      return;
    }
  }

  schedule(Fiber::GetThis(), thread);
  Fiber::YieldToHold();
}

SchedulerSwicher::SchedulerSwicher(Scheduler *target) {
  m_caller = Scheduler::GetThis();
  if (target) {
    target->switchTo();
  }
}

SchedulerSwicher::~SchedulerSwicher() {
  if (m_caller) {
    m_caller->switchTo();
  }
}

} // namespace zz