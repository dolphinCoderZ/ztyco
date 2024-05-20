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
   * @brief 刷新设置定时器的执行时间
   */
  bool refresh();
  /**
   * @brief 重置定时器时间
   * @param[in] ms 定时器执行间隔时间(毫秒)
   * @param[in] from_now 是否从当前时间开始计算
   */
  bool reset(uint64_t ms, bool from_now);

private:
  /**
   * @brief 构造函数
   * @param[in] ms 定时器执行间隔时间
   * @param[in] cb 回调函数
   * @param[in] recurring 是否循环
   * @param[in] manager 定时器管理器
   */
  Timer(uint64_t ms, std::function<void()> cb, bool recurring,
        TimerManager *mgr);
  Timer(uint64_t next);

private:
  bool m_recurring = false;   // 是否循环执行
  uint64_t m_ms = 0;          // 执行周期
  uint64_t m_next = 0;        // 精确的执行时间
  std::function<void()> m_cb; // 执行函数
  TimerManager *m_mgr = nullptr;

private:
  struct Comparator {
    /**
     * @brief 比较定时器的智能指针的大小(按执行时间排序)
     * @param[in] lhs 定时器智能指针
     * @param[in] rhs 定时器智能指针
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
   * @brief 添加定时器
   * @param[in] ms 定时器执行间隔时间
   * @param[in] cb 定时器回调函数
   * @param[in] recurring 是否循环定时器
   */
  Timer::ptr addTimer(uint64_t ms, std::function<void()> cb,
                      bool recurring = false);

  /**
   * @brief 添加条件定时器
   * @param[in] ms 定时器执行间隔时间
   * @param[in] cb 定时器回调函数
   * @param[in] weak_cond 条件
   * @param[in] recurring 是否循环
   */
  Timer::ptr addConditionTimer(uint64_t ms, std::function<void()> cb,
                               std::weak_ptr<void> weak_cond,
                               bool recurring = false);

  /**
   * @brief 到最近一个定时器执行的时间间隔(毫秒)
   */
  uint64_t getNextTimer();

  /**
   * @brief 获取需要执行的定时器的回调函数列表
   * @param[out] cbs 回调函数数组
   */
  void listExpiredCb(std::vector<std::function<void()>> &cbs);

  bool hasTimer();

protected:
  virtual void onTimerInsertedAtFront() = 0;

  /**
   * @brief 实际将定时器添加到管理器中
   */
  void addTimer(Timer::ptr val, RWMutexType::WriteLock &lock);

private:
  bool detectClockRollover(uint64_t now_ms);

private:
  RWMutexType m_mutex;
  std::set<Timer::ptr, Timer::Comparator> m_timers; // set总是排序
  bool m_tickled = false;      // 是否触发onTimerInsertedAtFront
  uint64_t m_previousTime = 0; // 上次执行时间
};

} // namespace zz