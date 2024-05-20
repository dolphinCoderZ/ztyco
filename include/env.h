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
   * @brief ��ʼ����������¼����������·��������������ѡ��Ͳ���
   * @details
   * ������ѡ��ȫ����-��ͷ���������ѡ������ѡ�����������key-value�ṹ�����洢��������Զ��廷��������
   * ���ֻ��keyû��value����ôvalueΪ���ַ���
   * @param[in] argc main��������
   * @param[in] argv main��������
   */
  bool init(int argc, char **argv);

  /**
   * @brief �Զ��廷���������洢�ڳ����ڲ���map�ṹ��
   */
  void add(const std::string &key, const std::string &val);
  void del(const std::string &key);
  std::string get(const std::string &key, const std::string &defaultValue = "");
  bool has(const std::string &key);

  /**
   * @brief �����а���ѡ��
   * @param[in] key ѡ����
   * @param[in] desc ѡ������
   */
  void addHelp(const std::string &key, const std::string &desc);
  void removeHelp(const std::string &key);
  void printHelp();

  const std::string &getExePath() const { return m_exepath; }
  const std::string &getCwd() const { return m_cwd; }

  /**
   * @brief ϵͳ��������
   */
  bool setEnv(const std::string &key, const std::string &val);
  std::string getEnv(const std::string &key, const std::string &val = "");

  std::string getAbsolutePath(const std::string &path) const;
  std::string getAbsoluteWorkPath(const std::string &path) const;
  std::string getConfigPath();

private:
  RWMutexType m_mutex;
  std::map<std::string, std::string> m_args; // �Զ��廷������
  std::vector<std::pair<std::string, std::string>> m_helps; // ������Ϣ

  std::string m_programname;
  std::string m_exepath; // ��������·��
  std::string m_cwd;     // ��ǰ·��
};

// ����ģʽ����֤����Ļ�������ȫ��Ψһ
using EnvMgr = zz::Singleton<Env>;

} // namespace zz