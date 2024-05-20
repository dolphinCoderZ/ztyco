#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <netinet/in.h>
#include <ostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace zz {

class IPAddress;

// ��Ӧsockaddr����
class Address {
public:
  using ptr = std::shared_ptr<Address>;

  /**
   * @brief ͨ��sockaddrָ�봴��Address
   * @param[in] addr sockaddrָ��
   * @param[in] addrlen sockaddr�ĳ���
   * @return ���غ�sockaddr��ƥ���Address,ʧ�ܷ���nullptr
   */
  static Address::ptr Create(const sockaddr *addr, socklen_t addrlen);

  /**
   * @brief ͨ��host��ַ���ض�Ӧ����������Address
   * @param[out] result ��������������Address
   * @param[in] host ����,����������.����: www.sylar.top[:80] (������Ϊ��ѡ����)
   * @param[in] family Э����(AF_INT, AF_INT6, AF_UNIX)
   * @param[in] type socketl����SOCK_STREAM��SOCK_DGRAM ��
   * @param[in] protocol Э��,IPPROTO_TCP��IPPROTO_UDP ��
   */
  static bool Lookup(std::vector<Address::ptr> &result, const std::string &host,
                     int family = AF_INET, int type = 0, int protocol = 0);

  /**
   * @brief ͨ��host��ַ���ض�Ӧ����������Address
   * @param[out] result ��������������Address
   * @param[in] host ����,����������.����: www.sylar.top[:80] (������Ϊ��ѡ����)
   * @param[in] family Э����(AF_INT, AF_INT6, AF_UNIX)
   * @param[in] type socketl����SOCK_STREAM��SOCK_DGRAM ��
   * @param[in] protocol Э��,IPPROTO_TCP��IPPROTO_UDP ��
   */
  static Address::ptr LookupAny(const std::string &host, int family = AF_INET,
                                int type = 0, int protocol = 0);

  /**
   * @brief ͨ��host��ַ���ض�Ӧ����������IPAddress
   * @param[in] host ����,����������.����: www.sylar.top[:80] (������Ϊ��ѡ����)
   * @param[in] family Э����(AF_INT, AF_INT6, AF_UNIX)
   * @param[in] type socketl����SOCK_STREAM��SOCK_DGRAM ��
   * @param[in] protocol Э��,IPPROTO_TCP��IPPROTO_UDP ��
   * @return ������������������IPAddress,ʧ�ܷ���nullptr
   */
  static std::shared_ptr<IPAddress> LookupAnyIPAddress(const std::string &host,
                                                       int family = AF_INET,
                                                       int type = 0,
                                                       int protocol = 0);

  /**
   * @brief ���ر�������������<������, ��ַ, ��������λ��>
   * @param[out] result ���汾�����е�ַ
   * @param[in] family Э����(AF_INT, AF_INT6, AF_UNIX)
   * @return �Ƿ��ȡ�ɹ�
   */
  static bool GetInterfaceAddresses(
      std::multimap<std::string, std::pair<Address::ptr, uint32_t>> &result,
      int family = AF_INET);

  /**
   * @brief ��ȡָ�������ĵ�ַ����������λ��
   * @param[out] result ����ָ���������е�ַ
   * @param[in] iface ��������
   * @param[in] family Э����(AF_INT, AF_INT6, AF_UNIX)
   * @return �Ƿ��ȡ�ɹ�
   */
  static bool
  GetInterfaceAddresses(std::vector<std::pair<Address::ptr, uint32_t>> &result,
                        const std::string &iface, int family = AF_INET);

  virtual ~Address() {}

  int getFamily() const;
  virtual const sockaddr *getAddr() const = 0;
  virtual sockaddr *getAddr() = 0;
  virtual socklen_t getAddrLen() const = 0;

  virtual std::ostream &insert(std::ostream &os) const = 0;
  /**
   * @brief ���ؿɶ����ַ���
   */
  std::string toString() const;

  bool operator<(const Address &rhs) const;
  bool operator==(const Address &rhs) const;
  bool operator!=(const Address &rhs) const;
};

class IPAddress : public Address {
public:
  using ptr = std::shared_ptr<IPAddress>;
  /**
   * @brief ͨ������,IP,������������IPAddress
   * @param[in] address ����,IP,����������.����: www.sylar.top
   * @param[in] port �˿ں�
   * @return ���óɹ�����IPAddress,ʧ�ܷ���nullptr
   */
  static IPAddress::ptr Create(const char *address, uint16_t port = 0);

  /**
   * @brief ��ȡ�õ�ַ�Ĺ㲥��ַ
   * @param[in] prefix_len ��������λ��
   * @return ���óɹ�����IPAddress,ʧ�ܷ���nullptr
   */
  virtual IPAddress::ptr broadcastAddress(uint32_t prefix_len) = 0;

  /**
   * @brief ��ȡ�õ�ַ������
   * @param[in] prefix_len ��������λ��
   * @return ���óɹ�����IPAddress,ʧ�ܷ���nullptr
   */
  virtual IPAddress::ptr networkAddress(uint32_t prefix_len) = 0;

  /**
   * @brief ��ȡ���������ַ
   * @param[in] prefix_len ��������λ��
   * @return ���óɹ�����IPAddress,ʧ�ܷ���nullptr
   */
  virtual IPAddress::ptr subnetMask(uint32_t prefix_len) = 0;

  virtual uint32_t getPort() const = 0;
  virtual void setPort(uint16_t port) = 0;
};

// ��Ӧsockaddr_in����
class IPv4Address : public IPAddress {
public:
  using ptr = std::shared_ptr<IPv4Address>;

  /**
   * @brief ʹ�õ��ʮ���Ƶ�ַ����IPv4Address
   * @param[in] address ���ʮ���Ƶ�ַ,��:192.168.1.1
   * @param[in] port �˿ں�
   * @return ����IPv4Address,ʧ�ܷ���nullptr
   */
  static IPv4Address::ptr Create(const char *address, uint16_t port = 0);

  /**
   * @brief ͨ��sockaddr_in����IPv4Address
   * @param[in] address sockaddr_in�ṹ��
   */
  IPv4Address(const sockaddr_in &address);

  /**
   * @brief ͨ�������Ƶ�ַ����IPv4Address
   * @param[in] address �����Ƶ�ַaddress
   * @param[in] port �˿ں�
   */
  IPv4Address(uint32_t address = INADDR_ANY, uint16_t port = 0);

  const sockaddr *getAddr() const override;
  sockaddr *getAddr() override;
  socklen_t getAddrLen() const override;
  std::ostream &insert(std::ostream &os) const override;

  IPAddress::ptr broadcastAddress(uint32_t prefix_len) override;
  IPAddress::ptr networkAddress(uint32_t prefix_len) override;
  IPAddress::ptr subnetMask(uint32_t prefix_len) override;

  uint32_t getPort() const override;
  void setPort(uint16_t port) override;

private:
  sockaddr_in m_addr;
};

class IPv6Address : public IPAddress {
public:
  using ptr = std::shared_ptr<IPv6Address>;
  /**
   * @brief ͨ��IPv6��ַ�ַ�������IPv6Address
   * @param[in] address IPv6��ַ�ַ���
   * @param[in] port �˿ں�
   */
  static IPv6Address::ptr Create(const char *address, uint16_t port = 0);

  IPv6Address();
  /**
   * @brief ͨ��sockaddr_in6����IPv6Address
   * @param[in] address sockaddr_in6�ṹ��
   */
  IPv6Address(const sockaddr_in6 &address);
  /**
   * @brief ͨ��IPv6�����Ƶ�ַ����IPv6Address
   * @param[in] address IPv6�����Ƶ�ַ
   */
  IPv6Address(const uint8_t address[16], uint16_t port = 0);

  const sockaddr *getAddr() const override;
  sockaddr *getAddr() override;
  socklen_t getAddrLen() const override;
  std::ostream &insert(std::ostream &os) const override;

  IPAddress::ptr broadcastAddress(uint32_t prefix_len) override;
  IPAddress::ptr networkAddress(uint32_t prefix_len) override;
  IPAddress::ptr subnetMask(uint32_t prefix_len) override;

  uint32_t getPort() const override;
  void setPort(uint16_t port) override;

private:
  sockaddr_in6 m_addr;
};

class UnixAddress : public Address {
public:
  using ptr = std::shared_ptr<UnixAddress>;

  UnixAddress();
  UnixAddress(const std::string &path);

  const sockaddr *getAddr() const override;
  sockaddr *getAddr() override;
  socklen_t getAddrLen() const override;
  void setAddrLen(uint32_t v);

  std::string getPath() const;
  std::ostream &insert(std::ostream &os) const override;

private:
  sockaddr_un m_addr;
  socklen_t m_len;
};

class UnknownAddress : public Address {
public:
  using ptr = std::shared_ptr<UnknownAddress>;

  UnknownAddress(int family);
  UnknownAddress(const sockaddr &addr);
  const sockaddr *getAddr() const override;
  sockaddr *getAddr() override;
  socklen_t getAddrLen() const override;
  std::ostream &insert(std::ostream &os) const override;

private:
  sockaddr m_addr;
};

std::ostream &operator<<(std::ostream &os, const Address &addr);

} // namespace zz