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
   * @brief ����Socket(�����ַ����)
   * @param[in] address ��ַ
   */
  static Socket::ptr CreateTCP(zz::Address::ptr address);
  static Socket::ptr CreateUDP(zz::Address::ptr address);

  /**
   * @brief ����IPv4�� Socket
   */
  static Socket::ptr CreateTCPSocket();
  static Socket::ptr CreateUDPSocket();

  /**
   * @brief ����IPv6��TCP Socket
   */
  static Socket::ptr CreateTCPSocket6();
  static Socket::ptr CreateUDPSocket6();

  /**
   * @brief ����Unix�� Socket
   */
  static Socket::ptr CreateUnixTCPSocket();
  static Socket::ptr CreateUnixUDPSocket();

  Socket(int family, int type, int protocol = 0);
  virtual ~Socket();

  /**
   * @brief ���ͳ�ʱʱ��(����)
   */
  int64_t getSendTimeout();
  void setSendTimeout(int64_t v);

  /**
   * @brief ���ճ�ʱʱ��(����)
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
   * @brief �󶨵�ַ
   * @param[in] addr ��ַ
   * @return �Ƿ�󶨳ɹ�
   */
  virtual bool bind(const Address::ptr addr);

  /**
   * @brief ����socket
   * @param[in] backlog δ������Ӷ��е���󳤶�
   * @result ���ؼ����Ƿ�ɹ�
   * @pre ������ bind �ɹ�
   */
  virtual bool listen(int backlog = SOMAXCONN);

  /**
   * @brief ����connect����
   * @return �ɹ����������ӵ�socket,ʧ�ܷ���nullptr
   * @pre Socket���� bind , listen  �ɹ�
   */
  virtual Socket::ptr accept();

  virtual bool connect(const Address::ptr addr, uint64_t timeout_ms = -1);
  virtual bool reconnect(uint64_t timeout_ms = -1);
  virtual bool close();

  /**
   * @brief ��������
   * @param[in] buffer ���������ݵ��ڴ�
   * @param[in] len ���������ݵĳ���
   * @param[in] flags ��־��
   * @return
   *      @retval >0 ���ͳɹ���Ӧ��С������
   *      @retval =0 socket���ر�
   *      @retval <0 socket����
   */
  virtual int send(const void *buffer, size_t len, int flags = 0);
  virtual int send(const iovec *buffer, size_t len, int flags = 0);

  /**
   * @brief ��������
   * @param[in] buffer ���������ݵ��ڴ�
   * @param[in] len ���������ݵĳ���
   * @param[in] to ���͵�Ŀ���ַ
   * @param[in] flags ��־��
   * @return
   *      @retval >0 ���ͳɹ���Ӧ��С������
   *      @retval =0 socket���ر�
   *      @retval <0 socket����
   */
  virtual int sendTo(const void *buffer, size_t len, const Address::ptr to,
                     int flags = 0);
  virtual int sendTo(const iovec *buffer, size_t len, const Address::ptr to,
                     int flags);

  /**
   * @brief ��������
   * @param[out] buffer �������ݵ��ڴ�
   * @param[in] len �������ݵ��ڴ��С
   * @param[in] flags ��־��
   * @return
   *      @retval >0 ���յ���Ӧ��С������
   *      @retval =0 socket���ر�
   *      @retval <0 socket����
   */
  virtual int recv(void *buffer, size_t len, int flags = 0);
  virtual int recv(iovec *buffer, size_t len, int flags = 0);

  /**
   * @brief ��������
   * @param[out] buffer �������ݵ��ڴ�
   * @param[in] len �������ݵ��ڴ��С
   * @param[out] from ���Ͷ˵�ַ
   * @param[in] flags ��־��
   * @return
   *      @retval >0 ���յ���Ӧ��С������
   *      @retval =0 socket���ر�
   *      @retval <0 socket����
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
   * @brief �Ƿ���Ч(m_sock != -1)
   */
  bool isValid() const;

  /**
   * @brief ����Socket����
   */
  int getError();

  /**
   * @brief �����Ϣ������
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
  int m_family;   // Э���
  int m_type;     // ����
  int m_protocol; // Э��
  bool m_isConnected;
  Address::ptr m_localAddress;
  Address::ptr m_remoteAddress;
};

/**
 * @brief ��ʽ���socket
 * @param[in, out] os �����
 * @param[in] sock Socket��
 */
std::ostream &operator<<(std::ostream &os, const Socket &sock);

} // namespace zz