

#include "timer.h"
#include "mutex.h"
#include "util.h"
#include <functional>
#include <memory>
#include <vector>
namespace zz {

bool Timer::Comparator::operator()(const Timer::ptr &lhs,
                                   const Timer::ptr &rhs) const {
  if (!lhs && !rhs) {
    return false;
  }
  if (!lhs) {
    return true;
  }
  if (!rhs) {
    return false;
  }

  if (lhs->m_next < rhs->m_next) {
    return true;
  }
  if (rhs->m_next < lhs->m_next) {
    return false;
  }

  return lhs.get() < rhs.get();
}

Timer::Timer(uint64_t ms, std::function<void()> cb, bool recurring,
             TimerManager *mgr)
    : m_recurring(recurring), m_ms(ms), m_cb(cb), m_mgr(mgr) {
  m_next = zz::GetCurrentMS() + m_ms; // ת��Ϊ����ʱ��
}

Timer::Timer(uint64_t next) : m_next(next) {}

bool Timer::cancel() {
  TimerManager::RWMutexType::WriteLock lock(m_mgr->m_mutex);
  if (m_cb) {
    m_cb = nullptr;
    auto it = m_mgr->m_timers.find(shared_from_this());
    m_mgr->m_timers.erase(it);
    return true;
  }
  return false;
}

/**
 * ������������Ҫ��Ϊ�˱�֤��ʱ�����ϵ�һ���Ժ���ȷ�ԡ�
 * ԭ�����£�
 * �Ƴ���ʱ�������Ƚ���ʱ����TimerManager���Ƴ�����Ϊ�˱����ڶ�ʱ�������д��ڶ����ͬ�Ķ�ʱ��������ȷ��ÿ����ʱ�������еĶ�ʱ��������Ψһ�ġ�
 * �޸�ʱ�䣺Ȼ���޸Ķ�ʱ����ʱ�䣬ȷ����ʱ�����´γ�ʱʱ���ǰ��������õ�ʱ�����ó��ġ�
 * ���¼ӻ�ȥ������ٽ��޸ĺ�Ķ�ʱ������ӻص�TimerManager�У��Ա����´γ�ʱʱ�ܹ�����ȷ������
 */
bool Timer::refresh() {
  TimerManager::RWMutexType::WriteLock lock(m_mgr->m_mutex);
  if (!m_cb) {
    return false;
  }

  auto it = m_mgr->m_timers.find(shared_from_this());
  if (it == m_mgr->m_timers.end()) {
    return false;
  }

  m_mgr->m_timers.erase(it);
  m_next = zz::GetCurrentMS() + m_ms;
  m_mgr->m_timers.insert(shared_from_this());
  return true;
}

bool Timer::reset(uint64_t ms, bool from_now) {
  if (ms == m_ms && !from_now) {
    return true;
  }

  TimerManager::RWMutexType::WriteLock lock(m_mgr->m_mutex);
  if (!m_cb) {
    return false;
  }
  auto it = m_mgr->m_timers.find(shared_from_this());
  if (it == m_mgr->m_timers.end()) {
    return false;
  }

  m_mgr->m_timers.erase(it);
  uint64_t start = 0;
  if (from_now) {
    start = zz::GetCurrentMS();
  } else {
    start = m_next - m_ms;
  }
  m_ms = ms;
  m_next = start + m_ms;
  m_mgr->addTimer(shared_from_this(), lock);
  return true;
}

TimerManager::TimerManager() { m_previousTime = zz::GetCurrentMS(); }

TimerManager::~TimerManager() {}

Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb,
                                  bool recurring) {
  Timer::ptr timer(new Timer(ms, cb, recurring, this));
  RWMutexType::WriteLock lock(m_mutex);
  addTimer(timer, lock); // ��ӵ�������
  return timer;
}

static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb) {
  std::shared_ptr<void> tmp = weak_cond.lock();
  if (tmp) {
    cb();
  }
}

Timer::ptr TimerManager::addConditionTimer(uint64_t ms,
                                           std::function<void()> cb,
                                           std::weak_ptr<void> weak_cond,
                                           bool recurring) {
  return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
}

void TimerManager::addTimer(Timer::ptr val, RWMutexType::WriteLock &lock) {
  auto it = m_timers.insert(val).first;
  bool atFront = (it == m_timers.begin()) && !m_tickled;
  if (atFront) {
    m_tickled = true;
  }
  lock.unlock();

  if (atFront) {
    onTimerInsertedAtFront();
  }
}

uint64_t TimerManager::getNextTimer() {
  RWMutexType::ReadLock lock(m_mutex);
  m_tickled = false;
  if (m_timers.empty()) {
    return ~0ull;
  }

  const Timer::ptr &next = *m_timers.begin();
  uint64_t now_ms = zz::GetCurrentMS();
  if (now_ms >= next->m_next) {
    return 0;
  } else {
    return next->m_next - now_ms;
  }
}

void TimerManager::listExpiredCb(std::vector<std::function<void()>> &cbs) {
  uint64_t now_ms = zz::GetCurrentMS();
  std::vector<Timer::ptr> expired;
  {
    RWMutexType::ReadLock lock(m_mutex);
    if (m_timers.empty()) {
      return;
    }
  }

  RWMutexType::WriteLock lock(m_mutex);
  if (m_timers.empty()) {
    return;
  }

  bool rollover = detectClockRollover(now_ms);
  if (!rollover && ((*m_timers.begin())->m_next > now_ms)) {
    return;
  }

  Timer::ptr now_timer(new Timer(now_ms));
  auto it = rollover ? m_timers.end() : m_timers.lower_bound(now_timer);
  while (it != m_timers.end() && (*it)->m_next == now_ms) {
    ++it;
  }

  expired.insert(expired.begin(), m_timers.begin(), it);
  m_timers.erase(m_timers.begin(), it);
  cbs.reserve(expired.size());

  for (auto &timer : expired) {
    cbs.push_back(timer->m_cb);
    if (timer->m_recurring) {
      timer->m_next = now_ms + timer->m_ms;
      m_timers.insert(timer);
    } else {
      timer->m_cb = nullptr;
    }
  }
}

bool TimerManager::detectClockRollover(uint64_t now_ms) {
  bool rollover = false;
  if (now_ms < m_previousTime && now_ms < (m_previousTime - 60 * 60 * 1000)) {
    rollover = true;
  }
  m_previousTime = now_ms;
  return rollover;
}

bool TimerManager::hasTimer() {
  RWMutexType::ReadLock lock(m_mutex);
  return !m_timers.empty();
}

} // namespace zz