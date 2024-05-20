#pragma once

#include "log.h"
#include "mutex.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace zz {
class Env {
public:
  using RWMutexType = RWMutex;

  /**
   * @brief 初始化，包括记录程序名称与路径，解析命令行选项和参数
   * @details
   * 命令行选项全部以-开头，后面跟可选参数，选项与参数构成key-value结构，被存储到程序的自定义环境变量中
   * 如果只有key没有value，那么value为空字符串
   * @param[in] argc main函数传入
   * @param[in] argv main函数传入
   */
  bool init(int argc, char **argv);

  /**
   * @brief 自定义环境变量，存储在程序内部的map结构中
   */
  void add(const std::string &key, const std::string &val);
  void del(const std::string &key);
  std::string get(const std::string &key, const std::string &defaultValue = "");
  bool has(const std::string &key);

  /**
   * @brief 命令行帮助选项
   * @param[in] key 选项名
   * @param[in] desc 选项描述
   */
  void addHelp(const std::string &key, const std::string &desc);
  void removeHelp(const std::string &key);
  void printHelp();

  const std::string &getExePath() const { return m_exepath; }
  const std::string &getCwd() const { return m_cwd; }

  /**
   * @brief 系统环境变量
   */
  bool setEnv(const std::string &key, const std::string &val);
  std::string getEnv(const std::string &key, const std::string &val = "");

  std::string getAbsolutePath(const std::string &path) const;
  std::string getAbsoluteWorkPath(const std::string &path) const;
  std::string getConfigPath();

private:
  RWMutexType m_mutex;
  std::map<std::string, std::string> m_args; // 自定义环境变量
  std::vector<std::pair<std::string, std::string>> m_helps; // 帮助信息

  std::string m_programname;
  std::string m_exepath; // 程序完整路径
  std::string m_cwd;     // 当前路径
};

// 单例模式，保证程序的环境变量全局唯一
using EnvMgr = zz::Singleton<Env>;

} // namespace zz