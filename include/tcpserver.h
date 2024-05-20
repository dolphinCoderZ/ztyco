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
   * @brief 构造函数
   * @param[in] io_worker socket客户端工作的协程调度器
   * @param[in] accept_worker 服务器socket执行接收socket连接的协程调度器
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
   * @brief 绑定地址数组
   * @param[in] addrs 需要绑定的地址数组
   * @param[out] fails 绑定失败的地址
   * @return 是否绑定成功
   */
  virtual bool bind(const std::vector<Address::ptr> &addrs,
                    std::vector<Address::ptr> &fails);

  virtual bool start();
  virtual void stop();

  bool isStop() const { return m_isStop; }

  uint64_t getRecvTimeout() const { return m_recvTimeout; }
  void setRecvTimeout(uint64_t v) { m_recvTimeout = v; }

  /**
   * @brief 以字符串形式dump server信息
   */
  virtual std::string toString(const std::string &prefix = "");

protected:
  virtual void handleClient(Socket::ptr client);
  virtual void startAccept(Socket::ptr sock);

protected:
  std::vector<Socket::ptr> m_socks; // 监听socket数组
  IOManager *m_ioWorker;            // 新连接的Socket工作的调度器
  IOManager *m_acceptWorker;        // 服务器Socket接收连接的调度器
  uint64_t m_recvTimeout;
  std::string m_name;
  std::string m_type;
  bool m_isStop;
};

} // namespace zz