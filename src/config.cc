#include "config.h"
#include "env.h"
#include "log.h"
#include "mutex.h"
#include "util.h"
#include <algorithm>
#include <cctype>
#include <list>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace zz {
static zz::Logger::ptr g_logger = ZZ_LOG_NAME("system");

ConfigVarBase::ptr Config::LookupBase(const std::string &name) {
  RWMutexType::ReadLock lock(GetMutex());
  auto it = GetDatas().find(name);
  return it == GetDatas().end() ? nullptr : it->second;
}

static void
ListAllMember(const std::string &prefix, const YAML::Node node,
              std::list<std::pair<std::string, const YAML::Node>> &output) {
  if (prefix.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678") !=
      std::string::npos) {
    ZZ_LOG_ERROR(g_logger) << "Config invalid name: " << prefix << " : "
                           << node;
    return;
  }

  output.push_back(std::make_pair(prefix, node));
  if (node.IsMap()) {
    for (auto it = node.begin(); it != node.end(); ++it) {
      ListAllMember(prefix.empty() ? it->first.Scalar()
                                   : prefix + "." + it->first.Scalar(),
                    it->second, output);
    }
  }
}

void Config::LoadFromYaml(const YAML::Node &root) {
  std::list<std::pair<std::string, const YAML::Node>> all_nodes;
  ListAllMember("", root, all_nodes);

  for (auto &it : all_nodes) {
    std::string key = it.first;
    if (key.empty()) {
      continue;
    }

    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    ConfigVarBase::ptr var = LookupBase(key);

    if (var) {
      if (it.second.IsScalar()) {
        var->fromString(it.second.Scalar());
      } else {
        std::stringstream ss;
        ss << it.second;
        var->fromString(ss.str());
      }
    }
  }
}

// 记录每个文件的修改时间
static std::map<std::string, uint64_t> s_file2modifytime;
// 是否强制加载配置文件，非强制加载的情况下，如果记录的文件修改时间未变化，则跳过该文件的加载
static zz::Mutex s_mutex;
void Config::LoadFromConDir(const std::string &path, bool force) {
  std::string absolute_path = zz::EnvMgr::GetInstance()->getAbsolutePath(path);
  std::vector<std::string> files;
  FSUtil::ListAllFile(files, absolute_path, ".yml");

  for (auto &it : files) {
    {
      struct stat st;
      lstat(it.c_str(), &st);
      zz::Mutex::Lock lock(s_mutex);
      if (!force && s_file2modifytime[it] == (uint64_t)st.st_mtime) {
        continue;
      }
    }
    try {
      YAML::Node root = YAML::LoadFile(it);
      LoadFromYaml(root);
      ZZ_LOG_INFO(g_logger) << "LoadConfFile file=" << it << " ok";
    } catch (...) {
      ZZ_LOG_ERROR(g_logger) << "LoadConfFile file=" << it << " failed";
    }
  }
}

void Config::Visit(std::function<void(ConfigVarBase::ptr)> cb) {
  RWMutexType::ReadLock lock(GetMutex());
  ConfigvarMap &mp = GetDatas();
  for (auto it = mp.begin(); it != mp.end(); ++it) {
    cb(it->second);
  }
}

} // namespace zz