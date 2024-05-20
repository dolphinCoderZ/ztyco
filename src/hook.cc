#include "hook.h"
#include "config.h"
#include "fdmanager.h"
#include "fiber.h"
#include "iomanager.h"
#include "log.h"
#include "macro.h"
#include "scheduler.h"
#include "timer.h"
#include <asm-generic/socket.h>
#include <asm-generic/sockios.h>
#include <bits/types/struct_timeval.h>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <ctime>
#include <dlfcn.h>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

zz::Logger::ptr g_logger = ZZ_LOG_NAME("system");

namespace zz {
static zz::ConfigVar<int>::ptr g_tcp_connet_timeout =
    zz::Config::Lookup("tcp.connect.timeout", 5000, "tcp connect timeout");

// hook�������߳�Ϊ��λ
static thread_local bool t_hook_enable = false;

#define HOOK_FUN(XX)                                                           \
  XX(sleep)                                                                    \
  XX(usleep)                                                                   \
  XX(nanosleep)                                                                \
  XX(socket)                                                                   \
  XX(connect)                                                                  \
  XX(accept)                                                                   \
  XX(read)                                                                     \
  XX(readv)                                                                    \
  XX(recv)                                                                     \
  XX(recvfrom)                                                                 \
  XX(recvmsg)                                                                  \
  XX(write)                                                                    \
  XX(writev)                                                                   \
  XX(send)                                                                     \
  XX(sendto)                                                                   \
  XX(sendmsg)                                                                  \
  XX(close)                                                                    \
  XX(fcntl)                                                                    \
  XX(ioctl)                                                                    \
  XX(getsockopt)                                                               \
  XX(setsockopt)

// ��������ԭʼAPI�ı���
extern "C" {
#define XX(name) name##_fun name##_f = nullptr;
HOOK_FUN(XX);
#undef XX
}

// ��ʼ��������ԭʼAPI����ڵ�ַ��hookֻ�Ƕ�ԭʼAPI���и���
void hook_init() {
  static bool is_inited = false;
  if (is_inited) {
    return;
  }
  // �ҵ�ԭʼAPI��name##_f����ľ���ԭʼAPI
#define XX(name) name##_f = (name##_fun)dlsym(RTLD_NEXT, #name);
  HOOK_FUN(XX);
#undef XX
}
/**
 * ##���ã� ���Ӻ�׺
 * #���ã� ����ʶ��ת��Ϊ�ַ�������ֵ
 * dlsym ���ã� ���ұ�׼���к����ķ��ŵ�ַ
 */
/*
ͷ�ļ���extern���ⲿ�����������˱�׼��API
sleep_f = (sleep_fun)dlsym(RTLD_NEXT, "sleep");
...
usleep_f = (usleep_fun)dlsym(RTLD_NEXT, "usleep");
setsocketopt_f = (setsocketopt_fun)dlsym(RTLD_NEXT, "setsocketopt");
*/

static uint64_t s_connect_timeout = -1;
struct _HookIniter {
  _HookIniter() {
    // �ڹ��쾲̬����ʱ�͵���hook_init��main��������֮ǰ��ȡԭʼAPI
    hook_init();

    s_connect_timeout = g_tcp_connet_timeout->getValue();
    g_tcp_connet_timeout->addListener([](const int &oldval, const int &newVal) {
      ZZ_LOG_INFO(g_logger)
          << "tcp connect timeout changed from " << oldval << " to " << newVal;
      s_connect_timeout = newVal;
    });
  }
};
// ��ʼ����̬������mian֮����ɳ�ʼ����.data�ξͱ����˸���ԭʼAPI�ĵ�ַ
static _HookIniter s_hook_initer;

bool is_hook_enable() { return t_hook_enable; }
void set_hook_enable(bool flag) { t_hook_enable = flag; }

// ��ʱ����
struct timer_info {
  int cancelled = 0;
};

template <typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, const char *hook_fun_name,
                     uint32_t event, int timeout_so, Args &&...args) {
  if (!zz::t_hook_enable) {
    return fun(fd, std::forward<Args>(args)...);
  }
  zz::FdCtx::ptr ctx = zz::FdMgr::GetInstance()->get(fd);
  if (!ctx) {
    return fun(fd, std::forward<Args>(args)...);
  }
  if (ctx->isClose()) {
    errno = EBADF;
    return -1;
  }
  if (!ctx->isScoket() || ctx->getUserNonblock()) {
    return fun(fd, std::forward<Args>(args)...);
  }

  uint64_t to = ctx->getTimeout(timeout_so);
  std::shared_ptr<timer_info> tinfo(new timer_info);
retry:
  ssize_t n = fun(fd, std::forward<Args>(args)...);
  while (n == -1 && errno == EINTR) {
    n = fun(fd, std::forward<Args>(args)...); // ���жϾͼ���
  }
  if (n == -1 && errno == EAGAIN) {
    zz::IOManager *iom = zz::IOManager::GetThis();
    zz::Timer::ptr timer;
    std::weak_ptr<timer_info> winfo(tinfo);

    // ����������ʱ��
    if (to != (uint64_t)-1) {
      timer = iom->addConditionTimer(
          to,
          [winfo, fd, iom, event]() {
            auto t = winfo.lock();
            if (!t || t->cancelled) {
              return;
            }
            t->cancelled = ETIMEDOUT;
            iom->cancelEvent(fd, (zz::IOManager::Event)(event));
          },
          winfo);
    }

    // �����¼�
    int ret = iom->addEvent(fd, (zz::IOManager::Event)(event));
    if (ZZ_UNLIKELY(ret)) {
      ZZ_LOG_ERROR(g_logger)
          << hook_fun_name << " addEvent(" << fd << ", " << event << ")";
      if (timer) {
        timer->cancel();
      }
      return -1;
    } else {
      zz::Fiber::GetThis()->YieldToHold();
      if (timer) {
        timer->cancel();
      }
      if (tinfo->cancelled) {
        errno = tinfo->cancelled;
        return -1;
      }
      goto retry;
    }
  }

  return n;
}

/*
sleep��ʱϵ�нӿڣ�����sleep/usleep/nanosleep��������Щ�ӿڵ�hook��ֻ��Ҫ��IOЭ�̵�����ע��һ����ʱ�¼����ڶ�ʱ�¼��������ټ���ִ�е�ǰЭ�̼��ɡ���ǰЭ����ע���궨ʱ�¼��󼴿�yield�ó�ִ��Ȩ��

socketϵ�нӿڣ�����read/write/recv/send...�ȣ�connect��acceptҲ���Թ鵽����ӿ��С�����ӿڵ�hook������Ҫ�жϲ�����fd�Ƿ���sockfd���Լ��û��Ƿ���ʽ�ضԸ�fd���ù�������ģʽ���������sockfd�����û���ʽ���ù�������ģʽ����ô�Ͳ���Ҫhook��ֱ�ӵ��ò���ϵͳ��IO�ӿڼ��ɡ������Ҫhook����ô������IOЭ�̵�������ע���Ӧ�Ķ�д�¼������¼��������ټ���ִ�е�ǰЭ�̡���ǰЭ����ע����IO�¼�����yield�ó�ִ��Ȩ��

socket/fcntl/ioctl/close�Ƚӿڣ�����ӿ���Ҫ�������Ǳ�Ե������������fd�����ģ�������ʱ���û���ʽ���÷��������⡣
*/

// 1.���Ӷ�ʱ�� 2.yield�ó�ִ��Ȩ
unsigned int sleep(unsigned int seconds) {
  if (!zz::t_hook_enable) {
    return sleep_f(seconds);
  }

  zz::Fiber::ptr fiber = zz::Fiber::GetThis();
  zz::IOManager *iom = zz::IOManager::GetThis();
  iom->addTimer(seconds * 1000,
                std::bind((void(zz::Scheduler::*)(zz::Fiber::ptr, int thread)) &
                              zz::IOManager::schedule,
                          iom, fiber, -1));
  zz::Fiber::YieldToHold();
  return 0;
}

int usleep(useconds_t usec) {
  if (!zz::t_hook_enable) {
    return usleep_f(usec);
  }

  zz::Fiber::ptr fiber = zz::Fiber::GetThis();
  zz::IOManager *iom = zz::IOManager::GetThis();
  iom->addTimer(usec / 1000,
                std::bind((void(zz::Scheduler::*)(zz::Fiber::ptr, int thread)) &
                              zz::IOManager::schedule,
                          iom, fiber, -1));
  zz::Fiber::YieldToHold();
  return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
  if (!zz::t_hook_enable) {
    return nanosleep_f(req, rem);
  }

  int timeout_ms = req->tv_sec * 1000 + req->tv_nsec / 1000 / 1000;
  zz::Fiber::ptr fiber = zz::Fiber::GetThis();
  zz::IOManager *iom = zz::IOManager::GetThis();
  iom->addTimer(timeout_ms,
                std::bind((void(zz::Scheduler::*)(zz::Fiber::ptr, int thread)) &
                              zz::IOManager::schedule,
                          iom, fiber, -1));
  zz::Fiber::YieldToHold();
  return 0;
}

// �����׽��֣����ӵ�FdManager���й���
int socket(int domain, int type, int protocol) {
  if (!zz::t_hook_enable) {
    return socket_f(domain, type, protocol);
  }
  int fd = socket_f(domain, type, protocol);
  if (fd == -1) {
    return -1;
  }
  zz::FdMgr::GetInstance()->get(fd, true);
  return fd;
}

// ����ʱ��connect������Ӧfdע�ᶨʱ��
int connect_with_timeout(int fd, const struct sockaddr *addr, socklen_t addrlen,
                         uint64_t timeout_ms) {

  if (!zz::t_hook_enable) {
    return connect_f(fd, addr, addrlen);
  }

  zz::FdCtx::ptr ctx = zz::FdMgr::GetInstance()->get(fd);
  if (!ctx || ctx->isClose()) {
    errno = EBADF;
    return -1;
  }
  if (!ctx->isScoket()) {
    return connect_f(fd, addr, addrlen);
  }
  if (!ctx->getUserNonblock()) {
    return connect_f(fd, addr, addrlen);
  }

  int n = connect_f(fd, addr, addrlen);
  if (n == 0) {
    return 0;
  } else if (n != -1 ||
             errno == EINPROGRESS) { // �����׽����Ƿ�����,�᷵��EINPROGRESS����
    return n;
  }

  zz::IOManager *iom = zz::IOManager::GetThis();
  zz::Timer::ptr timer;
  std::shared_ptr<timer_info> tinfo(new timer_info);
  std::weak_ptr<timer_info> winfo(tinfo);

  // ���ӳ�ʱ������������ʱ��
  if (timeout_ms != (uint64_t)-1) {
    // ����������ʱ��
    timer = iom->addConditionTimer(
        timeout_ms,
        [winfo, fd, iom]() {
          auto t = winfo.lock();
          if (!t || t->cancelled) { // ��ʱ������Ч
            return;
          }
          t->cancelled = ETIMEDOUT;                   // ���ó�ʱ��ʶ
          iom->cancelEvent(fd, zz::IOManager::WRITE); // ����д�¼�
        },
        winfo);
  }

  // �׽��ֿ�д
  int ret = iom->addEvent(fd, zz::IOManager::WRITE); // ����д�¼���yield
  if (ret == 0) {
    zz::Fiber::GetThis()->YieldToHold();
    if (timer) {
      timer->cancel();
    }
    if (tinfo->cancelled) {
      errno = tinfo->cancelled;
      return -1;
    }
  } else {
    if (timer) {
      timer->cancel();
    }
    ZZ_LOG_ERROR(g_logger) << "connect addEvent(" << fd << ", WRITE) error";
  }

  int error = 0;
  socklen_t len = sizeof(int);
  if (-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len)) {
    return -1;
  }
  if (!error) {
    return 0;
  } else {
    errno = error;
    return -1;
  }
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  return connect_with_timeout(sockfd, addr, addrlen, zz::s_connect_timeout);
}

ssize_t read(int fd, void *buf, size_t n) {
  return do_io(fd, read_f, "read", zz::IOManager::READ, SO_RCVTIMEO, buf, n);
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
  return do_io(fd, readv_f, "readv", zz::IOManager::READ, SO_RCVTIMEO, iov,
               iovcnt);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
  return do_io(sockfd, recv_f, "recv", zz::IOManager::READ, SO_RCVTIMEO, buf,
               len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen) {
  return do_io(sockfd, recvfrom_f, "recvfrom", zz::IOManager::READ, SO_RCVTIMEO,
               buf, len, flags, src_addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
  return do_io(sockfd, recvmsg_f, "recvmsg", zz::IOManager::READ, SO_RCVTIMEO,
               msg, flags);
}

ssize_t write(int fd, const void *buf, size_t n) {
  return do_io(fd, write_f, "write", zz::IOManager::WRITE, SO_SNDTIMEO, buf, n);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
  return do_io(fd, writev_f, "writev", zz::IOManager::WRITE, SO_SNDTIMEO, iov,
               iovcnt);
}

ssize_t send(int s, const void *msg, size_t len, int flags) {
  return do_io(s, send_f, "send", zz::IOManager::WRITE, SO_SNDTIMEO, msg, len,
               flags);
}

ssize_t sendto(int s, const void *msg, size_t len, int flags,
               const struct sockaddr *to, socklen_t tolen) {
  return do_io(s, sendto_f, "sendto", zz::IOManager::WRITE, SO_SNDTIMEO, msg,
               len, flags, to, tolen);
}

ssize_t sendmsg(int s, const struct msghdr *msg, int flags) {
  return do_io(s, sendmsg_f, "sendmsg", zz::IOManager::WRITE, SO_SNDTIMEO, msg,
               flags);
}

int close(int fd) {
  if (!zz::t_hook_enable) {
    return close_f(fd);
  }

  zz::FdCtx::ptr ctx = zz::FdMgr::GetInstance()->get(fd);
  if (ctx) {
    auto iom = zz::IOManager::GetThis();
    if (iom) {
      iom->cancelAll(fd); // �ر�֮ǰ�ᴥ��һ�����еĶ�д�¼�
    }
    zz::FdMgr::GetInstance()->del(fd);
  }
  return close_f(fd);
}

int fcntl(int fd, int cmd, ...) {
  va_list va;
  va_start(va, cmd);
  switch (cmd) {
  case F_SETFL: {
    int arg = va_arg(va, int);
    va_end(va);
    zz::FdCtx::ptr ctx = zz::FdMgr::GetInstance()->get(fd);
    if (!ctx || ctx->isClose() || !ctx->isScoket()) {
      return fcntl_f(fd, cmd, arg);
    }
    ctx->setUserNonblock(arg & O_NONBLOCK);
    if (ctx->getSysNonblock()) {
      arg |= O_NONBLOCK;
    } else {
      arg &= ~O_NONBLOCK;
    }
    return fcntl_f(fd, cmd, arg);
  } break;
  case F_GETFL: {
    va_end(va);
    int arg = fcntl_f(fd, cmd);
    zz::FdCtx::ptr ctx = zz::FdMgr::GetInstance()->get(fd);
    if (!ctx || ctx->isClose() || !ctx->isScoket()) {
      return arg;
    }
    if (ctx->getUserNonblock()) {
      return arg | O_NONBLOCK;
    } else {
      return arg & ~O_NONBLOCK;
    }
  } break;

  case F_DUPFD:
  case F_DUPFD_CLOEXEC:
  case F_SETFD:
  case F_SETOWN:
  case F_SETSIG:
  case F_SETLEASE:
  case F_NOTIFY:
#ifdef F_SETPIPE_SZ
  case F_SETPIPE_SZ:
#endif
  {
    int arg = va_arg(va, int);
    va_end(va);
    return fcntl_f(fd, cmd, arg);
  } break;

  case F_GETFD:
  case F_GETOWN:
  case F_GETSIG:
  case F_GETLEASE:
#ifdef F_GETPIPE_SZ
  case F_GETPIPE_SZ:
#endif
  {
    va_end(va);
    return fcntl_f(fd, cmd);
  } break;

  case F_SETLK:
  case F_SETLKW:
  case F_GETLK: {
    struct flock *arg = va_arg(va, struct flock *);
    va_end(va);
    return fcntl_f(fd, cmd, arg);
  } break;

  case F_GETOWN_EX:
  case F_SETOWN_EX: {
    struct f_owner_exlock *arg = va_arg(va, struct f_owner_exlock *);
    va_end(va);
    return fcntl_f(fd, cmd, arg);
  } break;
  default:
    va_end(va);
    return fcntl_f(fd, cmd);
  }
}

int ioctl(int d, unsigned long int request, ...) {
  va_list va;
  va_start(va, request);
  void *arg = va_arg(va, void *);
  va_end(va);

  if (FIONBIO == request) {
    bool user_nonnlock = !!*(int *)arg;
    zz::FdCtx::ptr ctx = zz::FdMgr::GetInstance()->get(d);
    if (!ctx || ctx->isClose() || !ctx->isScoket()) {
      return ioctl_f(d, request, arg);
    }
    ctx->setUserNonblock(user_nonnlock);
  }
  return ioctl_f(d, request, arg);
}

int getsockopt(int sockfd, int level, int optname, void *optval,
               socklen_t *optlen) {
  return getsockopt_f(sockfd, level, optname, optval, optlen);
}

int setsockopt(int sockfd, int level, int optname, const void *optval,
               socklen_t optlen) {
  if (!zz::t_hook_enable) {
    return setsockopt_f(sockfd, level, optname, optval, optlen);
  }

  if (level == SOL_SOCKET) {
    if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
      zz::FdCtx::ptr ctx = zz::FdMgr::GetInstance()->get(sockfd);
      if (ctx) {
        const timeval *v = (const timeval *)optval;
        // ��¼�׽��ֳ�ʱ
        ctx->setTimeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
      }
    }
  }
  return setsockopt_f(sockfd, level, optname, optval, optlen);
}

} // namespace zz