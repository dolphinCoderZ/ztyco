#pragma once

#include "mutex.h"
#include <functional>
#include <memory>
#include <set>
#include <vector>

namespace zz {

class TimerManager;

class Timer : public std::enable_shared_from_this<Timer> {
  friend class TimerManager;

public:
  using ptr = std::shared_ptr<Timer>;

  bool cancel();
  /**
   * @brief ˢ�����ö�ʱ����ִ��ʱ��
   */
  bool refresh();
  /**
   * @brief ���ö�ʱ��ʱ��
   * @param[in] ms ��ʱ��ִ�м��ʱ��(����)
   * @param[in] from_now �Ƿ�ӵ�ǰʱ�俪ʼ����
   */
  bool reset(uint64_t ms, bool from_now);

private:
  /**
   * @brief ���캯��
   * @param[in] ms ��ʱ��ִ�м��ʱ��
   * @param[in] cb �ص�����
   * @param[in] recurring �Ƿ�ѭ��
   * @param[in] manager ��ʱ��������
   */
  Timer(uint64_t ms, std::function<void()> cb, bool recurring,
        TimerManager *mgr);
  Timer(uint64_t next);

private:
  bool m_recurring = false;   // �Ƿ�ѭ��ִ��
  uint64_t m_ms = 0;          // ִ������
  uint64_t m_next = 0;        // ��ȷ��ִ��ʱ��
  std::function<void()> m_cb; // ִ�к���
  TimerManager *m_mgr = nullptr;

private:
  struct Comparator {
    /**
     * @brief �Ƚ϶�ʱ��������ָ��Ĵ�С(��ִ��ʱ������)
     * @param[in] lhs ��ʱ������ָ��
     * @param[in] rhs ��ʱ������ָ��
     */
    bool operator()(const Timer::ptr &lhs, const Timer::ptr &rhs) const;
  };
};

class TimerManager {
  friend class Timer;

public:
  using RWMutexType = RWMutex;

  TimerManager();
  virtual ~TimerManager();

  /**
   * @brief ��Ӷ�ʱ��
   * @param[in] ms ��ʱ��ִ�м��ʱ��
   * @param[in] cb ��ʱ���ص�����
   * @param[in] recurring �Ƿ�ѭ����ʱ��
   */
  Timer::ptr addTimer(uint64_t ms, std::function<void()> cb,
                      bool recurring = false);

  /**
   * @brief ���������ʱ��
   * @param[in] ms ��ʱ��ִ�м��ʱ��
   * @param[in] cb ��ʱ���ص�����
   * @param[in] weak_cond ����
   * @param[in] recurring �Ƿ�ѭ��
   */
  Timer::ptr addConditionTimer(uint64_t ms, std::function<void()> cb,
                               std::weak_ptr<void> weak_cond,
                               bool recurring = false);

  /**
   * @brief �����һ����ʱ��ִ�е�ʱ����(����)
   */
  uint64_t getNextTimer();

  /**
   * @brief ��ȡ��Ҫִ�еĶ�ʱ���Ļص������б�
   * @param[out] cbs �ص���������
   */
  void listExpiredCb(std::vector<std::function<void()>> &cbs);

  bool hasTimer();

protected:
  virtual void onTimerInsertedAtFront() = 0;

  /**
   * @brief ʵ�ʽ���ʱ����ӵ���������
   */
  void addTimer(Timer::ptr val, RWMutexType::WriteLock &lock);

private:
  bool detectClockRollover(uint64_t now_ms);

private:
  RWMutexType m_mutex;
  std::set<Timer::ptr, Timer::Comparator> m_timers; // set��������
  bool m_tickled = false;      // �Ƿ񴥷�onTimerInsertedAtFront
  uint64_t m_previousTime = 0; // �ϴ�ִ��ʱ��
};

} // namespace zz