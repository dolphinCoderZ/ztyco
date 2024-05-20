/**
 * @brief ����Epoll��IOЭ�̵�����
 */

#pragma once

#include "fiber.h"
#include "mutex.h"
#include "scheduler.h"
#include "timer.h"
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace zz {
class IOManager : public Scheduler, public TimerManager {
public:
  using ptr = std::shared_ptr<IOManager>;
  using RWMutexType = RWMutex;

  enum Event { NONE = 0x0, READ = 0x1, WRITE = 0x4 };

private:
  /**
   * @brief socket fd��������
   * @details
   * ��Ӧһ��FdContext������fd��ֵ��fd�ϵ��¼����Լ�fd�Ķ�д�¼��ص�����
   */
  struct FdContext {
    using MutexType = Mutex;
    /**
     * @brief �¼���������
     */
    struct EventContext {
      // �¼�ִ�����õĵ�����
      Scheduler *scheduler = nullptr;
      // �¼���Э��
      Fiber::ptr fiber;
      // �¼��ص�����
      std::function<void()> cb;
    };

    EventContext &getEventContext(Event e);
    void resetEventContext(EventContext &ectx);
    void triggerEvent(Event e);

    EventContext read;
    EventContext write;
    int fd = 0;          // �¼������ľ��
    Event events = NONE; // ����Ϲ��ĵ��¼�����
    MutexType mutex;
  };

public:
  IOManager(size_t threads = 1, bool use_caller = true,
            const std::string &name = "");
  ~IOManager();

  /**
   * @brief ����¼�
   * @details fd������������event�¼�ʱִ��cb����
   * @param[in] fd socket���
   * @param[in] event �¼�����
   * @param[in] cb �¼��ص�����
   */
  int addEvent(int fd, Event e, std::function<void()> cb = nullptr);

  /**
   * @brief ɾ���¼�
   * @param[in] fd socket���
   * @param[in] event �¼�����
   * @attention ���ᴥ���¼�
   */
  bool delEvent(int fd, Event e);

  /**
   * @brief ȡ���¼�
   * @param[in] fd socket���
   * @param[in] event �¼�����
   * @attention ������¼���ע����ص����Ǿʹ���һ�λص��¼�
   */
  bool cancelEvent(int fd, Event e);
  bool cancelAll(int fd);

  static IOManager *GetThis();

protected:
  /**
   * @brief ֪ͨ������������Ҫ����
   * @details
   * дpipe��idleЭ�̴�epoll_wait�˳�����idleЭ��yield֮��Scheduler::run�Ϳ��Ե�����������
   */
  void tickle() override;

  bool stopping() override;
  /**
   * @brief �ж��Ƿ����ֹͣ��ͬʱ��ȡ���һ����ʱ���ĳ�ʱʱ��
   * @param[out] timeout ���һ����ʱ���ĳ�ʱʱ�䣬����idleЭ�̵�epoll_wait
   * @return �����Ƿ����ֹͣ
   */
  bool stopping(uint64_t &timeout);

  /**
   * @brief idleЭ��
   * @details
   * ����IOЭ�̵�����˵��Ӧ�����ڵȴ�IO�¼��ϣ�idle�˳���ʱ����epoll_wait���أ���Ӧ�Ĳ�����tickle��ע���IO�¼�����
   */
  void idle() override;

  /**
   * @brief �¶�ʱ������ͷ�������³�ʱʱ�䣬����idleЭ��ʹ���µĳ�ʱʱ��
   */
  void onTimerInsertedAtFront() override;

  /**
   * @brief ����socket��������Ķ���ش�С
   * @param[in] size ������С
   */
  void contextResize(size_t size);

private:
  int m_epfd = 0;     // epoll�ļ����
  int m_tickleFds[2]; // pipe�����fd[0]���ˣ�fd[1]д��
  std::atomic<size_t> m_pendingEventCount = {0}; // ����IO�¼�����
  RWMutexType m_mutex;
  std::vector<FdContext *> m_fdContexts; // fd����أ������Ѿ�ע���fd
};

} // namespace zz