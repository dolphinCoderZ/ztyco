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

// hook功能以线程为单位
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

// 用来保存原始API的变量
extern "C" {
#define XX(name) name##_fun name##_f = nullptr;
HOOK_FUN(XX);
#undef XX
}

// 初始化：保存原始API的入口地址，hook只是对原始API进行改造
void hook_init() {
  static bool is_inited = false;
  if (is_inited) {
    return;
  }
  // 找到原始API，name##_f保存的就是原始API
#define XX(name) name##_f = (name##_fun)dlsym(RTLD_NEXT, #name);
  HOOK_FUN(XX);
#undef XX
}
/**
 * ##作用： 连接后缀
 * #作用： 将标识符转换为字符串字面值
 * dlsym 作用： 查找标准库中函数的符号地址
 */
/*
头文件中extern的外部变量，保存了标准库API
sleep_f = (sleep_fun)dlsym(RTLD_NEXT, "sleep");
...
usleep_f = (usleep_fun)dlsym(RTLD_NEXT, "usleep");
setsocketopt_f = (setsocketopt_fun)dlsym(RTLD_NEXT, "setsocketopt");
*/

static uint64_t s_connect_timeout = -1;
struct _HookIniter {
  _HookIniter() {
    // 在构造静态对象时就调用hook_init，main函数运行之前获取原始API
    hook_init();

    s_connect_timeout = g_tcp_connet_timeout->getValue();
    g_tcp_connet_timeout->addListener([](const int &oldval, const int &newVal) {
      ZZ_LOG_INFO(g_logger)
          << "tcp connect timeout changed from " << oldval << " to " << newVal;
      s_connect_timeout = newVal;
    });
  }
};
// 初始化静态对象，在mian之间完成初始化，.data段就保存了各个原始API的地址
static _HookIniter s_hook_initer;

bool is_hook_enable() { return t_hook_enable; }
void set_hook_enable(bool flag) { t_hook_enable = flag; }

// 超时参数
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
    n = fun(fd, std::forward<Args>(args)...); // 被中断就继续
  }
  if (n == -1 && errno == EAGAIN) {
    zz::IOManager *iom = zz::IOManager::GetThis();
    zz::Timer::ptr timer;
    std::weak_ptr<timer_info> winfo(tinfo);

    // 添加条件定时器
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

    // 添加事件
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
sleep延时系列接口，包括sleep/usleep/nanosleep。对于这些接口的hook，只需要给IO协程调度器注册一个定时事件，在定时事件触发后再继续执行当前协程即可。当前协程在注册完定时事件后即可yield让出执行权。

socket系列接口，包括read/write/recv/send...等，connect及accept也可以归到这类接口中。这类接口的hook首先需要判断操作的fd是否是sockfd，以及用户是否显式地对该fd设置过非阻塞模式，如果不是sockfd或是用户显式设置过非阻塞模式，那么就不需要hook，直接调用操作系统的IO接口即可。如果需要hook，那么首先在IO协程调度器上注册对应的读写事件，等事件发生后再继续执行当前协程。当前协程在注册完IO事件即可yield让出执行权。

socket/fcntl/ioctl/close等接口，这类接口主要处理的是边缘情况，比如分配fd上下文，处理超时及用户显式设置非阻塞问题。
*/

// 1.添加定时器 2.yield让出执行权
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

// 创建套接字，添加到FdManager进行管理
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

// 带超时的connect，给对应fd注册定时器
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
             errno == EINPROGRESS) { // 由于套接字是非阻塞,会返回EINPROGRESS错误
    return n;
  }

  zz::IOManager *iom = zz::IOManager::GetThis();
  zz::Timer::ptr timer;
  std::shared_ptr<timer_info> tinfo(new timer_info);
  std::weak_ptr<timer_info> winfo(tinfo);

  // 连接超时，设置条件定时器
  if (timeout_ms != (uint64_t)-1) {
    // 添加条件定时器
    timer = iom->addConditionTimer(
        timeout_ms,
        [winfo, fd, iom]() {
          auto t = winfo.lock();
          if (!t || t->cancelled) { // 超时参数无效
            return;
          }
          t->cancelled = ETIMEDOUT;                   // 设置超时标识
          iom->cancelEvent(fd, zz::IOManager::WRITE); // 触发写事件
        },
        winfo);
  }

  // 套接字可写
  int ret = iom->addEvent(fd, zz::IOManager::WRITE); // 添加写事件并yield
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
      iom->cancelAll(fd); // 关闭之前会触发一次所有的读写事件
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
        // 记录套接字超时
        ctx->setTimeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
      }
    }
  }
  return setsockopt_f(sockfd, level, optname, optval, optlen);
}

} // namespace zz