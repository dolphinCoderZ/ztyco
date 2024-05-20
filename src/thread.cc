#include "thread.h"
#include "log.h"
#include "util.h"
#include <functional>
#include <pthread.h>
#include <stdexcept>

namespace zz {

// 当前正在运行的线程实例
static thread_local Thread *t_thread = nullptr;
static thread_local std::string t_thread_name = "UNKNOW";

static zz::Logger::ptr g_logger = ZZ_LOG_NAME("system");

Thread *Thread::GetThis() { return t_thread; }
const std::string &Thread::GetName() { return t_thread_name; }
void Thread::SetName(const std::string &name) {
  if (name.empty()) {
    return;
  }
  if (t_thread) {
    t_thread->m_name = name;
  }
  t_thread_name = name;
}

Thread::Thread(std::function<void()> func, const std::string &name)
    : m_func(func), m_name(name) {
  if (name.empty()) {
    m_name = "UNKNOW";
  }

  // 构造函数开启线程
  int ret = pthread_create(&m_thread, nullptr, &Thread::run, this);
  if (ret) {
    throw std::logic_error("pthread_create error");
  }
  // 使用信号量确保在构造函数完成时，线程已经启动
  m_semaphore.wait();
}

Thread::~Thread() {
  if (m_thread) {
    pthread_detach(m_thread);
  }
}

void Thread::join() {
  if (m_thread) {
    int ret = pthread_join(m_thread, nullptr);
    if (ret) {
      throw std::logic_error("pthread_join error");
    }
    m_thread = 0;
  }
}

void *Thread::run(void *arg) {
  // 维护相关数据后调用函数例程
  Thread *thread = (Thread *)arg;
  t_thread = thread;
  t_thread_name = thread->m_name;
  thread->m_id = zz::GetThreadId();
  pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());

  std::function<void()> func;
  func.swap(thread->m_func);

  // 线程函数即将开始运行
  thread->m_semaphore.notify();

  func();
  return nullptr;
}

} // namespace zz