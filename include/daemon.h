#pragma once

#include "log.h"
#include <cstdint>
#include <ctime>
#include <functional>
namespace zz {
struct ProcessInfo {
  pid_t parent_id = 0;
  pid_t main_id = 0;
  uint64_t parent_start_time = 0;
  uint64_t main_start_time = 0;
  uint32_t restart_count = 0; // �����������Ĵ���

  std::string toString() const;
};

using ProcessInfoMgr = zz::Singleton<ProcessInfo>;

/**
 * @brief �����������ѡ�����ػ����̵ķ�ʽ
 * @param[in] argc ��������
 * @param[in] argv ����ֵ����
 * @param[in] main_cb ��������
 * @param[in] is_daemon �Ƿ��ػ����̵ķ�ʽ
 * @return ���س����ִ�н��
 */

int start_daemon(int argc, char **argv,
                 std::function<int(int argc, char **argv)> main_cb,
                 bool is_daemon);

} // namespace zz