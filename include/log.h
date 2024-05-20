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
 * @brief ��ȡָ�����Ƶ���־��
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
   * @brief ��־���� -> �����ַ�������
   * @param[in] level ��־����
   * @return �ַ�����ʽ����־����
   */
  static const char *ToString(LogLevel::Level level);

  /**
   * @brief �����ַ��� -> ��־����
   * @param[in] str �ַ���
   * @return ��־����
   * @note �����ִ�Сд
   */
  static LogLevel::Level FromString(const std::string &str);
};

// ��־�¼���Ϣ��������ӡ��־ʱ����ĸ�����Ϣ(��¼��־�ֳ�)
class LogEvent {
public:
  using ptr = std::shared_ptr<LogEvent>;
  /**
   * @param[in] loggername ��־��
   * @param[in] level ��־����
   * @param[in] filename �ļ���
   * @param[in] line �к�
   * @param[in] elapse ����־��������ʼ����ǰ���ۼ����к���
   * @param[in] thead_id �߳�id
   * @param[in] thread_name �߳�����
   * @param[in] fiber_id Э��id
   * @param[in] time UTCʱ��
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

  // ��־�������Ϣ
  std::string getLoggerName() const { return m_loggername; }
  LogLevel::Level getLevel() const { return m_level; }

  /**
   * @brief ��ȡ�����ֽ�����������ʽд����־
   */
  std::stringstream &getSS() { return m_ss; }
  std::string getContent() const { return m_ss.str(); }
  void formatPrint(const char *fmt, ...);
  void formatPrint(const char *fmt, va_list ap);

private:
  const char *m_filename;   // �ļ���
  uint32_t m_line = 0;      // �к�
  uint32_t m_threadid = 0;  // �߳�id
  std::string m_threadname; // �߳���
  uint32_t m_fiberid = 0;   // Э��id
  time_t m_time = 0;        // ��ǰʱ���
  uint32_t m_elapse = 0;

  std::string m_loggername; // ��־������
  LogLevel::Level m_level;  // ��־����
  std::stringstream m_ss;   // ��־���� -- ��
};

// ��־��ʽ��������־�¼���ʽ�����ַ���
class LogFormatter {
public:
  using ptr = std::shared_ptr<LogFormatter>;
  LogFormatter(const std::string &pattern);

  /**
   * @brief ��ʼ����������ʽģ�壬��ȡģ����
   */
  void init();

  bool isError() const { return m_error; }
  const std::string getPattern() const { return m_pattern; }

  /**
   * @brief ����־�¼����и�ʽ�������ظ�ʽ����־�ַ���
   * @param[in] event ��־�¼�
   * @return ��ʽ����־�ַ���
   */
  std::string format_str(LogEvent::ptr event);

  /**
   * @brief ����־�¼����и�ʽ�������ظ�ʽ����־��
   * @param[in] event ��־�¼�
   * @param[in] os ��־�����
   * @return ��ʽ����־��
   */
  std::ostream &format_ss(std::ostream &os, LogEvent::ptr event);

public:
  /**
   * @brief ��־�¼��еĸ�ʽ����(ÿһ��������Ϣ)������ģʽ
   */
  class FormatItem {
  public:
    using ptr = std::shared_ptr<FormatItem>;
    virtual ~FormatItem();
    virtual void format_ss(std::ostream &os, LogEvent::ptr event) = 0;
  };

private:
  std::string m_pattern;
  std::vector<FormatItem::ptr> m_items; // ����������־��ʽ����
  bool m_error = false;
};

// ��־�����(����־��ʽ����)
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

  // �麯������������ؼ̳�ʵ�ָ÷���
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

// ��־��(����־�����)����־����ְ�����־��ʽ�������Ӷ���־����������־��ʽ��������־�����
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

  // ������־�¼�������־���
  void log(LogEvent::ptr event);
  std::string toYamlString();

private:
  MutexType m_mutex;
  std::string m_name;
  LogLevel::Level m_level;
  std::list<LogAppender::ptr> m_appenders; // ����ؼ���
  LogFormatter::ptr m_formatter;
  Logger::ptr m_root; // ��root logger
};

// ��װ��־�¼�����־����һ����־ֻ��Ӧһ����־����������ʱ������־�������־
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

// �������е���־��
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

// ������־������
using LoggerMgr = zz::Singleton<LoggerManager>;

/*
��������
1.��ʼ��LogFormatter
2.��ʼ��LogAppender(�󶨳�ʼ����LogFormatter)
3.��ʼ��Logger(�󶨳�ʼ����LogAppender)
4.��ʼ��LogEventWrap����װLogEvent��Logger
5.LogEventWrap����ʱ������Logger��log���������־
*/

} // namespace zz