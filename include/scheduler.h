/**
 * @brief Э�̵�������װ
 * @details N-M��N���߳�����M��Э�̣��ڲ���һ���̳߳�,֧��Э�����̳߳������л�
 * @note ��Э�����������ӵ���������Խ����Э�̲���������һ��Э�̵�����
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
   * @brief ����������
   * @param[in] threads �߳���
   * @param[in] use_caller �Ƿ񽫵�ǰ�߳�Ҳ��Ϊ�����߳�
   * @param[in] name ����
   */
  Scheduler(size_t threads = 1, bool use_caller = true,
            const std::string &name = "");
  ~Scheduler();

  const std::string &getName() const { return m_name; }
  static Scheduler *GetThis();
  static Fiber *GetMainFiber();

  void start();
  void stop();

  // ��ӵ�������
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

  void run();              // ���Ⱥ���
  virtual void idle();     // �޵�������ʱִ��idleЭ��
  virtual bool stopping(); // �Ƿ����ֹͣ
  void setThis();          // ���õ�ǰЭ�̵�����

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
    // ĳ��Э����Ϊ���񣬿�ָ��ִ���߳�
    ScheduleTask(Fiber::ptr f, int thr) : fiber(f), threadId(thr) {}
    ScheduleTask(Fiber::ptr *f, int thr) : threadId(thr) { fiber.swap(*f); }

    // ĳ��������Ϊ����
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
  std::vector<Thread::ptr> m_threads; // �����̳߳�
  std::list<ScheduleTask> m_tasks; // ��ִ�е��������(��Э������)
  Fiber::ptr
      m_rootFiber; // use_callerΪtrueʱ��Ч����ʾcaller�߳������еĵ���Э��
  std::string m_name; // ����������
protected:
  std::vector<int> m_threadIds; // �߳�id���飬���ڿ��̵߳���
  size_t m_threadCount = 0;     // �����߳���
  std::atomic<size_t> m_activeThreadCount = {0}; // ��Ծ�߳���
  std::atomic<size_t> m_idleThreadCount = {0};   // �����߳���
  bool m_stopping = true;                        // �Ƿ�����ֹͣ
  bool m_autoStop = false;                       // �Ƿ��Զ�ֹͣ
  int m_rootThreadid = 0; // use_callerʱ��caller�߳�id
};

class SchedulerSwicher : Noncopyable {
public:
  SchedulerSwicher(Scheduler *target = nullptr);
  ~SchedulerSwicher();

private:
  Scheduler *m_caller;
};

} // namespace zz