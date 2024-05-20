#pragma once

#include "log.h"
#include "mutex.h"
#include <memory>
#include <vector>

namespace zz {

/**
 * @brief 文件句柄上下文类
 * @details 管理文件句柄类型(是否socket)
 *          是否阻塞,是否关闭,读/写超时时间
 */
class FdCtx : public std::enable_shared_from_this<FdCtx> {
public:
  using ptr = std::shared_ptr<FdCtx>;
  FdCtx(int fd);
  ~FdCtx();

  bool isInit();
  bool isScoket() const { return m_isSocket; }
  bool isClose() const { return m_isClosed; }

  void setUserNonblock(bool v) { m_userNonblock = v; }
  bool getUserNonblock() const { return m_userNonblock; }
  void setSysNonblock(bool v) { m_sysNonblock = v; }
  bool getSysNonblock() const { return m_sysNonblock; }

  /**
   * @brief 超时时间
   * @param[in] type 类型SO_RCVTIMEO(读超时), SO_SNDTIMEO(写超时)
   * @param[in] v 时间毫秒
   */
  void setTimeout(int type, uint64_t v);
  uint64_t getTimeout(int type);

private:
  bool init();

private:
  bool m_isInit : 1;
  bool m_isSocket : 1;
  bool m_sysNonblock : 1;  // 是否hook非阻塞
  bool m_userNonblock : 1; // 是否用户主动设置非阻
  bool m_isClosed : 1;
  int m_fd;
  uint64_t m_recvTimeout; // 读超时时间毫秒
  uint64_t m_sendTimeout; // 写超时时间毫秒
};

class FdManager {
public:
  using RWMutexType = RWMutex;
  FdManager();

  FdCtx::ptr get(int fd, bool auto_create = false);
  void del(int fd);

private:
  RWMutexType m_mutex;
  std::vector<FdCtx::ptr> m_datas;
};

using FdMgr = Singleton<FdManager>;

} // namespace zz