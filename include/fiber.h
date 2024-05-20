/**
 * @brief 协程模块
 * @details 基于ucontext_t实现，非对称协程
 * @note
 * 非对称协程，即子协程只能和主协程切换，且子协程结束后，一定要再切换回主协程
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <sys/ucontext.h>
#include <ucontext.h>

namespace zz {

class Scheduler;

class Fiber : public std::enable_shared_from_this<Fiber> {
  friend class Scheduler;

public:
  using ptr = std::shared_ptr<Fiber>;

  enum State {
    INIT,  // 初始化
    READY, // 就绪
    EXEC,  // 执行中
    HOLD,  // 暂停
    TERM,  // 结束
    EXCEPT // 异常
  };

private:
  /**
   * @attention
   * 创建每个线程的主协程，主协程没有函数运行栈，只负责过渡切换
   * 由于子协程不能resume另一个协程，而只能回到主协程，而主协程的构造只需要一次
   */
  Fiber();

public:
  /**
   * @param[in] func 协程执行的函数
   * @param[in] stacksize 协程栈大小
   * @param[in] use_caller 是否在MainFiber上调度
   */
  Fiber(std::function<void()> func, size_t stacksize = 0,
        bool use_caller = false);

  ~Fiber();

  /**
   * @brief 重置协程执行函数,并设置状态
   * @pre getState() 为 INIT, TERM, EXCEPT
   * @post getState() = INIT
   */
  void resetFunc(std::function<void()> func);

  /**
   * @brief 将当前协程切换到运行状态
   * @pre getState() != EXEC
   * @post getState() = EXEC
   */
  void swapIn();

  /**
   * @brief 将当前协程切换到后台
   */
  void swapOut();

  /**
   * @brief 将当前线程切换到执行状态
   * @pre 执行的为当前线程的主协程
   */
  void resume();

  /**
   * @brief 将当前线程切换到后台
   * @pre 执行的为该协程
   * @post 返回到线程的主协程
   */
  void yield();

  uint64_t getId() const { return m_id; }
  State getState() const { return m_state; }

public:
  /**
   * @brief 设置当前线程的运行协程
   */
  static void SetThis(Fiber *f);

  /**
   * @brief 返回当前所在的协程
   */
  static Fiber::ptr GetThis();

  static uint64_t GetFiberId();

  /**
   * @brief 将当前协程切换到后台,并设置状态
   */
  static void YieldToReady();
  static void YieldToHold();

  static uint64_t TotalFibers();

  /**
   * @brief 协程执行函数
   * @post 执行完成返回到线程主协程
   */
  static void MainFunc();

  /**
   * @brief 协程执行函数
   * @post 执行完成返回到调度线程的调度协程
   */
  static void CallerMainFunc();

private:
  uint64_t m_id = 0;            // 协程id
  State m_state = INIT;         // 协程状态
  uint32_t m_stacksize = 0;     // 协程运行栈大小
  std::function<void()> m_func; // 协程运行函数

  ucontext_t m_ctx;           // 保存协程上下文，用于切换
  void *m_stackptr = nullptr; // 协程运行栈指针
};

} // namespace zz