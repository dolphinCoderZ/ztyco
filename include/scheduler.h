/**
 * @brief 协程调度器封装
 * @details N-M，N个线程运行M个协程，内部有一个线程池,支持协程在线程池里面切换
 * @note 子协程向调度器添加调度任务可以解决子协程不能运行另一子协程的问题
 */

#pragma once

#include "fiber.h"
#include "mutex.h"
#include "noncopyable.h"
#include "thread.h"
#include <atomic>
#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <ostream>
#include <vector>

namespace zz {
class Scheduler {
public:
  using ptr = std::shared_ptr<Scheduler>;
  using MutexType = Mutex;

  /**
   * @brief 创建调度器
   * @param[in] threads 线程数
   * @param[in] use_caller 是否将当前线程也作为调度线程
   * @param[in] name 名称
   */
  Scheduler(size_t threads = 1, bool use_caller = true,
            const std::string &name = "");
  ~Scheduler();

  const std::string &getName() const { return m_name; }
  static Scheduler *GetThis();
  static Fiber *GetMainFiber();

  void start();
  void stop();

  // 添加调度任务
  template <class FiberOrCb> void schedule(FiberOrCb fc, int thread = -1) {
    bool need_tickle = false;
    {
      MutexType::Lock lock(m_mutex);
      need_tickle = scheduleNoLock(fc, thread);
    }
    if (need_tickle) {
      tickle();
    }
  }
  template <class InputIterator>
  void schedule(InputIterator begin, InputIterator end) {
    bool need_tickle = false;
    {
      MutexType::Lock lock(m_mutex);
      while (begin != end) {
        need_tickle = scheduleNoLock(&*begin, -1) || need_tickle;
        ++begin;
      }
    }

    if (need_tickle) {
      tickle();
    }
  }

  void switchTo(int thread = -1);
  std::ostream &dump(std::ostream &os);

protected:
  virtual void tickle();

  void run();              // 调度函数
  virtual void idle();     // 无调度任务时执行idle协程
  virtual bool stopping(); // 是否可以停止
  void setThis();          // 设置当前协程调度器

  bool hasIdleThreads() { return m_idleThreadCount > 0; }

private:
  template <class FiberOrCb> bool scheduleNoLock(FiberOrCb fc, int thread) {
    bool need_tickle = m_tasks.empty();
    ScheduleTask tk(fc, thread);
    if (tk.fiber || tk.func) {
      m_tasks.push_back(tk);
    }
    return need_tickle;
  }

private:
  struct ScheduleTask {
    Fiber::ptr fiber;
    std::function<void()> func;
    int threadId;
    // 某个协程作为任务，可指定执行线程
    ScheduleTask(Fiber::ptr f, int thr) : fiber(f), threadId(thr) {}
    ScheduleTask(Fiber::ptr *f, int thr) : threadId(thr) { fiber.swap(*f); }

    // 某个函数作为任务
    ScheduleTask(std::function<void()> f, int thr) : func(f), threadId(thr) {}
    ScheduleTask(std::function<void()> *f, int thr) : threadId(thr) {
      func.swap(*f);
    }

    ScheduleTask() : threadId(-1) {}

    void reset() {
      fiber = nullptr;
      func = nullptr;
      threadId = -1;
    }
  };

private:
  MutexType m_mutex;
  std::vector<Thread::ptr> m_threads; // 调度线程池
  std::list<ScheduleTask> m_tasks; // 待执行的任务队列(子协程任务)
  Fiber::ptr
      m_rootFiber; // use_caller为true时有效，表示caller线程中运行的调度协程
  std::string m_name; // 调度器名称
protected:
  std::vector<int> m_threadIds; // 线程id数组，用于跨线程调度
  size_t m_threadCount = 0;     // 工作线程数
  std::atomic<size_t> m_activeThreadCount = {0}; // 活跃线程数
  std::atomic<size_t> m_idleThreadCount = {0};   // 空闲线程数
  bool m_stopping = true;                        // 是否正在停止
  bool m_autoStop = false;                       // 是否自动停止
  int m_rootThreadid = 0; // use_caller时，caller线程id
};

class SchedulerSwicher : Noncopyable {
public:
  SchedulerSwicher(Scheduler *target = nullptr);
  ~SchedulerSwicher();

private:
  Scheduler *m_caller;
};

} // namespace zz