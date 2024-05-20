#include "util.h"
#include "fiber.h"
#include "log.h"
#include "thread.h"
#include <algorithm>
#include <bits/types/struct_timeval.h>
#include <cctype>
#include <csignal>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <execinfo.h>
#include <fstream>
#include <pthread.h>
#include <sstream>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace zz {

static zz::Logger::ptr g_logger = ZZ_LOG_NAME("system");

pid_t GetThreadId() { return syscall(SYS_gettid); }

uint64_t GetFiberId() { return zz::Fiber::GetFiberId(); }

uint64_t GetCurrentMS() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec * 1000ul + tv.tv_usec / 1000;
}

uint64_t GetCurrentUS() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec * 1000 * 1000ul + tv.tv_usec;
}

// BackTrace的辅助函数
static std::string demangle(const char *str) {
  size_t size = 0;
  int status = 0;
  std::string rt;
  rt.resize(256);
  if (1 == sscanf(str, "%*[^(]%*[^_]%255[^)+]", &rt[0])) {
    char *v = abi::__cxa_demangle(&rt[0], nullptr, &size, &status);
    if (v) {
      std::string result(v);
      free(v);
      return result;
    }
  }
  if (1 == sscanf(str, "%255s", &rt[0])) {
    return rt;
  }
  return str;
}

void BackTrace(std::vector<std::string> &bt, int size, int skip) {
  void **arr = (void **)malloc((sizeof(void *) * size));
  size_t n = ::backtrace(arr, size);

  char **str = backtrace_symbols(arr, n);
  if (str == nullptr) {
    ZZ_LOG_ERROR(g_logger) << "backtrace error";
    return;
  }

  for (size_t i = skip; i < n; ++i) {
    bt.push_back(demangle(str[i]));
  }

  free(str);
  free(arr);
}

std::string BackTraceToString(int size, int skip, const std::string &prefix) {
  std::vector<std::string> bt;
  BackTrace(bt, size, skip);
  std::stringstream ss;
  for (size_t i = 0; i < bt.size(); ++i) {
    ss << prefix << bt[i] << std::endl;
  }
  return ss.str();
}

std::string ToUpper(const std::string &str) {
  std::string ret = str;
  std::transform(ret.begin(), ret.end(), ret.begin(), ::toupper);
  return ret;
}

std::string ToLower(const std::string &str) {
  std::string ret = str;
  std::transform(ret.begin(), ret.end(), ret.begin(), ::tolower);
  return ret;
}

std::string Time2Str(time_t ts, const std::string &format) {
  struct tm t;
  localtime_r(&ts, &t);
  char buf[64];
  strftime(buf, sizeof(buf), format.c_str(), &t);
  return buf;
}

time_t Str2Time(const char *str, const char *format) {
  struct tm t;
  bzero(&t, sizeof(t));
  if (!strptime(str, format, &t)) {
    return 0;
  }
  return mktime(&t);
}

// 获取指定目录下的指定后缀文件列表
void FSUtil::ListAllFile(std::vector<std::string> &files,
                         const std::string &path, const std::string &subfix) {
  if (access(path.c_str(), 0) != 0) {
    return;
  }
  DIR *dir = opendir(path.c_str());
  if (dir == nullptr) {
    return;
  }

  struct dirent *d = readdir(dir);
  while (d != nullptr) {
    if (d->d_type == DT_DIR) {
      if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")) {
        continue;
      }
      ListAllFile(files, path + "/" + d->d_name, subfix);
    } else if (d->d_type == DT_REG) {
      std::string filename(d->d_name);
      if (subfix.empty()) {
        files.push_back(path + "/" + filename);
      } else {
        if (filename.size() < subfix.size()) {
          continue;
        }
        if (filename.substr(filename.length() - subfix.size()) == subfix) {
          files.push_back(path + "/" + filename);
        }
      }
    }
  }
  closedir(dir);
}

static int _lstat(const char *file, struct stat *st = nullptr) {
  struct stat lst;
  int ret = lstat(file, &lst);
  if (st) {
    *st = lst;
  }
  return ret;
}

static int _mkdir(const char *dirname) {
  if (access(dirname, F_OK) == 0) {
    return 0;
  }
  return mkdir(dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
}

bool FSUtil::Mkdir(const std::string &dirname) {
  if (_lstat(dirname.c_str()) == 0) {
    return true;
  }

  char *path = strdup(dirname.c_str());
  char *ptr = strchr(path + 1, '/');
  do {
    for (; ptr; *ptr = '/', ptr = strchr(ptr + 1, '/')) {
      *ptr = '\0';
      if (_mkdir(path) != 0) {
        break;
      }
    }
    if (ptr) {
      break;
    } else if (_mkdir(path) != 0) {
      break;
    }
    free(path);
    return true;
  } while (0);
  free(path);
  return false;
}

bool FSUtil::IsRunningPidFile(const std::string &pidfile) {
  if (_lstat(pidfile.c_str()) != 0) {
    return false;
  }

  std::ifstream ifs(pidfile);
  std::string line;
  if (!ifs || !std::getline(ifs, line)) {
    return false;
  }
  if (line.empty()) {
    return false;
  }

  pid_t pid = atoi(line.c_str());
  if (pid < 1) {
    return false;
  }
  if (kill(pid, 0) != 0) {
    return false;
  }
  return true;
}

bool FSUtil::Unlink(const std::string &filename, bool exist) {
  if (!exist && _lstat(filename.c_str())) {
    return true;
  }
  return ::unlink(filename.c_str()) == 0;
}

bool FSUtil::Rm(const std::string &path) {
  struct stat st;
  if (lstat(path.c_str(), &st)) {
    return true;
  }

  if (!(st.st_mode & S_IFDIR)) {
    return Unlink(path);
  }

  DIR *dir = opendir(path.c_str());
  if (dir == nullptr) {
    return false;
  }

  bool ret = true;
  struct dirent *d = readdir(dir);
  while (d) {
    if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")) {
      continue;
    }

    std::string dirname = path + "/" + d->d_name;
    ret = Rm(dirname);
  }

  closedir(dir);
  if (::rmdir(path.c_str()) != 0) {
    ret = false;
  }
  return ret;
}

bool FSUtil::Mv(const std::string &from, const std::string &to) {
  if (!Rm(to)) {
    return false;
  }
  return rename(from.c_str(), to.c_str()) == 0;
}

bool FSUtil::RealPath(const std::string &path, std::string &rpath) {
  if (_lstat(path.c_str())) {
    return false;
  }

  char *ptr = ::realpath(path.c_str(), nullptr);
  if (ptr == nullptr) {
    return false;
  }

  std::string(ptr).swap(rpath);
  free(ptr);
  return true;
}

std::string FSUtil::Dirname(const std::string &filename) {
  if (filename.empty()) {
    return ".";
  }
  auto pos = filename.rfind('/');
  if (pos == 0) {
    return "/";
  } else if (pos == std::string::npos) {
    return ".";
  } else {
    return filename.substr(0, pos);
  }
}

std::string FSUtil::Basename(const std::string &filename) {
  if (filename.empty()) {
    return filename;
  }

  auto pos = filename.rfind('/');
  if (pos == std::string::npos) {
    return filename;
  } else {
    return filename.substr(pos + 1);
  }
}

bool FSUtil::OpenForRead(std::ifstream &ifs, const std::string &filename,
                         std::ios_base::openmode mode) {
  ifs.open(filename.c_str(), mode);
  return ifs.is_open();
}

bool FSUtil::OpenForWrite(std::ofstream &ofs, const std::string &filename,
                          std::ios_base::openmode mode) {
  ofs.open(filename.c_str());
  if (ofs.is_open()) {
    std::string dir = Dirname(filename);
    Mkdir(dir);
    ofs.open(filename.c_str(), mode);
  }
  return ofs.is_open();
}

int8_t TypeUtil::ToChar(const std::string &str) {
  if (str.empty()) {
    return 0;
  }
  return *str.begin();
}

int64_t TypeUtil::Atoi(const std::string &str) {
  if (str.empty()) {
    return 0;
  }
  return strtoull(str.c_str(), nullptr, 10);
}

double TypeUtil::Atof(const std::string &str) {
  if (str.empty()) {
    return 0;
  }
  return atof(str.c_str());
}

int8_t TypeUtil::ToChar(const char *str) {
  if (str == nullptr) {
    return 0;
  }
  return str[0];
}

int64_t TypeUtil::Atoi(const char *str) {
  if (str == nullptr) {
    return 0;
  }
  return strtoull(str, nullptr, 10);
}

double TypeUtil::Atof(const char *str) {
  if (str == nullptr) {
    return 0;
  }
  return atof(str);
}

std::string StringUtil::FormatV(const char *fmt, va_list ap) {
  char *buf = nullptr;
  auto len = vasprintf(&buf, fmt, ap);
  if (len == -1) {
    return "";
  }
  std::string ret(buf, len);
  free(buf);
  return ret;
}

std::string StringUtil::Format(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  auto v = FormatV(fmt, ap);
  va_end(ap);
  return v;
}

std::string StringUtil::Trim(const std::string &str,
                             const std::string &delimit) {
  auto begin = str.find_first_not_of(delimit);
  if (begin == std::string::npos) {
    return "";
  }
  auto end = str.find_last_not_of(delimit);
  return str.substr(begin, end - begin + 1);
}

std::string StringUtil::TrimLeft(const std::string &str,
                                 const std::string &delimit) {
  auto begin = str.find_first_not_of(delimit);
  if (begin == std::string::npos) {
    return "";
  }
  return str.substr(begin);
}

std::string StringUtil::TrimRight(const std::string &str,
                                  const std::string &delimit) {
  auto end = str.find_last_not_of(delimit);
  if (end == std::string::npos) {
    return "";
  }
  return str.substr(0, end);
}

} // namespace zz
