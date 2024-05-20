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
 * @brief 获取线程id
 * @note 这里不要把pid_t和pthread_t混淆，关于它们之的区别可参考gettid(2)
 */
pid_t GetThreadId();

/**
 * @brief 获取协程id
 */
uint64_t GetFiberId();

/**
 * @brief 获取当前时间
 */
uint64_t GetCurrentMS();
uint64_t GetCurrentUS();

/**
 * @brief 获取当前的调用栈
 * @param[out] bt 保存调用栈
 * @param[in] size 最多返回层数
 * @param[in] skip 跳过栈顶的层数
 */
void BackTrace(std::vector<std::string> &bt, int size = 64, int skip = 1);

/**
 * @brief 获取当前栈信息的字符串
 * @param[in] size 栈的最大层数
 * @param[in] skip 跳过栈顶的层数
 * @param[in] prefix 栈信息前输出的内容
 */
std::string BackTraceToString(int size = 64, int skip = 2,
                              const std::string &prefix = "");

/**
 * @brief 字符串转大小写
 */
std::string ToUpper(const std::string &str);
std::string ToLower(const std::string &str);

/**
 * @brief 日期时间转字符串
 */
std::string Time2Str(time_t ts = time(0),
                     const std::string &format = "%Y-%m-%d %H:%M:%S");

/**
 * @brief 字符串转日期时间
 */
time_t Str2Time(const char *str, const char *format = "%Y-%m-%d %H:%M:%S");

/**
 * @brief 文件系统操作类
 */
class FSUtil {
public:
  /**
   * @brief
   * 递归列举指定目录下所有指定后缀的常规文件，如果不指定后缀，则遍历所有文件，返回的文件名带路径
   * @param[out] files 文件列表
   * @param[in] path 路径
   * @param[in] subfix 后缀名，比如 ".yml"
   */
  static void ListAllFile(std::vector<std::string> &files,
                          const std::string &path, const std::string &subfix);

  /**
   * @brief 创建路径，相当于mkdir -p
   * @param[in] dirname 路径名
   */
  static bool Mkdir(const std::string &dirname);

  /**
   * @brief 判断指定pid文件指定的pid是否正在运行，使用kill(pid, 0)的方式判断
   * @param[in] pidfile 保存进程号的文件
   */
  static bool IsRunningPidFile(const std::string &pidfile);

  /**
   * @brief 删除文件或路径
   * @param[in] path 文件名或路径名
   */
  static bool Rm(const std::string &path);

  /**
   * @brief 移动文件或路径，内部实现是先Rm(to)，再rename(from, to)，参考rename
   * @param[in] from 源
   * @param[in] to 目的地
   */
  static bool Mv(const std::string &from, const std::string &to);

  /**
   * @brief 返回绝对路径，参考realpath(3)
   * @details 路径中的符号链接会被解析成实际的路径，删除多余的'.' '..'和'/'
   * @param[in] path
   * @param[out] rpath
   */
  static bool RealPath(const std::string &path, std::string &rpath);

  /**
   * @brief 创建符号链接，参考symlink(2)
   * @param[in] from 目标
   * @param[in] to 链接路径
   */
  static bool Symlink(const std::string &from, const std::string &to);

  /**
   * @brief 删除文件目录项，参考unlink(2)
   * @param[in] filename 文件名
   * @param[in] exist 是否存在
   * @note 内部会判断一次是否真的不存在该文件
   */
  static bool Unlink(const std::string &filename, bool exist = false);

  /**
   * @brief
   * 返回文件，即路径中最后一个/前面的部分，不包括/本身，如果未找到，则返回filename
   * @param[in] filename 文件完整路径
   */
  static std::string Dirname(const std::string &filename);

  /**
   * @brief 返回文件名，即路径中最后一个/后面的部分
   * @param[in] filename 文件完整路径
   */
  static std::string Basename(const std::string &filename);

  /**
   * @brief 以只读方式打开
   * @param[in] ifs 文件流
   * @param[in] filename 文件名
   * @param[in] mode 打开方式
   */
  static bool OpenForRead(std::ifstream &ifs, const std::string &filename,
                          std::ios_base::openmode mode);

  /**
   * @brief 以只写方式打开
   * @param[in] ofs 文件流
   * @param[in] filename 文件名
   * @param[in] mode 打开方式
   */
  static bool OpenForWrite(std::ofstream &ofs, const std::string &filename,
                           std::ios_base::openmode mode);
};

/**
 * @brief 类型转换
 */
class TypeUtil {
public:
  // 转字符，返回*str.begin()
  static int8_t ToChar(const std::string &str);

  static int64_t Atoi(const std::string &str);
  static double Atof(const std::string &str);
  // 返回str[0]
  static int8_t ToChar(const char *str);

  static int64_t Atoi(const char *str);
  static double Atof(const char *str);
};

/**
 * @brief 获取T类型的类型字符串
 */
template <class T> const char *TypeToName() {
  static const char *s_name =
      abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, nullptr);
  return s_name;
}

/**
 * @brief 字符串辅助类
 */
class StringUtil {
public:
  /**
   * @brief 字符串格式化，返回格式化后的string
   */
  static std::string Format(const char *fmt, ...);
  static std::string FormatV(const char *fmt, va_list ap);

  /**
   * @brief url编码
   * @param[in] str 原始字符串
   * @param[in] space_as_plus 是否将空格编码成+号，如果为false，则空格编码成%20
   */
  static std::string UrlEncode(const std::string &str,
                               bool space_as_plus = true);

  /**
   * @brief url解码
   * @param[in] str url字符串
   * @param[in] space_as_plus 是否将+号解码为空格
   */
  static std::string UrlDecode(const std::string &str,
                               bool space_as_plus = true);

  /**
   * @brief 移除字符串首尾的指定字符串
   * @param[] str 输入字符串
   * @param[] delimit 待移除的字符串
   */
  static std::string Trim(const std::string &str,
                          const std::string &delimit = " \t\r\n");
  static std::string TrimLeft(const std::string &str,
                              const std::string &delimit = " \t\r\n");
  static std::string TrimRight(const std::string &str,
                               const std::string &delimit = " \t\r\n");

  /**
   * @brief 宽字符串转字符串
   */
  static std::string WStringToString(const std::wstring &ws);

  /**
   * @brief 字符串转宽字符串
   */
  static std::wstring StringToWString(const std::string &str);
};

} // namespace zz