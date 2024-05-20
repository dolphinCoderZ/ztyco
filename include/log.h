#pragma once

#include "mutex.h"
#include "singleton.h"
#include <cstdarg>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <list>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#define ZZ_LOG_ROOT() zz::LoggerMgr::GetInstance()->getRoot()
/**
 * @brief 获取指定名称的日志器
 */
#define ZZ_LOG_NAME(name) zz::LoggerMgr::GetInstance()->getLogger(name)

#define ZZ_LOG_LEVEL(logger, level)                                            \
  if (logger->getLevel() <= level)                                             \
  zz::LogEventWrap(logger, zz::LogEvent::ptr(new zz::LogEvent(                 \
                               logger->getLoggerName(), level, __FILE__,       \
                               __LINE__, 0, time(nullptr), zz::GetThreadId(),  \
                               zz::Thread::GetName(), zz::GetFiberId())))      \
      .getEvent()                                                              \
      ->getSS()

#define ZZ_LOG_DEBUG(logger) ZZ_LOG_LEVEL(logger, zz::LogLevel::DEBUG)
#define ZZ_LOG_INFO(logger) ZZ_LOG_LEVEL(logger, zz::LogLevel::INFO)
#define ZZ_LOG_WARN(logger) ZZ_LOG_LEVEL(logger, zz::LogLevel::WARN)
#define ZZ_LOG_ERROR(logger) ZZ_LOG_LEVEL(logger, zz::LogLevel::ERROR)
#define ZZ_LOG_FATAL(logger) ZZ_LOG_LEVEL(logger, zz::LogLevel::FATAL)

#define ZZ_LOG_FMT_LEVEL(logger, level, fmt, ...)                              \
  if (logger->getLevel() <= level)                                             \
  zz::LogEventWrap(logger, zz::LogEvent::ptr(new zz::LogEvent(                 \
                               logger->getLoggerName(), level, __FILE__,       \
                               __LINE__, 0, time(nullptr), zz::GetThreadId(),  \
                               zz::Thread::GetName(), zz::GetFiberId())))      \
      .getEvent()                                                              \
      ->formatPrint(fmt, __VA_ARGS__)

#define ZZ_LOG_FMT_DEBUG(logger, fmt, ...)                                     \
  ZZ_LOG_FMT_LEVEL(logger, zz::LogLevel::DEBUG, fmt, __VA_ARGS__)

#define ZZ_LOG_FMT_INFO(logger, fmt, ...)                                      \
  ZZ_LOG_FMT_LEVEL(logger, zz::LogLevel::INFO, fmt, __VA_ARGS__)

#define ZZ_LOG_FMT_WARN(logger, fmt, ...)                                      \
  ZZ_LOG_FMT_LEVEL(logger, zz::LogLevel::WARN, fmt, __VA_ARGS__)

#define ZZ_LOG_FMT_ERROR(logger, fmt, ...)                                     \
  ZZ_LOG_FMT_LEVEL(logger, zz::LogLevel::ERROR, fmt, __VA_ARGS__)

#define ZZ_LOG_FMT_FATAL(logger, fmt, ...)                                     \
  ZZ_LOG_FMT_LEVEL(logger, zz::LogLevel::FATAL, fmt, __VA_ARGS__)

namespace zz {

class LogLevel {
public:
  enum Level {
    UNKNOW = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5
  };
  /**
   * @brief 日志级别 -> 级别字符串描述
   * @param[in] level 日志级别
   * @return 字符串形式的日志级别
   */
  static const char *ToString(LogLevel::Level level);

  /**
   * @brief 级别字符串 -> 日志级别
   * @param[in] str 字符串
   * @return 日志级别
   * @note 不区分大小写
   */
  static LogLevel::Level FromString(const std::string &str);
};

// 日志事件信息，包括打印日志时所需的各种信息(记录日志现场)
class LogEvent {
public:
  using ptr = std::shared_ptr<LogEvent>;
  /**
   * @param[in] loggername 日志器
   * @param[in] level 日志级别
   * @param[in] filename 文件名
   * @param[in] line 行号
   * @param[in] elapse 从日志器创建开始到当前的累计运行毫秒
   * @param[in] thead_id 线程id
   * @param[in] thread_name 线程名称
   * @param[in] fiber_id 协程id
   * @param[in] time UTC时间
   */
  LogEvent(std::string loggername, LogLevel::Level level, const char *filename,
           uint32_t line, uint32_t elapse, uint64_t time, uint32_t threadid,
           const std::string &threadname, uint32_t fiberid)
      : m_filename(filename), m_line(line), m_threadid(threadid),
        m_threadname(threadname), m_fiberid(fiberid), m_time(time),
        m_loggername(loggername), m_level(level) {}

  std::string getFileName() const { return m_filename; }
  uint32_t getLine() const { return m_line; }

  uint32_t getThreadId() const { return m_threadid; }
  const std::string getThreadName() const { return m_threadname; }
  uint32_t getFiberId() const { return m_fiberid; }
  time_t getTime() const { return m_time; }

  // 日志器相关信息
  std::string getLoggerName() const { return m_loggername; }
  LogLevel::Level getLevel() const { return m_level; }

  /**
   * @brief 获取内容字节流，用于流式写入日志
   */
  std::stringstream &getSS() { return m_ss; }
  std::string getContent() const { return m_ss.str(); }
  void formatPrint(const char *fmt, ...);
  void formatPrint(const char *fmt, va_list ap);

private:
  const char *m_filename;   // 文件名
  uint32_t m_line = 0;      // 行号
  uint32_t m_threadid = 0;  // 线程id
  std::string m_threadname; // 线程名
  uint32_t m_fiberid = 0;   // 协程id
  time_t m_time = 0;        // 当前时间戳
  uint32_t m_elapse = 0;

  std::string m_loggername; // 日志器名字
  LogLevel::Level m_level;  // 日志级别
  std::stringstream m_ss;   // 日志内容 -- 流
};

// 日志格式化，将日志事件格式化成字符串
class LogFormatter {
public:
  using ptr = std::shared_ptr<LogFormatter>;
  LogFormatter(const std::string &pattern);

  /**
   * @brief 初始化，解析格式模板，提取模板项
   */
  void init();

  bool isError() const { return m_error; }
  const std::string getPattern() const { return m_pattern; }

  /**
   * @brief 对日志事件进行格式化，返回格式化日志字符串
   * @param[in] event 日志事件
   * @return 格式化日志字符串
   */
  std::string format_str(LogEvent::ptr event);

  /**
   * @brief 对日志事件进行格式化，返回格式化日志流
   * @param[in] event 日志事件
   * @param[in] os 日志输出流
   * @return 格式化日志流
   */
  std::ostream &format_ss(std::ostream &os, LogEvent::ptr event);

public:
  /**
   * @brief 日志事件中的格式化项(每一个具体信息)，工厂模式
   */
  class FormatItem {
  public:
    using ptr = std::shared_ptr<FormatItem>;
    virtual ~FormatItem();
    virtual void format_ss(std::ostream &os, LogEvent::ptr event) = 0;
  };

private:
  std::string m_pattern;
  std::vector<FormatItem::ptr> m_items; // 保存所有日志格式化项
  bool m_error = false;
};

// 日志输出地(绑定日志格式化器)
class LogAppender {
  friend class Logger;

public:
  using ptr = std::shared_ptr<LogAppender>;
  using MutexType = Spinlock;
  virtual ~LogAppender() {}

  LogLevel::Level getLevel() const { return m_level; }
  void setLevel(LogLevel::Level level) { m_level = level; }

  void setFormatter(LogFormatter::ptr val);
  LogFormatter::ptr getFormatter();

  // 虚函数，具体输出地继承实现该方法
  virtual void log(LogEvent::ptr event) = 0;
  virtual std::string toYamlString() = 0;

protected:
  LogLevel::Level m_level = LogLevel::DEBUG;
  MutexType m_mutex;
  LogFormatter::ptr m_formatter;
  bool m_hasFormatter = false;
};
class StdoutLogAppender : public LogAppender {
public:
  using ptr = std::shared_ptr<StdoutLogAppender>;

  void log(LogEvent::ptr event) override;
  std::string toYamlString() override;
};
class FileLogAppender : public LogAppender {
public:
  using ptr = std::shared_ptr<FileLogAppender>;

  FileLogAppender(const std::string &file);
  bool reopen();

  void log(LogEvent::ptr event) override;
  std::string toYamlString() override;

private:
  std::string m_filename;
  std::ofstream m_filestream;
  uint64_t m_lastTime = 0;
};

// 日志器(绑定日志输出地)，日志输出又绑定了日志格式化器，从而日志器就有了日志格式化器和日志输出地
class Logger : public std::enable_shared_from_this<Logger> {
  friend class LoggerManager;

public:
  using ptr = std::shared_ptr<Logger>;
  using MutexType = Spinlock;

  Logger(const std::string &name = "root");

  const std::string getLoggerName() const { return m_name; }
  void setLevel(LogLevel::Level level) { m_level = level; }
  LogLevel::Level getLevel() const { return m_level; }

  LogFormatter::ptr getFormatter();
  void setFormatter(LogFormatter::ptr val);
  void setFormatter(const std::string val);

  void addAppender(LogAppender::ptr appender);
  void delAppender(LogAppender::ptr appender);
  void clearAppender();

  // 传入日志事件，将日志输出
  void log(LogEvent::ptr event);
  std::string toYamlString();

private:
  MutexType m_mutex;
  std::string m_name;
  LogLevel::Level m_level;
  std::list<LogAppender::ptr> m_appenders; // 输出地集合
  LogFormatter::ptr m_formatter;
  Logger::ptr m_root; // 带root logger
};

// 包装日志事件和日志器，一条日志只对应一个日志器，在析构时利用日志器输出日志
class LogEventWrap {
public:
  LogEventWrap(Logger::ptr logger, LogEvent::ptr evt)
      : m_logger(logger), m_event(evt) {}
  ~LogEventWrap();

  LogEvent::ptr getEvent() const { return m_event; }

private:
  Logger::ptr m_logger;
  LogEvent::ptr m_event;
};

// 管理所有的日志器
class LoggerManager {
public:
  using MutexType = Spinlock;
  LoggerManager();
  void init();

  Logger::ptr getLogger(const std::string &name);
  Logger::ptr getRoot() const { return m_root; }

  std::string toYamlString();

private:
  MutexType m_mutex;
  std::map<std::string, Logger::ptr> m_loggers;
  Logger::ptr m_root;
};

// 单例日志管理类
using LoggerMgr = zz::Singleton<LoggerManager>;

/*
工作流程
1.初始化LogFormatter
2.初始化LogAppender(绑定初始化的LogFormatter)
3.初始化Logger(绑定初始化的LogAppender)
4.初始化LogEventWrap，包装LogEvent和Logger
5.LogEventWrap析构时，调用Logger的log方法输出日志
*/

} // namespace zz