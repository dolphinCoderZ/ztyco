#include "iomanager.h"
#include "fiber.h"
#include "log.h"
#include "macro.h"
#include "mutex.h"
#include "thread.h"
#include "timer.h"
#include "util.h"
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <strings.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>

namespace zz {
static zz::Logger::ptr g_logger = ZZ_LOG_NAME("system");

enum EpollCtlOp {};

static std::ostream &operator<<(std::ostream &os, const EpollCtlOp &op) {
  switch ((int)op) {
#define XX(ctl)                                                                \
  case ctl:                                                                    \
    return os << #ctl;
    XX(EPOLL_CTL_ADD);
    XX(EPOLL_CTL_MOD);
    XX(EPOLL_CTL_DEL);
#undef XX
  default:
    return os << (int)op;
  }
}

static std::ostream &operator<<(std::ostream &os, EPOLL_EVENTS evts) {
  if (!evts) {
    return os << "0";
  }

  bool first = true;
#define XX(E)                                                                  \
  if (evts & E) {                                                              \
    if (!first) {                                                              \
      os << "|";                                                               \
    }                                                                          \
    os << #E;                                                                  \
    first = false;                                                             \
  }
  XX(EPOLLIN);
  XX(EPOLLPRI);
  XX(EPOLLOUT);
  XX(EPOLLRDNORM);
  XX(EPOLLRDBAND);
  XX(EPOLLWRNORM);
  XX(EPOLLWRBAND);
  XX(EPOLLMSG);
  XX(EPOLLERR);
  XX(EPOLLHUP);
  XX(EPOLLRDHUP);
  XX(EPOLLONESHOT);
  XX(EPOLLET);
#undef XX
  return os;
}

IOManager::FdContext::EventContext &
IOManager::FdContext::getEventContext(Event e) {
  switch (e) {
  case IOManager::READ:
    return read;
  case IOManager::WRITE:
    return write;
  default:
    ZZ_ASSERT2(false, "getEventContext");
  }
  throw std::invalid_argument("getEventContext invalid event");
}

void IOManager::FdContext::resetEventContext(EventContext &ectx) {
  ectx.scheduler = nullptr;
  ectx.fiber.reset();
  ectx.cb = nullptr;
}

void IOManager::FdContext::triggerEvent(IOManager::Event evt) {
  // 断言：待触发的事件必须已被注册过
  ZZ_ASSERT(events & evt);
  // 清除该事件，表示不再关注该事件了，即注册IO事件是一次性的，每次触发后需要重新添加
  events = (Event)(events & ~events);

  EventContext &ectx = getEventContext(evt);
  if (ectx.cb) {
    ectx.scheduler->schedule(&ectx.cb); // 添加调度任务
  } else {
    ectx.scheduler->schedule(&ectx.fiber);
  }
  resetEventContext(ectx);
}

IOManager::IOManager(size_t threads, bool use_caller, const std::string &name)
    : Scheduler(threads, use_caller, name) {
  m_epfd = epoll_create(5000);
  ZZ_ASSERT(m_epfd > 0);

  int ret = pipe(m_tickleFds);
  ZZ_ASSERT(!ret);
  // 注册pipe可读事件，用于tickle协程
  epoll_event event;
  bzero(&event, sizeof(epoll_event));
  event.events = EPOLLIN | EPOLLET; // ET边沿触发
  event.data.fd = m_tickleFds[0];
  // 设置非阻塞
  int flag = fcntl(m_tickleFds[0], F_GETFL);
  flag |= O_NONBLOCK;
  fcntl(m_tickleFds[0], flag);
  // 上树监听pipe读端
  ret = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
  ZZ_ASSERT(!ret);

  // 初始化对象池大小
  contextResize(32);

  // 启动调度
  start();
}

IOManager::~IOManager() {
  stop(); // 等待调度器调度完全部任务
  close(m_epfd);
  close(m_tickleFds[0]);
  close(m_tickleFds[1]);

  for (size_t i = 0; i < m_fdContexts.size(); ++i) {
    if (m_fdContexts[i]) {
      delete m_fdContexts[i];
    }
  }
}

void IOManager::contextResize(size_t size) {
  m_fdContexts.resize(size);
  for (size_t i = 0; i < m_fdContexts.size(); ++i) {
    m_fdContexts[i] = new FdContext;
    m_fdContexts[i]->fd = i;
  }
}

int IOManager::addEvent(int fd, Event e, std::function<void()> cb) {
  // 找到fd对应的FdContext，如果不存在，就分配一个
  FdContext *fd_ctx = nullptr;
  RWMutexType::ReadLock lock(m_mutex);
  if ((int)m_fdContexts.size() > fd) {
    fd_ctx = m_fdContexts[fd];
    lock.unlock();
  } else {
    lock.unlock();
    RWMutexType::WriteLock lk(m_mutex);
    contextResize(fd * 1.5);
    fd_ctx = m_fdContexts[fd];
  }

  FdContext::MutexType::Lock mtx(fd_ctx->mutex);
  // 同一个fd不允许重复添加相同的事件
  if (ZZ_UNLIKELY(fd_ctx->events & e)) {
    ZZ_LOG_ERROR(g_logger) << "addEvent assert fd=" << fd
                           << " event=" << (EPOLL_EVENTS)e
                           << " fd_ctx.event=" << (EPOLL_EVENTS)fd_ctx->events;
    ZZ_ASSERT(!(fd_ctx->events & e));
  }

  // 添加新事件
  int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
  epoll_event epevt;
  epevt.events = EPOLLIN | fd_ctx->events | e;
  epevt.data.ptr = fd_ctx;

  // 上内核监听红黑树
  int ret = epoll_ctl(m_epfd, op, fd, &epevt);
  if (ret) {
    ZZ_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", " << (EpollCtlOp)op
                           << ", " << fd << ", " << (EPOLL_EVENTS)epevt.events
                           << "):" << ret << " (" << errno << ") ("
                           << strerror(errno) << ") fd_ctx->events="
                           << (EPOLL_EVENTS)fd_ctx->events;
    return -1;
  }

  ++m_pendingEventCount;
  // 注册对应事件的回调函数
  fd_ctx->events = (Event)(fd_ctx->events | e);
  FdContext::EventContext &event_ctx = fd_ctx->getEventContext(e);
  ZZ_ASSERT(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb);

  event_ctx.scheduler = Scheduler::GetThis();
  if (cb) {
    event_ctx.cb.swap(cb); // 注册回调
  } else { // 没有回调，则默认将当前协程作为执行体
    event_ctx.fiber = Fiber::GetThis();
    ZZ_ASSERT2(event_ctx.fiber->getState() == Fiber::EXEC,
               "state=" << event_ctx.fiber->getState());
  }
  return 0;
}

bool IOManager::delEvent(int fd, Event e) {
  RWMutexType::ReadLock lock(m_mutex);
  if ((int)m_fdContexts.size() <= fd) {
    return false;
  }
  FdContext *fd_ctx = m_fdContexts[fd];
  lock.unlock();

  // 没有要删除的事件
  FdContext::MutexType::Lock lk(fd_ctx->mutex);
  if (ZZ_UNLIKELY(!(fd_ctx->events & e))) {
    return false;
  }

  Event new_events = (Event)(fd_ctx->events & ~e);
  // 清除指定的事件，如果清除之后结果为0，则从epoll_wait中删除该文件描述符
  int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
  epoll_event epevt;
  epevt.events = new_events | EPOLLET;
  epevt.data.ptr = fd_ctx;

  int ret = epoll_ctl(m_epfd, op, fd, &epevt);
  if (ret) {
    ZZ_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", " << (EpollCtlOp)op
                           << ", " << fd << ", " << (EPOLL_EVENTS)epevt.events
                           << "):" << ret << " (" << errno << ") ("
                           << strerror(errno) << ")";
    return false;
  }

  --m_pendingEventCount;
  fd_ctx->events = new_events;
  FdContext::EventContext &event_ctx = fd_ctx->getEventContext(e);
  fd_ctx->resetEventContext(event_ctx);
  return true;
}

bool IOManager::cancelEvent(int fd, Event e) {
  RWMutex::ReadLock lock(m_mutex);
  if ((int)m_fdContexts.size() <= fd) {
    return false;
  }
  FdContext *fd_ctx = m_fdContexts[fd];
  lock.unlock();

  FdContext::MutexType::Lock lk(fd_ctx->mutex);
  if (ZZ_UNLIKELY(!(fd_ctx->events & e))) {
    return false;
  }

  Event new_events = (Event)(fd_ctx->events & ~e);
  int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
  epoll_event epevt;
  epevt.events = new_events | EPOLLIN;
  epevt.data.ptr = fd_ctx;

  int ret = epoll_ctl(m_epfd, op, fd, &epevt);
  if (ret) {
    ZZ_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", " << (EpollCtlOp)op
                           << ", " << fd << ", " << (EPOLL_EVENTS)epevt.events
                           << "):" << ret << " (" << errno << ") ("
                           << strerror(errno) << ")";
    return false;
  }

  // 取消事件之前触发一次事件
  fd_ctx->triggerEvent(e);
  --m_pendingEventCount;
  return true;
}

bool IOManager::cancelAll(int fd) {
  RWMutexType::ReadLock lock(m_mutex);
  if ((int)m_fdContexts.size() <= fd) {
    return false;
  }
  FdContext *fd_ctx = m_fdContexts[fd];
  lock.unlock();

  FdContext::MutexType::Lock lk(fd_ctx->mutex);
  if (!fd_ctx->events) {
    return false;
  }

  int op = EPOLL_CTL_DEL;
  epoll_event epevt;
  epevt.events = 0;
  epevt.data.ptr = fd_ctx;

  int ret = epoll_ctl(m_epfd, op, fd, &epevt);
  if (ret) {
    ZZ_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", " << (EpollCtlOp)op
                           << ", " << fd << ", " << (EPOLL_EVENTS)epevt.events
                           << "):" << ret << " (" << errno << ") ("
                           << strerror(errno) << ")";
    return false;
  }

  // 触发已经注册的事件
  if (fd_ctx->events & READ) {
    fd_ctx->triggerEvent(READ);
    --m_pendingEventCount;
  }
  if (fd_ctx->events & WRITE) {
    fd_ctx->triggerEvent(WRITE);
    --m_pendingEventCount;
  }

  ZZ_ASSERT(fd_ctx->events == 0);
  return true;
}

IOManager *IOManager::GetThis() {
  return dynamic_cast<IOManager *>(Scheduler::GetThis());
}

/**
 * 写管道通知调度协程、也就是Scheduler::run()从idle中退出
 * Scheduler::run()每次从idle协程中退出之后，都会把任务队列里的所有任务执行完再重新进入idle
 */
void IOManager::tickle() {
  if (!hasIdleThreads()) {
    return;
  }
  int ret = write(m_tickleFds[1], "T", 1);
  ZZ_ASSERT(ret == 1);
}

// 对于IOManager而言，必须等所有待调度的IO事件都执行完了才可以退出
bool IOManager::stopping(uint64_t &timeout) {
  timeout = getNextTimer();
  // 保证没有剩余的定时器待触发
  return timeout == ~0ull && m_pendingEventCount == 0 && Scheduler::stopping();
}

bool IOManager::stopping() {
  uint64_t timeout = 0;
  return stopping(timeout);
}

/**
 * 调度器无调度任务时会阻塞在idle协程上，对IO调度器而言，idle状态应该关注两件事
 * 一是有没有新的调度任务，对应Schduler::schedule()，
 * 如果有新的调度任务，那应该立即退出idle状态，转而调度协程执行；
 * 二是关注当前注册的所有IO事件有没有触发，如果有触发，idle调用triggerEvent
 */
void IOManager::idle() {
  ZZ_LOG_DEBUG(g_logger) << "idle";
  // 一次epoll_wait最多检测256个就绪事件
  const uint64_t MAX_EVENTS = 256;
  epoll_event *epevts = new epoll_event[MAX_EVENTS]();
  std::shared_ptr<epoll_event> shared_epevt(
      epevts, [](epoll_event *ptr) { delete[] ptr; });

  while (1) {
    // stopping获取下一个定时器的超时时间，顺便判断调度器是否停止
    uint64_t next_timeout;
    if (ZZ_UNLIKELY(stopping(next_timeout))) {
      ZZ_LOG_DEBUG(g_logger) << "name=" << getName() << "idle stopping exit";
      break;
    }

    int ret;
    // 确定epoll_wait超时时间
    do {
      static const int MAX_TIMEOUT = 5000;
      if (next_timeout != ~0ull) {
        // 默认超时时间5秒，如果下一个定时器的超时时间大于5秒，仍以5秒来计算超时
        next_timeout = std::min((int)next_timeout, MAX_TIMEOUT);
      } else {
        next_timeout = MAX_TIMEOUT;
      }
      // 1.阻塞在epoll_wait上，等待事件发生或定时器超时
      ret = epoll_wait(m_epfd, epevts, MAX_EVENTS, (int)next_timeout);
      if (ret < 0 && errno == EINTR) {
        continue;
      } else {
        break;
      }
    } while (1);

    // 2.如果是超时定时器触发
    std::vector<std::function<void()>> cbs;
    listExpiredCb(cbs);
    if (!cbs.empty()) {
      schedule(cbs.begin(), cbs.end());
      cbs.clear();
    }

    // 2.如果是事件被触发
    for (int i = 0; i < ret; ++i) {
      epoll_event &event = epevts[i];
      if (event.data.fd == m_tickleFds[0]) {
        uint8_t dummy[256];
        // ticklefd[0]用于通知idle，触发读事件通知idle
        while (read(m_tickleFds[0], dummy, sizeof(dummy)) > 0)
          ;
        continue;
      }

      // 3.从就绪事件指针上获取fd上下文检查发生事件
      FdContext *fd_ctx = (FdContext *)event.data.ptr; //(reactor)
      FdContext::MutexType::Lock lock(fd_ctx->mutex);
      /**
       * EPOLLERR: 出错，比如写读端已经关闭的pipe
       * EPOLLHUP: 套接字对端关闭
       * 出现这两种事件，应该同时触发fd的读和写事件，否则有可能出现注册的事件永远执行不到的情况
       */
      if (event.events & (EPOLLERR | EPOLLHUP)) {
        event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
      }

      int real_evts = NONE;
      if (event.events & EPOLLIN) {
        real_evts |= READ;
      }
      if (event.events & EPOLLOUT) {
        real_evts |= WRITE;
      }
      if ((fd_ctx->events & real_evts) == NONE) {
        continue;
      }

      // 4.剔除已经发生的事件，将剩下的事件重新加入epoll_wait
      int evts_left = (fd_ctx->events & ~real_evts);
      int op = evts_left ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
      event.events = evts_left | EPOLLET;

      int rt = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
      if (rt) {
        ZZ_LOG_ERROR(g_logger)
            << "epoll_ctl(" << m_epfd << ", " << (EpollCtlOp)op << ", "
            << fd_ctx->fd << ", " << (EPOLL_EVENTS)event.events << "):" << rt
            << " (" << errno << ") (" << strerror(errno) << ")";
        continue;
      }

      // 5.处理已经发生的事件，也就是将其加入任务队列
      if (real_evts & READ) {
        fd_ctx->triggerEvent(READ);
        --m_pendingEventCount;
      }
      if (real_evts & WRITE) {
        fd_ctx->triggerEvent(WRITE);
        --m_pendingEventCount;
      }
    }

    /**
     * 一旦处理完所有的事件，idle协程yield，调度协程(Scheduler::run)重新检查是否有新任务要调度
     * triggerEvent实际只是把对应的fiber加入任务，idle退出后，调度协程才能取任务
     */
    Fiber::ptr cur = Fiber::GetThis();
    auto rawPtr = cur.get();
    cur.reset();
    rawPtr->swapOut(); // idle切回调度协程
  }
}

void IOManager::onTimerInsertedAtFront() { tickle(); }

} // namespace zz