#include "daemon.h"
#include "config.h"
#include "log.h"
#include <ctime>
#include <functional>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace zz {

static zz::Logger::ptr g_logger = ZZ_LOG_NAME("system");

static ConfigVar<uint32_t>::ptr g_daemon_restart_interval = zz::Config::Lookup(
    "daemon.restart_interval", (uint32_t)5, "daemon restart interval");

std::string ProcessInfo::toString() const {
  std::stringstream ss;
  ss << "[ProcessInfo parent_id=" << parent_id << " main_id=" << main_id
     << " parent_start_time=" << zz::Time2Str(parent_start_time)
     << " main_start_time=" << zz::Time2Str(main_start_time)
     << " restart_count=" << restart_count << "]";
  return ss.str();
}

static int real_start(int argc, char **argv,
                      std::function<int(int argc, char **argv)> main_cb) {
  return main_cb(argc, argv);
}

static int real_daemon(int argc, char **argv,
                       std::function<int(int argc, char **argv)> main_cb) {
  daemon(1, 0); // ����ǰ�������ػ����̵���ʽ����
  ProcessInfoMgr::GetInstance()->parent_id = getpid();
  ProcessInfoMgr::GetInstance()->parent_start_time = time(nullptr);

  while (1) {
    pid_t pid = fork(); // �ػ�����fork�ӽ��̣��ӽ���������ҵ��
    if (pid == 0) {     // �ӽ���
      ProcessInfoMgr::GetInstance()->main_id = getpid();
      ProcessInfoMgr::GetInstance()->main_start_time = time(nullptr);
      ZZ_LOG_INFO(g_logger) << "process start pid=" << getpid();
      return real_start(argc, argv, main_cb);
    } else if (pid < 0) {
      ZZ_LOG_ERROR(g_logger) << "fork fail return=" << pid << " errno=" << errno
                             << " errstr=" << strerror(errno);
    } else { // ������
      int status = 0;
      waitpid(pid, &status, 0);
      if (status) {
        ZZ_LOG_ERROR(g_logger)
            << "child crash pid=" << pid << " status=" << status;
      } else {
        ZZ_LOG_INFO(g_logger) << "child finished pid=" << pid;
        break;
      }
      // �ӽ����˳������һ��ʱ���������ӽ���
      ProcessInfoMgr::GetInstance()->restart_count += 1;
      sleep(g_daemon_restart_interval->getValue());
    }
  }
  return 0;
}

int start_daemon(int argc, char **argv,
                 std::function<int(int argc, char **argv)> main_cb,
                 bool is_daemon) {
  if (!is_daemon) {
    return real_start(argc, argv, main_cb);
  } else {
    return real_daemon(argc, argv, main_cb);
  }
}

} // namespace zz