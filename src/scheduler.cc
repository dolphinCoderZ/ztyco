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

// ��ǰ�̵߳ĵ�������ͬһ���������µ������̹߳���һ��������ʵ��
static thread_local Scheduler *t_scheduler = nullptr;
// ��ǰ�̵߳ĵ���Э��
static thread_local Fiber *t_scheduler_fiber = nullptr;

// ��������ʼ����Ҫ��Ϊcaller�̲߳������ʱ����һЩ��Ҫ�ĳ�ʼ������
/*
1.����caller�̵߳���Э���Լ���Э�̹���
2.��ʼ��caller�̵߳ĵ���Э�̲���������
*/
Scheduler::Scheduler(size_t threads, bool use_caller, const std::string &name)
    : m_name(name) {
  ZZ_ASSERT(threads > 0);
  /**
   * caller�̵߳���Э�̲��ᱻ�̵߳ĵ���Э�̵���
   * �̵߳ĵ���Э��ֹͣʱ����Ҫ����caller�̵߳���Э��
   * ��user_caller����£���caller�̵߳���Э����ʱ�����������ȵ���Э�̽���ʱ���ٷ�����Э��
   */
  if (use_caller) {
    --threads; // caller�̲߳����������һ���߳�

    zz::Fiber::GetThis(); // ����Э�̹���

    ZZ_ASSERT(GetThis() == nullptr);
    t_scheduler = this;

    // ��ʼ��caller�߳��еĵ���Э�̣�����û�п�ʼ���ȣ��Ҳ��������
    m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false));
    t_scheduler_fiber = m_rootFiber.get(); // ��¼caller�̵߳ĵ���Э��

    // caller���߳���Ϣ
    zz::Thread::SetName(m_name);
    m_rootThreadid = zz::GetThreadId();
    m_threadIds.push_back(m_rootThreadid);
  } else { // ��ʶ��ʹ��caller�߳�
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
  if (!m_stopping) { // �Ѿ�����
    return;
  }

  m_stopping = false;
  ZZ_ASSERT(m_threads.empty());
  // ���������̣߳�run�ᴴ���������̵߳ĵ���Э�̣����̿�ʼ����
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

  // ֪ͨ���������̵߳ĵ���Э���˳�
  for (size_t i = 0; i < m_threadCount; ++i) {
    tickle();
  }

  // ֪ͨcaller�̵߳ĵ���Э���˳�
  if (m_rootFiber) {
    tickle();
  }
  if (m_rootFiber) {
    if (!stopping()) {
      // caller�̵߳���Э�̵ĵ���ʱ���������̵߳���Э�̵ĵ���ʱ����ͬ
      m_rootFiber->resume(); // caller�̵߳ĵ���Э����������
    }
  }

  // �ȴ��̳߳��������߳̽���(Ҳ�����߳��е���Э�̵��Ƚ���)
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
  // ����ǰ�̲߳���caller�̣߳���ô��ǰ�̵߳ĵ���Э������Э�̳䵱
  if (zz::GetThreadId() != m_rootThreadid) {
    // GetThis�ᴴ�����̵߳���Э�̣�Ҳ�Ǹ��̵߳ĵ���Э��
    t_scheduler_fiber = Fiber::GetThis().get();
  }
  // ����ǰ�߳���caller�̣߳���ô����Э�̺͵���Э���ڵ�������ʼ��ʱ�ʹ��������

  // û������ʱ��ִ��idleЭ�̣������߳���ֹ
  Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
  Fiber::ptr func_fiber;
  ScheduleTask task;

  while (true) {
    task.reset(); // �´�ȡ����֮ǰ��������������

    bool tickle_me = false; // �Ƿ�tickle�����߳̽����������
    bool is_active = false;
    {
      MutexType::Lock lock(m_mutex);
      auto it = m_tasks.begin();
      while (it != m_tasks.end()) {
        // ָ�����̣߳�����ǰ����ָ���̣߳������Ҫ֪ͨ�����߳̽��е���
        if (it->threadId != -1 && it->threadId != zz::GetThreadId()) {
          ++it;
          tickle_me = true;
          continue;
        }

        // �ҵ�һ��ָ����ǰ�̵߳�����
        task = *it;
        m_tasks.erase(it++);
        ++m_activeThreadCount;
        is_active = true;
        break;
      }
      tickle_me |= it != m_tasks.end(); // ��������л���ʣ�࣬��Ǽ���֪ͨ
    } // ��ȡ������������

    if (tickle_me) { // ֪ͨ�����߳�
      tickle();
    }

    // ִ����ȡ��������
    if (task.fiber && (task.fiber->getState() != Fiber::TERM) &&
        task.fiber->getState() != Fiber::EXCEPT) {
      task.fiber->swapIn(); // ����ִ��

      --m_activeThreadCount; // ִ�����������Ծ�߳�-1
      // �ж��Ƿ���Ҫִ��
      if (task.fiber->getState() == Fiber::READY) {
        schedule(task.fiber);
      } else if (task.fiber->getState() != Fiber::TERM &&
                 task.fiber->getState() != Fiber::EXCEPT) {
        task.fiber->m_state = Fiber::HOLD;
      }
      task.reset();
    } else if (task.func) { // ���Ǻ��������װ��Э�̽��е���
      if (func_fiber) {
        func_fiber->resetFunc(task.func);
      } else {
        // ��������װ��Э��(��������ջ֡)
        func_fiber.reset(new Fiber(task.func));
      }
      task.reset();

      func_fiber->swapIn();
      --m_activeThreadCount;

      // ִ����֮�󣬸���״̬�ж��Ƿ���Ҫ��������
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
    } else {                   // ��ȡ����������Ч
      if (is_active) {         // �ָ�ȡ����ǰ��״̬
        --m_activeThreadCount; // ȡ����ʱ+1��ȡ����Ч����Ӧ��-1
        continue;
      }
      if (idle_fiber->getState() == Fiber::TERM) {
        ZZ_LOG_INFO(g_logger) << "idle fiber term";
        break;
      }

      // û������ɵ���ʱ��ִ��idleЭ��
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

// �л�����һ���߳�
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