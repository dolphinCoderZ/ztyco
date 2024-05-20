#include "fiber.h"
#include "config.h"
#include "log.h"
#include "macro.h"
#include "scheduler.h"
#include "util.h"
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <functional>
#include <string>
#include <ucontext.h>

namespace zz {

static Logger::ptr g_logger = ZZ_LOG_NAME("system");

// 线程局部变量在每个线程都独享一份，每个线程都要随时保存主协程对象和正在运行协程
// 正在运行协程
static thread_local Fiber *t_fiber = nullptr;
// 当前线程的主协程，每个线程的第一个协程
static thread_local Fiber::ptr t_threadFiber = nullptr;

// 全局静态变量，用于生成协程id
static std::atomic<uint64_t> s_fiber_id{0};
// 统计协程总数
static std::atomic<uint64_t> s_fiber_count{0};

static ConfigVar<uint32_t>::ptr g_fiber_stack_size = Config::Lookup<uint32_t>(
    "fiber.stack_size", 128 * 1024, "fiber stack size");

class MallocStackAllocater {
public:
  static void *Allocate(size_t size) { return malloc(size); }
  static void Deallocate(void *p, size_t size) { free(p); }
};
using StackAllocator = MallocStackAllocater;

uint64_t Fiber::GetFiberId() {
  if (t_fiber) {
    return t_fiber->getId();
  }
  return 0; // 无效协程id，协程id从1开始
}

// 主协程专用构造函数，不执行任何函数，只为初始化当前线程的协程功能
Fiber::Fiber() {
  SetThis(this);
  m_state = EXEC;

  if (getcontext(&m_ctx)) {
    ZZ_ASSERT2(false, "getcontext");
  }

  ++s_fiber_count;
  m_id = s_fiber_id++; // 协程id从0开始，用完加1
  ZZ_LOG_DEBUG(g_logger) << "Fiber::Fiber main";
}

// 子协程的构造函数，带函数运行栈
Fiber::Fiber(std::function<void()> func, size_t stacksize, bool use_caller)
    : m_id(++s_fiber_count), m_func(func) {
  ++s_fiber_count;
  // 分配运行栈
  m_stacksize = stacksize ? stacksize : g_fiber_stack_size->getValue();
  m_stackptr = StackAllocator::Allocate(m_stacksize);

  if (getcontext(&m_ctx)) {
    ZZ_ASSERT2(false, "getcontext");
  }

  // 设置上下文
  m_ctx.uc_link = nullptr;
  m_ctx.uc_stack.ss_sp = m_stackptr;
  m_ctx.uc_stack.ss_size = m_stacksize;

  if (use_caller == false) {
    makecontext(&m_ctx, &Fiber::MainFunc, 0);
  } else {
    makecontext(&m_ctx, &Fiber::CallerMainFunc, 0);
  }
}

Fiber::~Fiber() {
  --s_fiber_count;
  if (m_stackptr) { // 有运行栈说明是子协程，确保协程的状态
    ZZ_ASSERT(m_state == TERM || m_state == EXCEPT || m_state == INIT);
    StackAllocator::Deallocate(m_stackptr, m_stacksize);
  } else {
    ZZ_ASSERT(!m_func);
    ZZ_ASSERT(m_state == EXEC);

    Fiber *cur = t_fiber;
    if (cur == this) {
      SetThis(nullptr);
    }
  }
  ZZ_LOG_DEBUG(g_logger) << "Fiber::~Fiber id=" << m_id
                         << " total=" << s_fiber_count;
}

// 复用函数运行栈空间
void Fiber::resetFunc(std::function<void()> func) {
  ZZ_ASSERT(m_stackptr);
  ZZ_ASSERT(m_state == TERM || m_state == EXCEPT || m_state == INIT);

  m_func = func;
  if (getcontext(&m_ctx)) {
    ZZ_ASSERT2(false, "getcontext");
  }

  m_ctx.uc_link = nullptr;
  m_ctx.uc_stack.ss_sp = m_stackptr;
  m_ctx.uc_stack.ss_size = m_stacksize;

  makecontext(&m_ctx, &Fiber::MainFunc, 0);
  m_state = INIT;
}

// 当前协程和正在运行的协程(一定是主协程)交换
void Fiber::resume() {
  SetThis(this);
  m_state = EXEC;
  if (swapcontext(&(t_threadFiber->m_ctx), &m_ctx)) {
    ZZ_ASSERT2(false, "swapcontext");
  }
}

// 当前协程(一定是子协程)主动终止执行，切换回主协程
void Fiber::yield() {
  SetThis(t_threadFiber.get());
  if (swapcontext(&m_ctx, &(t_threadFiber->m_ctx))) {
    ZZ_ASSERT2(false, "swapcontext");
  }
}

void Fiber::swapIn() {
  SetThis(this);
  ZZ_ASSERT(m_state != EXEC);
  m_state = EXEC;
  if (swapcontext(&Scheduler::GetMainFiber()->m_ctx, &m_ctx)) {
    ZZ_ASSERT2(false, "swapcontext");
  }
}

void Fiber::swapOut() {
  SetThis(Scheduler::GetMainFiber());
  if (swapcontext(&m_ctx, &Scheduler::GetMainFiber()->m_ctx)) {
    ZZ_ASSERT2(false, "swapcontext");
  }
}

void Fiber::SetThis(Fiber *f) { t_fiber = f; }

// 使用协程之前必须显示调用一次
Fiber::ptr Fiber::GetThis() {
  if (t_fiber) {
    return t_fiber->shared_from_this();
  }

  // 当前线程没有协程就创建主协程(调用私有构造函数)，激活线程的协程功能
  Fiber::ptr main_fiber(new Fiber);
  ZZ_ASSERT(t_fiber == main_fiber.get());
  t_threadFiber = main_fiber;
  return t_fiber->shared_from_this();
}

uint64_t Fiber::TotalFibers() { return s_fiber_count; }

void Fiber::YieldToReady() {
  Fiber::ptr cur = GetThis();
  ZZ_ASSERT(cur->m_state == EXEC);
  cur->m_state = READY;
  cur->swapOut();
}

void Fiber::YieldToHold() {
  Fiber::ptr cur = GetThis();
  ZZ_ASSERT(cur->m_state == EXEC);
  cur->m_state = HOLD;
  cur->swapOut();
}

// 协程执行函数，并自动切换回调度线程
void Fiber::MainFunc() {
  Fiber::ptr cur = GetThis();
  ZZ_ASSERT(cur);
  try {
    cur->m_func(); // 执行函数例程
    cur->m_func = nullptr;
    cur->m_state = TERM;
  } catch (std::exception &e) {
    cur->m_state = EXCEPT;
    ZZ_LOG_ERROR(g_logger) << "Fiber Except: " << e.what()
                           << " fiber_id=" << cur->getId() << std::endl
                           << zz::BackTraceToString();
  } catch (...) {
    cur->m_state = EXCEPT;
    ZZ_LOG_ERROR(g_logger) << "Fiber Except" << " fiber_id=" << cur->getId()
                           << std::endl
                           << zz::BackTraceToString();
  }

  auto rawPtr = cur.get();
  cur.reset(); // cur是智能指针，reset解决引用计数问题
  // 引用计数-1，引用计数减为0就释放内存
  rawPtr->swapOut(); // 函数例程执行完毕，协程自动切换回调度协程
}

// use_call情况下，切换回主协程
void Fiber::CallerMainFunc() {
  Fiber::ptr cur = GetThis();
  ZZ_ASSERT(cur);

  try {
    cur->m_func();
    cur->m_func = nullptr;
    cur->m_state = TERM;
  } catch (std::exception &e) {
    cur->m_state = EXCEPT;
    ZZ_LOG_ERROR(g_logger) << "Fiber Except: " << e.what()
                           << " fiber_id=" << cur->getId() << std::endl
                           << zz::BackTraceToString();
  } catch (...) {
    cur->m_state = EXCEPT;
    ZZ_LOG_ERROR(g_logger) << "Fiber Except" << " fiber_id=" << cur->getId()
                           << std::endl
                           << zz::BackTraceToString();
  }

  auto rawPtr = cur.get();
  cur.reset();
  rawPtr->yield(); // caller线程中调度协程执行完毕返回主协程
}

} // namespace zz