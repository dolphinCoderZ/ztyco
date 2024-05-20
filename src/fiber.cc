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

// �ֲ߳̾�������ÿ���̶߳�����һ�ݣ�ÿ���̶߳�Ҫ��ʱ������Э�̶������������Э��
// ��������Э��
static thread_local Fiber *t_fiber = nullptr;
// ��ǰ�̵߳���Э�̣�ÿ���̵߳ĵ�һ��Э��
static thread_local Fiber::ptr t_threadFiber = nullptr;

// ȫ�־�̬��������������Э��id
static std::atomic<uint64_t> s_fiber_id{0};
// ͳ��Э������
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
  return 0; // ��ЧЭ��id��Э��id��1��ʼ
}

// ��Э��ר�ù��캯������ִ���κκ�����ֻΪ��ʼ����ǰ�̵߳�Э�̹���
Fiber::Fiber() {
  SetThis(this);
  m_state = EXEC;

  if (getcontext(&m_ctx)) {
    ZZ_ASSERT2(false, "getcontext");
  }

  ++s_fiber_count;
  m_id = s_fiber_id++; // Э��id��0��ʼ�������1
  ZZ_LOG_DEBUG(g_logger) << "Fiber::Fiber main";
}

// ��Э�̵Ĺ��캯��������������ջ
Fiber::Fiber(std::function<void()> func, size_t stacksize, bool use_caller)
    : m_id(++s_fiber_count), m_func(func) {
  ++s_fiber_count;
  // ��������ջ
  m_stacksize = stacksize ? stacksize : g_fiber_stack_size->getValue();
  m_stackptr = StackAllocator::Allocate(m_stacksize);

  if (getcontext(&m_ctx)) {
    ZZ_ASSERT2(false, "getcontext");
  }

  // ����������
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
  if (m_stackptr) { // ������ջ˵������Э�̣�ȷ��Э�̵�״̬
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

// ���ú�������ջ�ռ�
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

// ��ǰЭ�̺��������е�Э��(һ������Э��)����
void Fiber::resume() {
  SetThis(this);
  m_state = EXEC;
  if (swapcontext(&(t_threadFiber->m_ctx), &m_ctx)) {
    ZZ_ASSERT2(false, "swapcontext");
  }
}

// ��ǰЭ��(һ������Э��)������ִֹ�У��л�����Э��
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

// ʹ��Э��֮ǰ������ʾ����һ��
Fiber::ptr Fiber::GetThis() {
  if (t_fiber) {
    return t_fiber->shared_from_this();
  }

  // ��ǰ�߳�û��Э�̾ʹ�����Э��(����˽�й��캯��)�������̵߳�Э�̹���
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

// Э��ִ�к��������Զ��л��ص����߳�
void Fiber::MainFunc() {
  Fiber::ptr cur = GetThis();
  ZZ_ASSERT(cur);
  try {
    cur->m_func(); // ִ�к�������
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
  cur.reset(); // cur������ָ�룬reset������ü�������
  // ���ü���-1�����ü�����Ϊ0���ͷ��ڴ�
  rawPtr->swapOut(); // ��������ִ����ϣ�Э���Զ��л��ص���Э��
}

// use_call����£��л�����Э��
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
  rawPtr->yield(); // caller�߳��е���Э��ִ����Ϸ�����Э��
}

} // namespace zz