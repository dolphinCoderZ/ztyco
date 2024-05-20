#pragma once

#include <cstdarg>
#include <cstdint>
#include <ctime>
#include <cxxabi.h>
#include <fstream>
#include <ios>
#include <sched.h>
#include <string>
#include <vector>

namespace zz {
/**
 * @brief ��ȡ�߳�id
 * @note ���ﲻҪ��pid_t��pthread_t��������������֮������ɲο�gettid(2)
 */
pid_t GetThreadId();

/**
 * @brief ��ȡЭ��id
 */
uint64_t GetFiberId();

/**
 * @brief ��ȡ��ǰʱ��
 */
uint64_t GetCurrentMS();
uint64_t GetCurrentUS();

/**
 * @brief ��ȡ��ǰ�ĵ���ջ
 * @param[out] bt �������ջ
 * @param[in] size ��෵�ز���
 * @param[in] skip ����ջ���Ĳ���
 */
void BackTrace(std::vector<std::string> &bt, int size = 64, int skip = 1);

/**
 * @brief ��ȡ��ǰջ��Ϣ���ַ���
 * @param[in] size ջ��������
 * @param[in] skip ����ջ���Ĳ���
 * @param[in] prefix ջ��Ϣǰ���������
 */
std::string BackTraceToString(int size = 64, int skip = 2,
                              const std::string &prefix = "");

/**
 * @brief �ַ���ת��Сд
 */
std::string ToUpper(const std::string &str);
std::string ToLower(const std::string &str);

/**
 * @brief ����ʱ��ת�ַ���
 */
std::string Time2Str(time_t ts = time(0),
                     const std::string &format = "%Y-%m-%d %H:%M:%S");

/**
 * @brief �ַ���ת����ʱ��
 */
time_t Str2Time(const char *str, const char *format = "%Y-%m-%d %H:%M:%S");

/**
 * @brief �ļ�ϵͳ������
 */
class FSUtil {
public:
  /**
   * @brief
   * �ݹ��о�ָ��Ŀ¼������ָ����׺�ĳ����ļ��������ָ����׺������������ļ������ص��ļ�����·��
   * @param[out] files �ļ��б�
   * @param[in] path ·��
   * @param[in] subfix ��׺�������� ".yml"
   */
  static void ListAllFile(std::vector<std::string> &files,
                          const std::string &path, const std::string &subfix);

  /**
   * @brief ����·�����൱��mkdir -p
   * @param[in] dirname ·����
   */
  static bool Mkdir(const std::string &dirname);

  /**
   * @brief �ж�ָ��pid�ļ�ָ����pid�Ƿ��������У�ʹ��kill(pid, 0)�ķ�ʽ�ж�
   * @param[in] pidfile ������̺ŵ��ļ�
   */
  static bool IsRunningPidFile(const std::string &pidfile);

  /**
   * @brief ɾ���ļ���·��
   * @param[in] path �ļ�����·����
   */
  static bool Rm(const std::string &path);

  /**
   * @brief �ƶ��ļ���·�����ڲ�ʵ������Rm(to)����rename(from, to)���ο�rename
   * @param[in] from Դ
   * @param[in] to Ŀ�ĵ�
   */
  static bool Mv(const std::string &from, const std::string &to);

  /**
   * @brief ���ؾ���·�����ο�realpath(3)
   * @details ·���еķ������ӻᱻ������ʵ�ʵ�·����ɾ�������'.' '..'��'/'
   * @param[in] path
   * @param[out] rpath
   */
  static bool RealPath(const std::string &path, std::string &rpath);

  /**
   * @brief �����������ӣ��ο�symlink(2)
   * @param[in] from Ŀ��
   * @param[in] to ����·��
   */
  static bool Symlink(const std::string &from, const std::string &to);

  /**
   * @brief ɾ���ļ�Ŀ¼��ο�unlink(2)
   * @param[in] filename �ļ���
   * @param[in] exist �Ƿ����
   * @note �ڲ����ж�һ���Ƿ���Ĳ����ڸ��ļ�
   */
  static bool Unlink(const std::string &filename, bool exist = false);

  /**
   * @brief
   * �����ļ�����·�������һ��/ǰ��Ĳ��֣�������/�������δ�ҵ����򷵻�filename
   * @param[in] filename �ļ�����·��
   */
  static std::string Dirname(const std::string &filename);

  /**
   * @brief �����ļ�������·�������һ��/����Ĳ���
   * @param[in] filename �ļ�����·��
   */
  static std::string Basename(const std::string &filename);

  /**
   * @brief ��ֻ����ʽ��
   * @param[in] ifs �ļ���
   * @param[in] filename �ļ���
   * @param[in] mode �򿪷�ʽ
   */
  static bool OpenForRead(std::ifstream &ifs, const std::string &filename,
                          std::ios_base::openmode mode);

  /**
   * @brief ��ֻд��ʽ��
   * @param[in] ofs �ļ���
   * @param[in] filename �ļ���
   * @param[in] mode �򿪷�ʽ
   */
  static bool OpenForWrite(std::ofstream &ofs, const std::string &filename,
                           std::ios_base::openmode mode);
};

/**
 * @brief ����ת��
 */
class TypeUtil {
public:
  // ת�ַ�������*str.begin()
  static int8_t ToChar(const std::string &str);

  static int64_t Atoi(const std::string &str);
  static double Atof(const std::string &str);
  // ����str[0]
  static int8_t ToChar(const char *str);

  static int64_t Atoi(const char *str);
  static double Atof(const char *str);
};

/**
 * @brief ��ȡT���͵������ַ���
 */
template <class T> const char *TypeToName() {
  static const char *s_name =
      abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, nullptr);
  return s_name;
}

/**
 * @brief �ַ���������
 */
class StringUtil {
public:
  /**
   * @brief �ַ�����ʽ�������ظ�ʽ�����string
   */
  static std::string Format(const char *fmt, ...);
  static std::string FormatV(const char *fmt, va_list ap);

  /**
   * @brief url����
   * @param[in] str ԭʼ�ַ���
   * @param[in] space_as_plus �Ƿ񽫿ո�����+�ţ����Ϊfalse����ո�����%20
   */
  static std::string UrlEncode(const std::string &str,
                               bool space_as_plus = true);

  /**
   * @brief url����
   * @param[in] str url�ַ���
   * @param[in] space_as_plus �Ƿ�+�Ž���Ϊ�ո�
   */
  static std::string UrlDecode(const std::string &str,
                               bool space_as_plus = true);

  /**
   * @brief �Ƴ��ַ�����β��ָ���ַ���
   * @param[] str �����ַ���
   * @param[] delimit ���Ƴ����ַ���
   */
  static std::string Trim(const std::string &str,
                          const std::string &delimit = " \t\r\n");
  static std::string TrimLeft(const std::string &str,
                              const std::string &delimit = " \t\r\n");
  static std::string TrimRight(const std::string &str,
                               const std::string &delimit = " \t\r\n");

  /**
   * @brief ���ַ���ת�ַ���
   */
  static std::string WStringToString(const std::wstring &ws);

  /**
   * @brief �ַ���ת���ַ���
   */
  static std::wstring StringToWString(const std::string &str);
};

} // namespace zz