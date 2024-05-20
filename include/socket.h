#pragma once

#include "address.h"
#include "noncopyable.h"
#include <bits/types/struct_iovec.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <sys/socket.h>

namespace zz {
class Socket : public std::enable_shared_from_this<Socket>, Noncopyable {

public:
  using ptr = std::shared_ptr<Socket>;
  using weak_ptr = std::weak_ptr<Socket>;

  enum Type { TCP = SOCK_STREAM, UDP = SOCK_DGRAM };

  enum Family {
    IPv4 = AF_INET,
    IPv6 = AF_INET6,
    UNIX = AF_UNIX,
  };

  /**
   * @brief 创建Socket(满足地址类型)
   * @param[in] address 地址
   */
  static Socket::ptr CreateTCP(zz::Address::ptr address);
  static Socket::ptr CreateUDP(zz::Address::ptr address);

  /**
   * @brief 创建IPv4的 Socket
   */
  static Socket::ptr CreateTCPSocket();
  static Socket::ptr CreateUDPSocket();

  /**
   * @brief 创建IPv6的TCP Socket
   */
  static Socket::ptr CreateTCPSocket6();
  static Socket::ptr CreateUDPSocket6();

  /**
   * @brief 创建Unix的 Socket
   */
  static Socket::ptr CreateUnixTCPSocket();
  static Socket::ptr CreateUnixUDPSocket();

  Socket(int family, int type, int protocol = 0);
  virtual ~Socket();

  /**
   * @brief 发送超时时间(毫秒)
   */
  int64_t getSendTimeout();
  void setSendTimeout(int64_t v);

  /**
   * @brief 接收超时时间(毫秒)
   */
  int64_t getRecvTimeout();
  void setRecvTimeout(int64_t v);

  bool getOption(int level, int option, void *result, socklen_t *len);
  template <class T> bool getOption(int level, int option, T &result) {
    socklen_t len = sizeof(T);
    return getOption(level, option, &result, &len);
  }

  bool setOption(int level, int option, const void *result, socklen_t len);
  template <class T> bool setOption(int level, int option, const T &value) {
    return setOption(level, option, &value, sizeof(T));
  }

  /**
   * @brief 绑定地址
   * @param[in] addr 地址
   * @return 是否绑定成功
   */
  virtual bool bind(const Address::ptr addr);

  /**
   * @brief 监听socket
   * @param[in] backlog 未完成连接队列的最大长度
   * @result 返回监听是否成功
   * @pre 必须先 bind 成功
   */
  virtual bool listen(int backlog = SOMAXCONN);

  /**
   * @brief 接收connect链接
   * @return 成功返回新连接的socket,失败返回nullptr
   * @pre Socket必须 bind , listen  成功
   */
  virtual Socket::ptr accept();

  virtual bool connect(const Address::ptr addr, uint64_t timeout_ms = -1);
  virtual bool reconnect(uint64_t timeout_ms = -1);
  virtual bool close();

  /**
   * @brief 发送数据
   * @param[in] buffer 待发送数据的内存
   * @param[in] len 待发送数据的长度
   * @param[in] flags 标志字
   * @return
   *      @retval >0 发送成功对应大小的数据
   *      @retval =0 socket被关闭
   *      @retval <0 socket出错
   */
  virtual int send(const void *buffer, size_t len, int flags = 0);
  virtual int send(const iovec *buffer, size_t len, int flags = 0);

  /**
   * @brief 发送数据
   * @param[in] buffer 待发送数据的内存
   * @param[in] len 待发送数据的长度
   * @param[in] to 发送的目标地址
   * @param[in] flags 标志字
   * @return
   *      @retval >0 发送成功对应大小的数据
   *      @retval =0 socket被关闭
   *      @retval <0 socket出错
   */
  virtual int sendTo(const void *buffer, size_t len, const Address::ptr to,
                     int flags = 0);
  virtual int sendTo(const iovec *buffer, size_t len, const Address::ptr to,
                     int flags);

  /**
   * @brief 接受数据
   * @param[out] buffer 接收数据的内存
   * @param[in] len 接收数据的内存大小
   * @param[in] flags 标志字
   * @return
   *      @retval >0 接收到对应大小的数据
   *      @retval =0 socket被关闭
   *      @retval <0 socket出错
   */
  virtual int recv(void *buffer, size_t len, int flags = 0);
  virtual int recv(iovec *buffer, size_t len, int flags = 0);

  /**
   * @brief 接受数据
   * @param[out] buffer 接收数据的内存
   * @param[in] len 接收数据的内存大小
   * @param[out] from 发送端地址
   * @param[in] flags 标志字
   * @return
   *      @retval >0 接收到对应大小的数据
   *      @retval =0 socket被关闭
   *      @retval <0 socket出错
   */
  virtual int recvFrom(void *buffer, size_t len, Address::ptr from,
                       int flags = 0);
  virtual int recvFrom(iovec *buffer, size_t len, Address::ptr from,
                       int flags = 0);

  Address::ptr getRemoteAddress();
  Address::ptr getLocalAddress();
  int getFamily() const { return m_family; }
  int getType() const { return m_type; }
  int getProtocol() const { return m_protocol; }
  bool isConnected() const { return m_isConnected; }

  /**
   * @brief 是否有效(m_sock != -1)
   */
  bool isValid() const;

  /**
   * @brief 返回Socket错误
   */
  int getError();

  /**
   * @brief 输出信息到流中
   */
  virtual std::ostream &dump(std::ostream &os) const;
  virtual std::string toString() const;

  int getSocket() const { return m_sock; }

  bool cancelRead();
  bool cancelWrite();
  bool cancelAccept();
  bool cancelAll();

protected:
  void initSock();
  void newSock();

  virtual bool init(int sock);

protected:
  int m_sock;
  int m_family;   // 协议簇
  int m_type;     // 类型
  int m_protocol; // 协议
  bool m_isConnected;
  Address::ptr m_localAddress;
  Address::ptr m_remoteAddress;
};

/**
 * @brief 流式输出socket
 * @param[in, out] os 输出流
 * @param[in] sock Socket类
 */
std::ostream &operator<<(std::ostream &os, const Socket &sock);

} // namespace zz