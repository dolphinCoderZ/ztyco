#pragma once

#include "mutex.h"
#include "noncopyable.h"
#include <functional>
#include <memory>
#include <pthread.h>
#include <sched.h>

namespace zz {
class Thread : Noncopyable {
public:
  using ptr = std::shared_ptr<Thread>;

  /**
   * @param[in] func 线程执行函数
   * @param[in] name 线程名称
   */
  Thread(std::function<void()> func, const std::string &name);
  ~Thread();

  pid_t getId() const { return m_id; }
  const std::string &getName() const { return m_name; }
  void join();

  // 当前线程
  static Thread *GetThis();
  static const std::string &GetName();
  static void SetName(const std::string &name);

private:
  static void *run(void *arg);

private:
  pid_t m_id = -1;
  pthread_t m_thread = 0;
  std::string m_name;

  std::function<void()> m_func;
  Semaphore m_semaphore;
};

} // namespace zz