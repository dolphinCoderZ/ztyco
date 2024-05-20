#include "log.h"
#include "config.h"
#include "env.h"
#include "mutex.h"
#include "util.h"
#include <boost/lexical_cast.hpp>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <functional>
#include <iostream>
#include <map>
#include <ostream>
#include <sstream>
#include <utility>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace zz {

const char *LogLevel::ToString(LogLevel::Level level) {
  switch (level) {
#define XX(name)                                                               \
  case LogLevel::name:                                                         \
    return #name; // 预处理器操作符，将参数转化为字符串常量

    XX(DEBUG) // XX展开为define的代码
    XX(INFO)
    XX(WARN)
    XX(ERROR)
    XX(FATAL)
#undef XX
  default:
    return "UNKNOW";
  }
  return "UNKNOW";
}

LogLevel::Level LogLevel::FromString(const std::string &str) {
#define XX(level, v)                                                           \
  if (str == #v) {                                                             \
    return LogLevel::level;                                                    \
  }
  // 不区分大小写
  XX(DEBUG, debug);
  XX(INFO, info);
  XX(WARN, warn);
  XX(ERROR, error);
  XX(FATAL, fatal);

  XX(DEBUG, DEBUG);
  XX(INFO, INFO);
  XX(WARN, WARN);
  XX(ERROR, ERROR);
  XX(FATAL, FATAL);
  return LogLevel::UNKNOW;
#undef XX
}

void LogEvent::formatPrint(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

void LogEvent::formatPrint(const char *fmt, va_list ap) {
  char *buf = nullptr;
  int len = vasprintf(&buf, fmt, ap);
  if (len != -1) {
    m_ss << std::string(buf, len);
    free(buf);
  }
}

// 工厂模式创建格式化日志项
// 日志消息项
class MessageFormatItem : public LogFormatter::FormatItem {
public:
  MessageFormatItem(const std::string &str) {}
  void format_ss(std::ostream &os, LogEvent::ptr event) override {
    os << event->getContent();
  }
};

// 日志级别项
class LevelFormatItem : public LogFormatter::FormatItem {
public:
  LevelFormatItem(const std::string &str) {}
  void format_ss(std::ostream &os, LogEvent::ptr event) override {
    os << LogLevel::ToString(event->getLevel());
  }
};

// 日志器名字项
class LoggerNameFormatItem : public LogFormatter::FormatItem {
public:
  LoggerNameFormatItem(const std::string &str) {}
  void format_ss(std::ostream &os, LogEvent::ptr event) override {
    os << event->getLoggerName();
  }
};

// 线程id项
class ThreadIdFormatItem : public LogFormatter::FormatItem {
public:
  ThreadIdFormatItem(const std::string &str) {}
  void format_ss(std::ostream &os, LogEvent::ptr event) override {
    os << event->getThreadId();
  }
};

// 线程名字项
class ThreadNameFormatItem : public LogFormatter::FormatItem {
public:
  ThreadNameFormatItem(const std::string &str) {}
  void format_ss(std::ostream &os, LogEvent::ptr event) override {
    os << event->getThreadName();
  }
};

// 协程id项
class FiberIdFormatItem : public LogFormatter::FormatItem {
public:
  FiberIdFormatItem(const std::string &str) {}
  void format_ss(std::ostream &os, LogEvent::ptr event) override {
    os << event->getFiberId();
  }
};

// 时间戳项
class DateTimeFormatItem : public LogFormatter::FormatItem {
public:
  DateTimeFormatItem(const std::string &format = "%Y-%m-%d %H:%M:%S")
      : m_format(format) {
    if (m_format.empty()) {
      m_format = "%Y-%m-%d %H:%M:%S";
    }
  }

  void format_ss(std::ostream &os, LogEvent::ptr event) override {
    struct tm t;
    time_t time = event->getTime();
    localtime_r(&time, &t);
    char buf[64];
    strftime(buf, sizeof(buf), m_format.c_str(), &t);
    os << buf;
  }

private:
  std::string m_format;
};

// 文件名项
class FileNameFormatItem : public LogFormatter::FormatItem {
public:
  FileNameFormatItem(const std::string &str) {}
  void format_ss(std::ostream &os, LogEvent::ptr event) override {
    os << event->getFileName();
  }
};

// 行号项
class LineFormatItem : public LogFormatter::FormatItem {
public:
  LineFormatItem(const std::string &str) {}
  void format_ss(std::ostream &os, LogEvent::ptr event) override {
    os << event->getLine();
  }
};

// 换行项
class NewLineFormatItem : public LogFormatter::FormatItem {
public:
  NewLineFormatItem(const std::string &str) {}
  void format_ss(std::ostream &os, LogEvent::ptr event) override {
    os << std::endl;
  }
};

// 字符串项
class StringFormatItem : public LogFormatter ::FormatItem {
public:
  StringFormatItem(const std::string &str) : m_str(str) {}
  void format_ss(std::ostream &os, LogEvent::ptr event) override {
    os << m_str;
  }

private:
  std::string m_str;
};

// tab项
class TabFormatItem : public LogFormatter::FormatItem {
public:
  TabFormatItem(const std::string &str) {}
  void format_ss(std::ostream &os, LogEvent::ptr event) override { os << "\t"; }
};

// %项
class PercentSignFormat : public LogFormatter::FormatItem {
public:
  PercentSignFormat(const std::string &str) {}
  void format_ss(std::ostream &os, LogEvent::ptr event) override { os << "%"; }
};

std::string LogFormatter::format_str(LogEvent::ptr event) {
  std::stringstream ss;
  for (auto &it : m_items) {
    it->format_ss(ss, event);
  }

  return ss.str();
}

std::ostream &LogFormatter::format_ss(std::ostream &os, LogEvent::ptr event) {
  for (auto &it : m_items) {
    it->format_ss(os, event);
  }
  return os;
}

LogFormatter::LogFormatter(const std::string &pattern) : m_pattern(pattern) {
  init();
}

/*
  状态机判断，提取pattern中的常规字符和模式字符
  从头到尾遍历，根据状态标志决定当前字符是常规字符还是模式字符
  两种状态：正在解析常规字符和正在解析模式转义字符
*/
void LogFormatter::init() {
  //<整数类型，字符串>，整数0表示常规字符，1表示需要转义
  std::vector<std::pair<int, std::string>> vec;
  // 临时存储常规字符串
  std::string tmp;
  // 日期格式字符串
  std::string dateFormat;
  bool error = false;

  bool parsing_string = true;
  size_t i = 0;
  while (i < m_pattern.size()) {
    std::string c = std::string(1, m_pattern[i]);
    if (c == "%") {
      if (parsing_string) { // 解析常规字符时碰到%，说明要开始解析模式字符
        if (!tmp.empty()) {
          // 记录当前常规字符
          vec.push_back(std::make_pair(0, tmp));
        }
        tmp.clear();
        parsing_string = false;
        i++;
        continue;
      } else { // 正在解析模式字符(已经遇到%)，再次遇到%，说明是个转义%
        vec.push_back(std::make_pair(1, c));
        parsing_string = true;
        i++;
        continue;
      }
    } else { // 不是%
      if (parsing_string) {
        tmp += c;
        i++;
        continue;
      } else {
        vec.push_back(std::make_pair(1, c)); // 遇到模式字符
        parsing_string = true;

        if (c != "d") {
          i++;
          continue;
        }

        i++; // 查看d的后一位是否为{
        if (i < m_pattern.size() && m_pattern[i] != '{') {
          continue;
        }

        // d的后一位是{
        i++; // 跳过{
        while (i < m_pattern.size() && m_pattern[i] != '}') {
          dateFormat.push_back(m_pattern[i]);
          i++;
        }
        if (m_pattern[i] != '}') {
          std::cout << "[ERROR] LogFormat::init() " << "pattern:[" << m_pattern
                    << "] '{' not closed" << std::endl;
          error = true;
          break;
        }
        i++;
        continue;
      }
    }
  }

  if (error) {
    m_error = true;
    return;
  }

  if (!tmp.empty()) {
    vec.push_back(std::make_pair(0, tmp));
    tmp.clear();
  }

  static std::map<std::string,
                  std::function<FormatItem::ptr(const std::string &str)>>
      s_format_items = {
#define XX(str, C)                                                             \
  {                                                                            \
    #str, [](const std::string &fmt) { return FormatItem::ptr(new C(fmt)); }   \
  }

          XX(m, MessageFormatItem),
          XX(p, LevelFormatItem),
          XX(c, LoggerNameFormatItem),
          XX(t, ThreadIdFormatItem),
          XX(N, ThreadNameFormatItem),
          XX(n, NewLineFormatItem),
          XX(d, DateTimeFormatItem),
          XX(f, FileNameFormatItem),
          XX(l, LineFormatItem),
          XX(T, TabFormatItem),
          XX(F, FiberIdFormatItem),
          XX(%, PercentSignFormat)
#undef XX
      };

  // 遍历解析结果，注册相应的格式化item
  for (auto &v : vec) {
    if (v.first == 0) {
      m_items.push_back(FormatItem::ptr(new StringFormatItem(v.second)));
    } else if (v.second == "d") {
      m_items.push_back(FormatItem::ptr(new DateTimeFormatItem(dateFormat)));
    } else {
      auto it = s_format_items.find(v.second);
      if (it == s_format_items.end()) {
        std::cout << "[ERROR] LogFormatter::init() " << "pattern:[" << m_pattern
                  << "] " << "unknow format item: " << v.second << std::endl;
        error = true;
        break;
      } else {
        m_items.push_back(it->second(v.second));
      }
    }
  }

  if (error) {
    m_error = true;
    return;
  }
}

void LogAppender::setFormatter(LogFormatter::ptr val) {
  MutexType::Lock lock(m_mutex);
  m_formatter = val;
  if (m_formatter) {
    m_hasFormatter = true;
  }
}

LogFormatter::ptr LogAppender::getFormatter() {
  MutexType::Lock lock(m_mutex);
  return m_formatter;
}

void StdoutLogAppender::log(LogEvent::ptr event) {
  MutexType::Lock lock(m_mutex);
  m_formatter->format_ss(std::cout, event);
}

std::string StdoutLogAppender::toYamlString() {
  MutexType::Lock lock(m_mutex);
  YAML::Node node;
  node["type"] = "StdoutLogAppender";
  node["pattern"] = m_formatter->getPattern();
  std::stringstream ss;
  ss << node;
  return ss.str();
}

FileLogAppender::FileLogAppender(const std::string &file) : m_filename(file) {
  reopen();
}

bool FileLogAppender::reopen() {
  MutexType::Lock lock(m_mutex);
  if (m_filestream) {
    m_filestream.close();
  }
  return zz::FSUtil::OpenForWrite(m_filestream, m_filename, std::ios::app);
}

void FileLogAppender::log(LogEvent::ptr event) {
  uint64_t now = event->getTime();
  if (now >= m_lastTime + 3) {
    reopen();
    m_lastTime = now;
  }

  MutexType::Lock lock(m_mutex);
  if (!m_formatter->format_ss(m_filestream, event)) {
    std::cout << "[ERROR] FileLogAppender::log() format error" << std::endl;
  }
}

std::string FileLogAppender::toYamlString() {
  MutexType::Lock lock(m_mutex);
  YAML::Node node;
  node["type"] = "FileLogAppender";
  node["file"] = m_filename;
  node["pattern"] = m_formatter->getPattern();
  std::stringstream ss;
  ss << node;
  return ss.str();
}

Logger::Logger(const std::string &name)
    : m_name(name), m_level(LogLevel::DEBUG) {
  m_formatter.reset(new LogFormatter(
      "%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"));
}

void Logger::setFormatter(LogFormatter::ptr val) {
  MutexType::Lock lock(m_mutex);
  m_formatter = val;

  for (auto &it : m_appenders) {
    MutexType::Lock lk(it->m_mutex);
    if (!it->m_hasFormatter) {
      it->m_formatter = m_formatter;
    }
  }
}

void Logger::setFormatter(const std::string val) {
  std::cout << "------" << val << std::endl;
  zz::LogFormatter::ptr handleVal(new zz::LogFormatter(val));
  if (handleVal->isError()) {
    std::cout << "[ERROR] Logger setFormatter name=" << m_name
              << " value=" << val << " invalid formatter" << std::endl;
    return;
  }
  setFormatter(handleVal);
}

std::string Logger::toYamlString() {
  MutexType::Lock lock(m_mutex);
  YAML::Node node;
  node["name"] = m_name;
  if (m_level != LogLevel::UNKNOW) {
    node["level"] = LogLevel::ToString(m_level);
  }
  if (m_formatter) {
    node["formatter"] = m_formatter->getPattern();
  }

  for (auto &i : m_appenders) {
    node["appenders"].push_back(YAML::Load(i->toYamlString()));
  }
  std::stringstream ss;
  ss << node;
  return ss.str();
}

LogFormatter::ptr Logger::getFormatter() {
  MutexType::Lock lock(m_mutex);
  return m_formatter;
}

void Logger::addAppender(LogAppender::ptr appender) {
  MutexType::Lock lock(m_mutex);
  if (!appender->getFormatter()) {
    MutexType::Lock lk(appender->m_mutex);
    appender->setFormatter(m_formatter);
  }
  m_appenders.push_back(appender);
}

void Logger::delAppender(LogAppender::ptr appender) {
  MutexType::Lock lock(m_mutex);
  for (auto it = m_appenders.begin(); it != m_appenders.end(); ++it) {
    if (*it == appender) {
      m_appenders.erase(it);
      break;
    }
  }
}

void Logger::clearAppender() {
  MutexType::Lock lock(m_mutex);
  m_appenders.clear();
}

void Logger::log(LogEvent::ptr event) {
  auto self = shared_from_this();
  MutexType::Lock lock(m_mutex);
  if (!m_appenders.empty()) {
    for (auto &it : m_appenders) {
      it->log(event);
    }
  } else if (m_root) {
    m_root->log(event);
  }
}

// 在LogEventWrap析构时写入日志
LogEventWrap::~LogEventWrap() { m_logger->log(m_event); }

LoggerManager::LoggerManager() {
  // 设置root logger
  m_root.reset(new Logger);
  m_root->addAppender(LogAppender::ptr(new StdoutLogAppender));
  m_loggers[m_root->getLoggerName()] = m_root;

  init();
}

Logger::ptr LoggerManager::getLogger(const std::string &name) {
  MutexType::Lock lock(m_mutex);
  auto it = m_loggers.find(name);
  if (it != m_loggers.end()) {
    return it->second;
  }

  // 没有就新构建logger
  Logger::ptr logger(new Logger(name));
  logger->m_root = m_root;
  m_loggers[name] = logger;
  return logger;
}

void init() {}

std::string LoggerManager::toYamlString() {
  MutexType::Lock lock(m_mutex);
  YAML::Node node;
  for (auto &i : m_loggers) {
    node.push_back(YAML::Load(i.second->toYamlString()));
  }
  std::stringstream ss;
  ss << node;
  return ss.str();
}

///////////////////////////////////////////////
// 从配置文件中加载日志配置

// 日志输出器配置结构体
struct LogAppenderDefine {
  int type = 0; // 1：file  2：stdout
  LogLevel::Level level = LogLevel::UNKNOW;
  std::string formatter;
  std::string file;

  bool operator==(const LogAppenderDefine &other) const {
    return type == other.type && level == other.level &&
           formatter == other.formatter && file == other.file;
  }
};

// 日志配置结构体
struct LogDefine {
  std::string name;
  LogLevel::Level level = LogLevel::UNKNOW;
  std::string formatter;
  std::vector<LogAppenderDefine> appenders;

  bool operator==(const LogDefine &other) const {
    return name == other.name && level == other.level &&
           formatter == other.formatter && appenders == other.appenders;
  }

  bool operator<(const LogDefine &other) const { return name < other.name; }
  bool isValid() const { return !name.empty(); }
};

template <> class LexicalCast<std::string, LogDefine> {
public:
  LogDefine operator()(const std::string &v) {
    YAML::Node n = YAML::Load(v);
    LogDefine ld;
    if (!n["name"].IsDefined()) {
      std::cout << "log config error: name is null, " << n << std::endl;
      throw std::logic_error("log config name is null");
    }
    ld.name = n["name"].as<std::string>();
    ld.level = LogLevel::FromString(
        n["level"].IsDefined() ? n["level"].as<std::string>() : "");
    if (n["formatter"].IsDefined()) {
      ld.formatter = n["formatter"].as<std::string>();
    }

    if (n["appenders"].IsDefined()) {
      // std::cout << "==" << ld.name << " = " << n["appenders"].size() <<
      // std::endl;
      for (size_t x = 0; x < n["appenders"].size(); ++x) {
        auto a = n["appenders"][x];
        if (!a["type"].IsDefined()) {
          std::cout << "log config error: appender type is null, " << a
                    << std::endl;
          continue;
        }
        std::string type = a["type"].as<std::string>();
        LogAppenderDefine lad;
        if (type == "FileLogAppender") {
          lad.type = 1;
          if (!a["file"].IsDefined()) {
            std::cout << "log config error: fileappender file is null, " << a
                      << std::endl;
            continue;
          }
          lad.file = a["file"].as<std::string>();
          if (a["formatter"].IsDefined()) {
            lad.formatter = a["formatter"].as<std::string>();
          }
        } else if (type == "StdoutLogAppender") {
          lad.type = 2;
          if (a["formatter"].IsDefined()) {
            lad.formatter = a["formatter"].as<std::string>();
          }
        } else {
          std::cout << "log config error: appender type is invalid, " << a
                    << std::endl;
          continue;
        }

        ld.appenders.push_back(lad);
      }
    }
    return ld;
  }
};

template <> class LexicalCast<LogDefine, std::string> {
public:
  std::string operator()(const LogDefine &i) {
    YAML::Node n;
    n["name"] = i.name;
    if (i.level != LogLevel::UNKNOW) {
      n["level"] = LogLevel::ToString(i.level);
    }
    if (!i.formatter.empty()) {
      n["formatter"] = i.formatter;
    }

    for (auto &a : i.appenders) {
      YAML::Node na;
      if (a.type == 1) {
        na["type"] = "FileLogAppender";
        na["file"] = a.file;
      } else if (a.type == 2) {
        na["type"] = "StdoutLogAppender";
      }
      if (a.level != LogLevel::UNKNOW) {
        na["level"] = LogLevel::ToString(a.level);
      }

      if (!a.formatter.empty()) {
        na["formatter"] = a.formatter;
      }

      n["appenders"].push_back(na);
    }
    std::stringstream ss;
    ss << n;
    return ss.str();
  }
};

zz::ConfigVar<std::set<LogDefine>>::ptr g_log_defines =
    zz::Config::Lookup("logs", std::set<LogDefine>(), "logs config");

struct LogIniter {
  LogIniter() {
    g_log_defines->addListener([](const std::set<LogDefine> &old_value,
                                  const std::set<LogDefine> &new_value) {
      ZZ_LOG_INFO(ZZ_LOG_ROOT()) << "on_logger_conf_changed";
      for (auto &i : new_value) {
        auto it = old_value.find(i);
        zz::Logger::ptr logger;
        if (it == old_value.end()) {
          // 新增logger
          logger = ZZ_LOG_NAME(i.name);
        } else {
          if (!(i == *it)) {
            // 修改的logger
            logger = ZZ_LOG_NAME(i.name);
          } else {
            continue;
          }
        }
        logger->setLevel(i.level);

        if (!i.formatter.empty()) {
          logger->setFormatter(i.formatter);
        }

        logger->clearAppender();
        for (auto &a : i.appenders) {
          zz::LogAppender::ptr ap;
          if (a.type == 1) {
            ap.reset(new FileLogAppender(a.file));
          } else if (a.type == 2) {
            if (!zz::EnvMgr::GetInstance()->has("d")) {
              ap.reset(new StdoutLogAppender);
            } else {
              continue;
            }
          }
          ap->setLevel(a.level);
          if (!a.formatter.empty()) {
            LogFormatter::ptr fmt(new LogFormatter(a.formatter));
            if (!fmt->isError()) {
              ap->setFormatter(fmt);
            } else {
              std::cout << "log.name=" << i.name << " appender type=" << a.type
                        << " formatter=" << a.formatter << " is invalid"
                        << std::endl;
            }
          }
          logger->addAppender(ap);
        }
      }

      for (auto &i : old_value) {
        auto it = new_value.find(i);
        if (it == new_value.end()) {
          // 删除logger
          auto logger = ZZ_LOG_NAME(i.name);
          logger->setLevel((LogLevel::Level)0);
          logger->clearAppender();
        }
      }
    });
  }
};

static LogIniter __log_init;

} // namespace zz