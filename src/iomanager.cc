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
  // ���ԣ����������¼������ѱ�ע���
  ZZ_ASSERT(events & evt);
  // ������¼�����ʾ���ٹ�ע���¼��ˣ���ע��IO�¼���һ���Եģ�ÿ�δ�������Ҫ�������
  events = (Event)(events & ~events);

  EventContext &ectx = getEventContext(evt);
  if (ectx.cb) {
    ectx.scheduler->schedule(&ectx.cb); // ��ӵ�������
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
  // ע��pipe�ɶ��¼�������tickleЭ��
  epoll_event event;
  bzero(&event, sizeof(epoll_event));
  event.events = EPOLLIN | EPOLLET; // ET���ش���
  event.data.fd = m_tickleFds[0];
  // ���÷�����
  int flag = fcntl(m_tickleFds[0], F_GETFL);
  flag |= O_NONBLOCK;
  fcntl(m_tickleFds[0], flag);
  // ��������pipe����
  ret = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
  ZZ_ASSERT(!ret);

  // ��ʼ������ش�С
  contextResize(32);

  // ��������
  start();
}

IOManager::~IOManager() {
  stop(); // �ȴ�������������ȫ������
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
  // �ҵ�fd��Ӧ��FdContext����������ڣ��ͷ���һ��
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
  // ͬһ��fd�������ظ������ͬ���¼�
  if (ZZ_UNLIKELY(fd_ctx->events & e)) {
    ZZ_LOG_ERROR(g_logger) << "addEvent assert fd=" << fd
                           << " event=" << (EPOLL_EVENTS)e
                           << " fd_ctx.event=" << (EPOLL_EVENTS)fd_ctx->events;
    ZZ_ASSERT(!(fd_ctx->events & e));
  }

  // ������¼�
  int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
  epoll_event epevt;
  epevt.events = EPOLLIN | fd_ctx->events | e;
  epevt.data.ptr = fd_ctx;

  // ���ں˼��������
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
  // ע���Ӧ�¼��Ļص�����
  fd_ctx->events = (Event)(fd_ctx->events | e);
  FdContext::EventContext &event_ctx = fd_ctx->getEventContext(e);
  ZZ_ASSERT(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb);

  event_ctx.scheduler = Scheduler::GetThis();
  if (cb) {
    event_ctx.cb.swap(cb); // ע��ص�
  } else { // û�лص�����Ĭ�Ͻ���ǰЭ����Ϊִ����
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

  // û��Ҫɾ�����¼�
  FdContext::MutexType::Lock lk(fd_ctx->mutex);
  if (ZZ_UNLIKELY(!(fd_ctx->events & e))) {
    return false;
  }

  Event new_events = (Event)(fd_ctx->events & ~e);
  // ���ָ�����¼���������֮����Ϊ0�����epoll_wait��ɾ�����ļ�������
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

  // ȡ���¼�֮ǰ����һ���¼�
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

  // �����Ѿ�ע����¼�
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
 * д�ܵ�֪ͨ����Э�̡�Ҳ����Scheduler::run()��idle���˳�
 * Scheduler::run()ÿ�δ�idleЭ�����˳�֮�󣬶����������������������ִ���������½���idle
 */
void IOManager::tickle() {
  if (!hasIdleThreads()) {
    return;
  }
  int ret = write(m_tickleFds[1], "T", 1);
  ZZ_ASSERT(ret == 1);
}

// ����IOManager���ԣ���������д����ȵ�IO�¼���ִ�����˲ſ����˳�
bool IOManager::stopping(uint64_t &timeout) {
  timeout = getNextTimer();
  // ��֤û��ʣ��Ķ�ʱ��������
  return timeout == ~0ull && m_pendingEventCount == 0 && Scheduler::stopping();
}

bool IOManager::stopping() {
  uint64_t timeout = 0;
  return stopping(timeout);
}

/**
 * �������޵�������ʱ��������idleЭ���ϣ���IO���������ԣ�idle״̬Ӧ�ù�ע������
 * һ����û���µĵ������񣬶�ӦSchduler::schedule()��
 * ������µĵ���������Ӧ�������˳�idle״̬��ת������Э��ִ�У�
 * ���ǹ�ע��ǰע�������IO�¼���û�д���������д�����idle����triggerEvent
 */
void IOManager::idle() {
  ZZ_LOG_DEBUG(g_logger) << "idle";
  // һ��epoll_wait�����256�������¼�
  const uint64_t MAX_EVENTS = 256;
  epoll_event *epevts = new epoll_event[MAX_EVENTS]();
  std::shared_ptr<epoll_event> shared_epevt(
      epevts, [](epoll_event *ptr) { delete[] ptr; });

  while (1) {
    // stopping��ȡ��һ����ʱ���ĳ�ʱʱ�䣬˳���жϵ������Ƿ�ֹͣ
    uint64_t next_timeout;
    if (ZZ_UNLIKELY(stopping(next_timeout))) {
      ZZ_LOG_DEBUG(g_logger) << "name=" << getName() << "idle stopping exit";
      break;
    }

    int ret;
    // ȷ��epoll_wait��ʱʱ��
    do {
      static const int MAX_TIMEOUT = 5000;
      if (next_timeout != ~0ull) {
        // Ĭ�ϳ�ʱʱ��5�룬�����һ����ʱ���ĳ�ʱʱ�����5�룬����5�������㳬ʱ
        next_timeout = std::min((int)next_timeout, MAX_TIMEOUT);
      } else {
        next_timeout = MAX_TIMEOUT;
      }
      // 1.������epoll_wait�ϣ��ȴ��¼�������ʱ����ʱ
      ret = epoll_wait(m_epfd, epevts, MAX_EVENTS, (int)next_timeout);
      if (ret < 0 && errno == EINTR) {
        continue;
      } else {
        break;
      }
    } while (1);

    // 2.����ǳ�ʱ��ʱ������
    std::vector<std::function<void()>> cbs;
    listExpiredCb(cbs);
    if (!cbs.empty()) {
      schedule(cbs.begin(), cbs.end());
      cbs.clear();
    }

    // 2.������¼�������
    for (int i = 0; i < ret; ++i) {
      epoll_event &event = epevts[i];
      if (event.data.fd == m_tickleFds[0]) {
        uint8_t dummy[256];
        // ticklefd[0]����֪ͨidle���������¼�֪ͨidle
        while (read(m_tickleFds[0], dummy, sizeof(dummy)) > 0)
          ;
        continue;
      }

      // 3.�Ӿ����¼�ָ���ϻ�ȡfd�����ļ�鷢���¼�
      FdContext *fd_ctx = (FdContext *)event.data.ptr; //(reactor)
      FdContext::MutexType::Lock lock(fd_ctx->mutex);
      /**
       * EPOLLERR: ��������д�����Ѿ��رյ�pipe
       * EPOLLHUP: �׽��ֶԶ˹ر�
       * �����������¼���Ӧ��ͬʱ����fd�Ķ���д�¼��������п��ܳ���ע����¼���Զִ�в��������
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

      // 4.�޳��Ѿ��������¼�����ʣ�µ��¼����¼���epoll_wait
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

      // 5.�����Ѿ��������¼���Ҳ���ǽ�������������
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
     * һ�����������е��¼���idleЭ��yield������Э��(Scheduler::run)���¼���Ƿ���������Ҫ����
     * triggerEventʵ��ֻ�ǰѶ�Ӧ��fiber��������idle�˳��󣬵���Э�̲���ȡ����
     */
    Fiber::ptr cur = Fiber::GetThis();
    auto rawPtr = cur.get();
    cur.reset();
    rawPtr->swapOut(); // idle�лص���Э��
  }
}

void IOManager::onTimerInsertedAtFront() { tickle(); }

} // namespace zz