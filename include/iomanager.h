/**
 * @brief 基于Epoll的IO协程调度器
 */

#pragma once

#include "fiber.h"
#include "mutex.h"
#include "scheduler.h"
#include "timer.h"
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace zz {
class IOManager : public Scheduler, public TimerManager {
public:
  using ptr = std::shared_ptr<IOManager>;
  using RWMutexType = RWMutex;

  enum Event { NONE = 0x0, READ = 0x1, WRITE = 0x4 };

private:
  /**
   * @brief socket fd上下文类
   * @details
   * 对应一个FdContext，包括fd的值，fd上的事件，以及fd的读写事件回调函数
   */
  struct FdContext {
    using MutexType = Mutex;
    /**
     * @brief 事件上下文类
     */
    struct EventContext {
      // 事件执行所用的调度器
      Scheduler *scheduler = nullptr;
      // 事件的协程
      Fiber::ptr fiber;
      // 事件回调函数
      std::function<void()> cb;
    };

    EventContext &getEventContext(Event e);
    void resetEventContext(EventContext &ectx);
    void triggerEvent(Event e);

    EventContext read;
    EventContext write;
    int fd = 0;          // 事件关联的句柄
    Event events = NONE; // 句柄上关心的事件类型
    MutexType mutex;
  };

public:
  IOManager(size_t threads = 1, bool use_caller = true,
            const std::string &name = "");
  ~IOManager();

  /**
   * @brief 添加事件
   * @details fd描述符发生了event事件时执行cb函数
   * @param[in] fd socket句柄
   * @param[in] event 事件类型
   * @param[in] cb 事件回调函数
   */
  int addEvent(int fd, Event e, std::function<void()> cb = nullptr);

  /**
   * @brief 删除事件
   * @param[in] fd socket句柄
   * @param[in] event 事件类型
   * @attention 不会触发事件
   */
  bool delEvent(int fd, Event e);

  /**
   * @brief 取消事件
   * @param[in] fd socket句柄
   * @param[in] event 事件类型
   * @attention 如果该事件被注册过回调，那就触发一次回调事件
   */
  bool cancelEvent(int fd, Event e);
  bool cancelAll(int fd);

  static IOManager *GetThis();

protected:
  /**
   * @brief 通知调度器有任务要调度
   * @details
   * 写pipe让idle协程从epoll_wait退出，待idle协程yield之后Scheduler::run就可以调度其他任务
   */
  void tickle() override;

  bool stopping() override;
  /**
   * @brief 判断是否可以停止，同时获取最近一个定时器的超时时间
   * @param[out] timeout 最近一个定时器的超时时间，用于idle协程的epoll_wait
   * @return 返回是否可以停止
   */
  bool stopping(uint64_t &timeout);

  /**
   * @brief idle协程
   * @details
   * 对于IO协程调度来说，应阻塞在等待IO事件上，idle退出的时机是epoll_wait返回，对应的操作是tickle或注册的IO事件发生
   */
  void idle() override;

  /**
   * @brief 新定时器插入头部，更新超时时间，唤醒idle协程使用新的超时时间
   */
  void onTimerInsertedAtFront() override;

  /**
   * @brief 重置socket句柄上下文对象池大小
   * @param[in] size 容量大小
   */
  void contextResize(size_t size);

private:
  int m_epfd = 0;     // epoll文件句柄
  int m_tickleFds[2]; // pipe句柄，fd[0]读端，fd[1]写端
  std::atomic<size_t> m_pendingEventCount = {0}; // 就绪IO事件数量
  RWMutexType m_mutex;
  std::vector<FdContext *> m_fdContexts; // fd对象池，所有已经注册的fd
};

} // namespace zz