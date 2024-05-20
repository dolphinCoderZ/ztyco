/**
 * @brief Э��ģ��
 * @details ����ucontext_tʵ�֣��ǶԳ�Э��
 * @note
 * �ǶԳ�Э�̣�����Э��ֻ�ܺ���Э���л�������Э�̽�����һ��Ҫ���л�����Э��
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
    INIT,  // ��ʼ��
    READY, // ����
    EXEC,  // ִ����
    HOLD,  // ��ͣ
    TERM,  // ����
    EXCEPT // �쳣
  };

private:
  /**
   * @attention
   * ����ÿ���̵߳���Э�̣���Э��û�к�������ջ��ֻ��������л�
   * ������Э�̲���resume��һ��Э�̣���ֻ�ܻص���Э�̣�����Э�̵Ĺ���ֻ��Ҫһ��
   */
  Fiber();

public:
  /**
   * @param[in] func Э��ִ�еĺ���
   * @param[in] stacksize Э��ջ��С
   * @param[in] use_caller �Ƿ���MainFiber�ϵ���
   */
  Fiber(std::function<void()> func, size_t stacksize = 0,
        bool use_caller = false);

  ~Fiber();

  /**
   * @brief ����Э��ִ�к���,������״̬
   * @pre getState() Ϊ INIT, TERM, EXCEPT
   * @post getState() = INIT
   */
  void resetFunc(std::function<void()> func);

  /**
   * @brief ����ǰЭ���л�������״̬
   * @pre getState() != EXEC
   * @post getState() = EXEC
   */
  void swapIn();

  /**
   * @brief ����ǰЭ���л�����̨
   */
  void swapOut();

  /**
   * @brief ����ǰ�߳��л���ִ��״̬
   * @pre ִ�е�Ϊ��ǰ�̵߳���Э��
   */
  void resume();

  /**
   * @brief ����ǰ�߳��л�����̨
   * @pre ִ�е�Ϊ��Э��
   * @post ���ص��̵߳���Э��
   */
  void yield();

  uint64_t getId() const { return m_id; }
  State getState() const { return m_state; }

public:
  /**
   * @brief ���õ�ǰ�̵߳�����Э��
   */
  static void SetThis(Fiber *f);

  /**
   * @brief ���ص�ǰ���ڵ�Э��
   */
  static Fiber::ptr GetThis();

  static uint64_t GetFiberId();

  /**
   * @brief ����ǰЭ���л�����̨,������״̬
   */
  static void YieldToReady();
  static void YieldToHold();

  static uint64_t TotalFibers();

  /**
   * @brief Э��ִ�к���
   * @post ִ����ɷ��ص��߳���Э��
   */
  static void MainFunc();

  /**
   * @brief Э��ִ�к���
   * @post ִ����ɷ��ص������̵߳ĵ���Э��
   */
  static void CallerMainFunc();

private:
  uint64_t m_id = 0;            // Э��id
  State m_state = INIT;         // Э��״̬
  uint32_t m_stacksize = 0;     // Э������ջ��С
  std::function<void()> m_func; // Э�����к���

  ucontext_t m_ctx;           // ����Э�������ģ������л�
  void *m_stackptr = nullptr; // Э������ջָ��
};

} // namespace zz