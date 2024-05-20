#pragma once

#include "address.h"
#include "iomanager.h"
#include "socket.h"
#include <memory>
#include <string>
#include <vector>
namespace zz {

class TcpServer : public std::enable_shared_from_this<TcpServer> {
public:
  using ptr = std::shared_ptr<TcpServer>;

  /**
   * @brief ���캯��
   * @param[in] io_worker socket�ͻ��˹�����Э�̵�����
   * @param[in] accept_worker ������socketִ�н���socket���ӵ�Э�̵�����
   */
  TcpServer(zz::IOManager *io_worker = zz::IOManager::GetThis(),
            zz::IOManager *accept_worker = zz::IOManager::GetThis(),
            const std::string &name = "server",
            const std::string &type = "tcp");
  virtual ~TcpServer();

  std::string getName() const { return m_name; }
  virtual void setName(const std::string &v) { m_name = v; }

  virtual bool bind(zz::Address::ptr addr);
  /**
   * @brief �󶨵�ַ����
   * @param[in] addrs ��Ҫ�󶨵ĵ�ַ����
   * @param[out] fails ��ʧ�ܵĵ�ַ
   * @return �Ƿ�󶨳ɹ�
   */
  virtual bool bind(const std::vector<Address::ptr> &addrs,
                    std::vector<Address::ptr> &fails);

  virtual bool start();
  virtual void stop();

  bool isStop() const { return m_isStop; }

  uint64_t getRecvTimeout() const { return m_recvTimeout; }
  void setRecvTimeout(uint64_t v) { m_recvTimeout = v; }

  /**
   * @brief ���ַ�����ʽdump server��Ϣ
   */
  virtual std::string toString(const std::string &prefix = "");

protected:
  virtual void handleClient(Socket::ptr client);
  virtual void startAccept(Socket::ptr sock);

protected:
  std::vector<Socket::ptr> m_socks; // ����socket����
  IOManager *m_ioWorker;            // �����ӵ�Socket�����ĵ�����
  IOManager *m_acceptWorker;        // ������Socket�������ӵĵ�����
  uint64_t m_recvTimeout;
  std::string m_name;
  std::string m_type;
  bool m_isStop;
};

} // namespace zz