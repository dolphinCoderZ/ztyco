#pragma once

#include "log.h"
#include "mutex.h"
#include <memory>
#include <vector>

namespace zz {

/**
 * @brief �ļ������������
 * @details �����ļ��������(�Ƿ�socket)
 *          �Ƿ�����,�Ƿ�ر�,��/д��ʱʱ��
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
   * @brief ��ʱʱ��
   * @param[in] type ����SO_RCVTIMEO(����ʱ), SO_SNDTIMEO(д��ʱ)
   * @param[in] v ʱ�����
   */
  void setTimeout(int type, uint64_t v);
  uint64_t getTimeout(int type);

private:
  bool init();

private:
  bool m_isInit : 1;
  bool m_isSocket : 1;
  bool m_sysNonblock : 1;  // �Ƿ�hook������
  bool m_userNonblock : 1; // �Ƿ��û��������÷���
  bool m_isClosed : 1;
  int m_fd;
  uint64_t m_recvTimeout; // ����ʱʱ�����
  uint64_t m_sendTimeout; // д��ʱʱ�����
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