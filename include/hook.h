/**
 * @note
 * hook技术可以使应用程序在执行系统调用之前进行一些隐藏的操作
 * 比如可以对系统提供malloc和free进行hook
 * 在真正进行内存分配和释放之前，统计内存的引用计数，以排查内存泄露问题
 * 以继承角度理解，子类继承父类的方法，但又想做一些自己的操作
 * 则在重载时候先执行自己的操作，然后调用父类的方法
    class Base {
    public:
        void Print() {
            cout << "This is Base" << endl;
        }
    };

    class Child : public Base {
    public:
        // 子类重载时先实现自己的操作，再调用父类的操作
        void Print() {
            cout << "This is Child" << endl;
            Base::Print();
        }
    };
 */

/*
1.外挂式hook(非侵入式hook)
    利用动态链接全局符号介入功能，当一个符号需要被加入全局符号表时，如果有同名符号存在，则加入符号被忽略
    自定义实现与系统调用同名函数，通过设置LD_PRELOAD环境变量优先加载自定义函数，后续同名函数被忽略
2.侵入式hook
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <sys/types.h>
#include <unistd.h>
namespace zz {

bool is_hook_enable();
void set_hook_enable(bool flag);

// extern "C"防止对符号名添加修饰，确保C++正确链接
extern "C" {
// sleep
using sleep_fun = unsigned int (*)(unsigned int seconds);
extern sleep_fun sleep_f;

using usleep_fun = int (*)(useconds_t usec);
extern usleep_fun usleep_f;

using nanosleep_fun = int (*)(const struct timespec *req, struct timespec *rem);
extern nanosleep_fun nanosleep_f;

// socket
using socket_fun = int (*)(int domain, int type, int protocol);
extern socket_fun socket_f;

using connect_fun = int (*)(int sockfd, const struct sockaddr *addr,
                            socklen_t addrlen);
extern connect_fun connect_f;

using accept_fun = int (*)(int s, struct sockaddr *addr, socklen_t *addrlen);
extern accept_fun accept_f;

// read
using read_fun = ssize_t (*)(int fd, void *buf, size_t n);
extern read_fun read_f;

using readv_fun = ssize_t (*)(int fd, const struct iovec *iov, int iovcnt);
extern readv_fun readv_f;

using recv_fun = ssize_t (*)(int sockfd, void *buf, size_t len, int flags);
extern recv_fun recv_f;

using recvfrom_fun = ssize_t (*)(int sockfd, void *buf, size_t len, int flags,
                                 struct sockaddr *src_addr, socklen_t *addrlen);
extern recvfrom_fun recvfrom_f;

using recvmsg_fun = ssize_t (*)(int sockfd, struct msghdr *msg, int flags);
extern recvmsg_fun recvmsg_f;

// write
using write_fun = ssize_t (*)(int fd, const void *buf, size_t n);
extern write_fun write_f;

using writev_fun = ssize_t (*)(int fd, const struct iovec *iov, int iovcnt);
extern writev_fun writev_f;

using send_fun = ssize_t (*)(int s, const void *msg, size_t len, int flags);
extern send_fun send_f;

using sendto_fun = ssize_t (*)(int s, const void *msg, size_t len, int flags,
                               const struct sockaddr *to, socklen_t tolen);
extern sendto_fun sendto_f;

using sendmsg_fun = ssize_t (*)(int s, const struct msghdr *msg, int flags);
extern sendmsg_fun sendmsg_f;

using close_fun = int (*)(int fd);
extern close_fun close_f;

// 辅助函数
using fcntl_fun = int (*)(int fd, int cmd, ...);
extern fcntl_fun fcntl_f;

using ioctl_fun = int (*)(int d, unsigned long int request, ...);
extern ioctl_fun ioctl_f;

using getsockopt_fun = int (*)(int sockfd, int level, int optname, void *optval,
                               socklen_t *optlen);
extern getsockopt_fun getsockopt_f;

using setsockopt_fun = int (*)(int sockfd, int level, int optname,
                               const void *optval, socklen_t optlen);
extern setsockopt_fun setsockopt_f;

extern int connect_with_timeout(int fd, const struct sockaddr *addr,
                                socklen_t addrlen, uint64_t timeout_ms);
}

} // namespace zz