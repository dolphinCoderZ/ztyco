#include "socket.h"
#include "address.h"
#include "fdmanager.h"
#include "fiber.h"
#include "hook.h"
#include "iomanager.h"
#include "log.h"
#include "macro.h"
#include "thread.h"
#include "util.h"
#include <asm-generic/socket.h>
#include <bits/types/struct_timeval.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <ostream>
#include <sstream>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace zz {

static zz::Logger::ptr g_logger = ZZ_LOG_NAME("system");

Socket::ptr Socket::CreateTCP(zz::Address::ptr address) {
  Socket::ptr sock(new Socket(address->getFamily(), TCP, 0));
  return sock;
}

Socket::ptr Socket::CreateUDP(zz::Address::ptr address) {
  Socket::ptr sock(new Socket(address->getFamily(), UDP, 0));
  return sock;
}

Socket::ptr Socket::CreateTCPSocket() {
  Socket::ptr sock(new Socket(IPv4, TCP, 0));
  return sock;
}

Socket::ptr Socket::CreateUDPSocket() {
  Socket::ptr sock(new Socket(IPv4, UDP, 0));
  sock->newSock();
  sock->m_isConnected = true;
  return sock;
}

Socket::ptr Socket::CreateTCPSocket6() {
  Socket::ptr sock(new Socket(IPv6, TCP, 0));
  return sock;
}

Socket::ptr Socket::CreateUDPSocket6() {
  Socket::ptr sock(new Socket(IPv6, UDP, 0));
  sock->newSock();
  sock->m_isConnected = true;
  return sock;
}

Socket::ptr Socket::CreateUnixTCPSocket() {
  Socket::ptr sock(new Socket(UNIX, TCP, 0));
  return sock;
}

Socket::ptr Socket::CreateUnixUDPSocket() {
  Socket::ptr sock(new Socket(UNIX, UDP, 0));
  return sock;
}

Socket::Socket(int family, int type, int protocol)
    : m_sock(-1), m_family(family), m_type(type), m_protocol(protocol),
      m_isConnected(false) {}

Socket::~Socket() { close(); }

int64_t Socket::getSendTimeout() {
  FdCtx::ptr ctx = FdMgr::GetInstance()->get(m_sock);
  if (ctx) {
    return ctx->getTimeout(SO_SNDTIMEO);
  }
  return -1;
}

void Socket::setSendTimeout(int64_t v) {
  struct timeval tv {
    int(v / 1000), int(v % 1000 * 1000)
  };
  setOption(SOL_SOCKET, SO_SNDTIMEO, tv);
}

int64_t Socket::getRecvTimeout() {
  FdCtx::ptr ctx = FdMgr::GetInstance()->get(m_sock);
  if (ctx) {
    return ctx->getTimeout(SO_RCVTIMEO);
  }
  return -1;
}

void Socket::setRecvTimeout(int64_t v) {
  struct timeval tv {
    int(v / 1000), int(v % 1000 * 1000)
  };
  setOption(SOL_SOCKET, SO_RCVTIMEO, tv);
}

bool Socket::getOption(int level, int option, void *result, socklen_t *len) {
  if (getsockopt(m_sock, level, option, result, (socklen_t *)len)) {
    ZZ_LOG_DEBUG(g_logger) << "getOption sock=" << m_sock << " level=" << level
                           << " option=" << option << " errno=" << errno
                           << " errstr=" << strerror(errno);
    return false;
  }
  return true;
}

bool Socket::setOption(int level, int option, const void *result,
                       socklen_t len) {
  if (setsockopt(m_sock, level, option, result, (socklen_t)len)) {
    ZZ_LOG_DEBUG(g_logger) << "setOption sock=" << m_sock << " level=" << level
                           << " option=" << option << " errno=" << errno
                           << " errstr=" << strerror(errno);
    return false;
  }
  return true;
}

Socket::ptr Socket::accept() {
  Socket::ptr sock(new Socket(m_family, m_type, m_protocol));
  int newsock = ::accept(m_sock, nullptr, nullptr);
  if (newsock == -1) {
    ZZ_LOG_ERROR(g_logger) << "accept(" << m_sock << ") errno=" << errno
                           << " errstr=" << strerror(errno);
    return nullptr;
  }
  if (sock->init(newsock)) {
    return sock;
  }
  return nullptr;
}

bool Socket::init(int sock) {
  FdCtx::ptr ctx = FdMgr::GetInstance()->get(sock);
  if (ctx && ctx->isScoket() && !ctx->isClose()) {
    m_sock = sock;
    m_isConnected = true;
    initSock();
    getLocalAddress();
    getRemoteAddress();
    return true;
  }
  return false;
}

bool Socket::bind(const Address::ptr addr) {
  m_localAddress = addr;
  if (!isValid()) {
    newSock();
    if (ZZ_UNLIKELY(!isValid())) {
      return false;
    }
  }

  if (ZZ_UNLIKELY(addr->getFamily() != m_family)) {
    ZZ_LOG_ERROR(g_logger) << "bind sock.family(" << m_family
                           << ") addr.family(" << addr->getFamily()
                           << ") not equal, addr=" << addr->toString();
    return false;
  }

  UnixAddress::ptr uaddr = std::dynamic_pointer_cast<UnixAddress>(addr);
  if (uaddr) {
    Socket::ptr sock = Socket::CreateUnixTCPSocket();
    if (sock->connect(uaddr)) {
      return false;
    } else {
      zz::FSUtil::Unlink(uaddr->getPath(), true);
    }
  }

  if (::bind(m_sock, addr->getAddr(), addr->getAddrLen())) {
    ZZ_LOG_ERROR(g_logger) << "bind error errrno=" << errno
                           << " errstr=" << strerror(errno);
    return false;
  }
  getLocalAddress();
  return true;
}

bool Socket::reconnect(uint64_t timeout_ms) {
  if (!m_remoteAddress) {
    ZZ_LOG_ERROR(g_logger) << "reconnect m_remoteAddress is null";
    return false;
  }
  m_localAddress.reset();
  return connect(m_remoteAddress, timeout_ms);
}

bool Socket::connect(const Address::ptr addr, uint64_t timeout_ms) {
  m_remoteAddress = addr;
  if (!isValid()) {
    newSock();
    if (ZZ_UNLIKELY(!isValid())) {
      return false;
    }
  }

  if (ZZ_UNLIKELY(addr->getFamily() != m_family)) {
    ZZ_LOG_ERROR(g_logger) << "connect sock.family(" << m_family
                           << ") addr.family(" << addr->getFamily()
                           << ") not equal, addr=" << addr->toString();
    return false;
  }

  if (timeout_ms == (uint64_t)-1) {
    if (::connect(m_sock, addr->getAddr(), addr->getAddrLen())) {
      ZZ_LOG_ERROR(g_logger)
          << "sock=" << m_sock << " connect(" << addr->toString()
          << ") error errno=" << errno << " errstr=" << strerror(errno);
      return false;
    }
  } else {
    if (zz::connect_with_timeout(m_sock, addr->getAddr(), addr->getAddrLen(),
                                 timeout_ms)) {
      ZZ_LOG_ERROR(g_logger)
          << "sock=" << m_sock << " connect(" << addr->toString()
          << ") timeout=" << timeout_ms << " error errno=" << errno
          << " errstr=" << strerror(errno);
      close();
      return false;
    }
  }
  m_isConnected = true;
  getRemoteAddress();
  getLocalAddress();
  return true;
}

bool Socket::listen(int backlog) {
  if (!isValid()) {
    ZZ_LOG_ERROR(g_logger) << "listen error sock=-1";
    return false;
  }

  if (::listen(m_sock, backlog)) {
    ZZ_LOG_ERROR(g_logger) << "listen error errno=" << errno
                           << " errstr=" << strerror(errno);
    return false;
  }
  return true;
}

bool Socket::close() {
  if (!m_isConnected && m_sock == -1) {
    return true;
  }

  m_isConnected = false;
  if (m_sock != -1) {
    ::close(m_sock);
    m_sock = -1;
    return true;
  }

  return false;
}

int Socket::send(const void *buffer, size_t len, int flags) {
  if (isConnected()) {
    return ::send(m_sock, buffer, len, flags);
  }
  return -1;
}

int Socket::send(const iovec *buffer, size_t len, int flags) {
  if (isConnected()) {
    msghdr msg;
    bzero(&msg, sizeof(msg));
    msg.msg_iov = (iovec *)buffer;
    msg.msg_iovlen = len;
    return ::sendmsg(m_sock, &msg, flags);
  }
  return -1;
}

int Socket::sendTo(const void *buffer, size_t len, const Address::ptr to,
                   int flags) {
  if (isConnected()) {
    return ::sendto(m_sock, buffer, len, flags, to->getAddr(),
                    to->getAddrLen());
  }
  return -1;
}

int Socket::sendTo(const iovec *buffer, size_t len, const Address::ptr to,
                   int flags) {
  if (isConnected()) {
    msghdr msg;
    bzero(&msg, sizeof(msg));
    msg.msg_iov = (iovec *)buffer;
    msg.msg_iovlen = len;
    msg.msg_name = to->getAddr();
    msg.msg_namelen = to->getAddrLen();
    return ::sendmsg(m_sock, &msg, flags);
  }
  return -1;
}

int Socket::recv(void *buffer, size_t len, int flags) {
  if (isConnected()) {
    return ::recv(m_sock, buffer, len, flags);
  }
  return -1;
}

int Socket::recv(iovec *buffer, size_t len, int flags) {
  if (isConnected()) {
    msghdr msg;
    bzero(&msg, sizeof(msg));
    msg.msg_iov = (iovec *)buffer;
    msg.msg_iovlen = len;
    return ::recvmsg(m_sock, &msg, flags);
  }
  return -1;
}

int Socket::recvFrom(void *buffer, size_t len, zz::Address::ptr from,
                     int flags) {
  if (isConnected()) {
    socklen_t len = from->getAddrLen();
    return ::recvfrom(m_sock, buffer, len, flags, from->getAddr(), &len);
  }
  return -1;
}

int Socket::recvFrom(iovec *buffer, size_t len, zz::Address::ptr from,
                     int flags) {
  if (isConnected()) {
    msghdr msg;
    bzero(&msg, sizeof(msg));
    msg.msg_iov = (iovec *)buffer;
    msg.msg_iovlen = len;
    msg.msg_name = from->getAddr();
    msg.msg_namelen = from->getAddrLen();
    return ::recvmsg(m_sock, &msg, flags);
  }
  return -1;
}

Address::ptr Socket::getRemoteAddress() {
  if (m_remoteAddress) {
    return m_remoteAddress;
  }

  Address::ptr result;
  switch (m_family) {
  case AF_INET:
    result.reset(new IPv4Address());
    break;
  case AF_INET6:
    result.reset(new IPv6Address());
    break;
  case AF_UNIX:
    result.reset(new UnixAddress());
    break;
  default:
    result.reset(new UnknownAddress(m_family));
    break;
  }

  socklen_t addrlen = result->getAddrLen();
  if (getpeername(m_sock, result->getAddr(), &addrlen)) {
    ZZ_LOG_ERROR(g_logger) << "getpeername error sock=" << m_sock
                           << " errno=" << errno
                           << " errstr=" << strerror(errno);
    return Address::ptr(new UnknownAddress(m_family));
  }

  if (m_family == AF_UNIX) {
    UnixAddress::ptr addr = std::dynamic_pointer_cast<UnixAddress>(result);
    addr->setAddrLen(addrlen);
  }
  m_remoteAddress = result;
  return m_remoteAddress;
}

Address::ptr Socket::getLocalAddress() {
  if (m_localAddress) {
    return m_localAddress;
  }

  Address::ptr res;
  switch (m_family) {
  case AF_INET:
    res.reset(new IPv4Address());
    break;
  case AF_INET6:
    res.reset(new IPv6Address());
    break;
  case AF_UNIX:
    res.reset(new UnixAddress());
    break;
  default:
    res.reset(new UnknownAddress(m_family));
    break;
  }

  socklen_t addrlen = res->getAddrLen();
  if (getsockname(m_sock, res->getAddr(), &addrlen)) {
    ZZ_LOG_ERROR(g_logger) << "getsockname error sock=" << m_sock
                           << " errno=" << errno
                           << " errstr=" << strerror(errno);
    return Address::ptr(new UnknownAddress(m_family));
  }

  if (m_family == AF_UNIX) {
    UnixAddress::ptr addr = std::dynamic_pointer_cast<UnixAddress>(res);
    addr->setAddrLen(addrlen);
  }
  m_localAddress = res;
  return m_localAddress;
}

bool Socket::isValid() const { return m_sock != -1; }

int Socket::getError() {
  int err = 0;
  socklen_t len = sizeof(err);

  if (getOption(SOL_SOCKET, SO_ERROR, &err, &len)) {
    err = errno;
  }
  return err;
}

std::ostream &Socket::dump(std::ostream &os) const {
  os << "[Socket sock=" << m_sock << " is_connected=" << m_isConnected
     << " family=" << m_family << " type=" << m_type
     << " protocol=" << m_protocol;
  if (m_localAddress) {
    os << " local_address=" << m_localAddress->toString();
  }
  if (m_remoteAddress) {
    os << " remote_address=" << m_remoteAddress->toString();
  }
  os << "]";
  return os;
}

std::string Socket::toString() const {
  std::stringstream ss;
  dump(ss);
  return ss.str();
}

bool Socket::cancelRead() {
  return IOManager::GetThis()->cancelEvent(m_sock, zz::IOManager::READ);
}

bool Socket::cancelWrite() {
  return IOManager::GetThis()->cancelEvent(m_sock, zz::IOManager::WRITE);
}

bool Socket::cancelAccept() { return cancelRead(); }

bool Socket::cancelAll() { return IOManager::GetThis()->cancelAll(m_sock); }

void Socket::initSock() {
  int val = 1;
  setOption(SOL_SOCKET, SO_REUSEADDR, val);
  if (m_type == SOCK_STREAM) {
    setOption(IPPROTO_TCP, TCP_NODELAY, val);
  }
}

void Socket::newSock() {
  m_sock = socket(m_family, m_type, m_protocol);
  if (ZZ_LIKELY(m_sock != -1)) {
    initSock();
  } else {
    ZZ_LOG_ERROR(g_logger) << "socket(" << m_family << ", " << m_type << ", "
                           << m_protocol << ") errno=" << errno
                           << " errstr=" << strerror(errno);
  }
}

std::ostream &operator<<(std::ostream &os, const Socket &sock) {
  return sock.dump(os);
}

} // namespace zz