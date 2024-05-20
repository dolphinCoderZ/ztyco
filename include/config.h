#pragma once

#include "log.h"
#include "mutex.h"
#include "thread.h"
#include "util.h"
#include <algorithm>
#include <boost/lexical_cast.hpp>
#include <cctype>
#include <cstddef>
#include <exception>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace zz {

class ConfigVarBase {
public:
  using ptr = std::shared_ptr<ConfigVarBase>;

  ConfigVarBase(const std::string &name, const std::string &desc = "")
      : m_name(name), m_desc(desc) {
    std::transform(m_name.begin(), m_name.end(), m_name.begin(), ::_tolower);
  }

  virtual ~ConfigVarBase();
  const std::string getConfigName() const { return m_name; }
  const std::string getConfigdesc() const { return m_desc; }

  /**
   * @brief 将参数值转换成YAML String
   */
  virtual std::string toString() = 0;

  /**
   * @brief 从字符串初始化参数值
   */
  virtual bool fromString(const std::string &val) = 0;

  /**
   * @brief 返回配置参数值的类型名称
   */
  virtual std::string getValueTypeName() const = 0;

protected:
  std::string m_name; // 配置参数名称
  std::string m_desc; // 配置参数描述
};

/**
 * @brief 类型转换模板类(F 源类型, T 目标类型)
 */
template <typename F, typename T> class LexicalCast {
public:
  /**
   * @brief 类型转换
   * @param[in] v 源类型值
   * @return 返回v转换后的目标类型
   * @exception 当类型不可转换时抛出异常
   */
  T operator()(const F &v) { return boost::lexical_cast<T>(v); }
};

/**
 * @brief 类型转换模板类片特化(YAML String 转换成 std::vector<T>)
 */
template <typename T> class LexicalCast<std::string, std::vector<T>> {
public:
  std::vector<T> operator()(const std::string &v) {
    YAML::Node node = YAML::Load(v);
    typename std::vector<T> vec;
    std::stringstream ss;
    for (size_t i = 0; i < node.size(); ++i) {
      ss.str("");
      ss << node[i];
      vec.push_back(LexicalCast<std::string, T>()(ss.str()));
    }
    return vec;
  }
};

/**
 * @brief 类型转换模板类片特化(std::vector<T> 转换成 YAML String)
 */
template <typename T> class LexicalCast<std::vector<T>, std::string> {
public:
  std::string operator()(const std::vector<T> &v) {
    YAML::Node node(YAML::NodeType::Sequence);
    for (auto &it : v) {
      node.push_back(YAML::Load(LexicalCast<T, std::string>()(it)));
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
  }
};

/**
 * @brief 类型转换模板类片特化(YAML String 转换成 std::list<T>)
 */
template <typename T> class LexicalCast<std::string, std::list<T>> {
public:
  std::list<T> operator()(const std::string &v) {
    YAML::Node node = YAML::Load(v);
    typename std::list<T> lt;
    std::stringstream ss;
    for (size_t i = 0; i < node.size(); ++i) {
      ss.str("");
      ss << node[i];
      lt.push_back(LexicalCast<std::string, T>()(ss.str()));
    }
    return lt;
  }
};

/**
 * @brief 类型转换模板类片特化(std::list<T> 转换成 YAML String)
 */
template <typename T> class LexicalCast<std::list<T>, std::string> {
public:
  std::string operator()(const std::list<T> &v) {
    YAML::Node node(YAML::NodeType::Sequence);
    for (auto &it : v) {
      node.push_back(YAML::Load(LexicalCast<T, std::string>()(it)));
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
  }
};

/**
 * @brief 类型转换模板类片特化(YAML String 转换成 std::set<T>)
 */
template <typename T> class LexicalCast<std::string, std::set<T>> {
public:
  std::set<T> operator()(const std::string &v) {
    YAML::Node node = YAML::Load(v);
    typename std::set<T> st;
    std::stringstream ss;
    for (size_t i = 0; i < node.size(); ++i) {
      ss.str("");
      ss << node[i];
      st.insert(LexicalCast<std::string, T>()(ss.str()));
    }
    return st;
  }
};

/**
 * @brief 类型转换模板类片特化(std::set<T> 转换成 YAML String)
 */
template <typename T> class LexicalCast<std::set<T>, std::string> {
public:
  std::string operator()(const std::set<T> &v) {
    YAML::Node node(YAML::NodeType::Sequence);
    for (auto &it : v) {
      node.push_back(YAML::Load(LexicalCast<T, std::string>()(it)));
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
  }
};

/**
 * @brief 类型转换模板类片特化(YAML String 转换成 std::unordered_set<T>)
 */
template <typename T> class LexicalCast<std::string, std::unordered_set<T>> {
public:
  std::unordered_set<T> operator()(const std::string &v) {
    YAML::Node node = YAML::Load(v);
    typename std::unordered_set<T> st;
    std::stringstream ss;
    for (size_t i = 0; i < node.size(); ++i) {
      ss.str("");
      ss << node[i];
      st.insert(LexicalCast<std::string, T>()(ss.str()));
    }
    return st;
  }
};

/**
 * @brief 类型转换模板类片特化(std::unordered_set<T> 转换成 YAML String)
 */
template <typename T> class LexicalCast<std::unordered_set<T>, std::string> {
public:
  std::string operator()(const std::unordered_set<T> &v) {
    YAML::Node node(YAML::NodeType::Sequence);
    for (auto &it : v) {
      node.push_back(YAML::Load(LexicalCast<T, std::string>()(it)));
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
  }
};

/**
 * @brief 类型转换模板类片特化(YAML String 转换成 std::map<std::string, T>)
 */
template <typename T> class LexicalCast<std::string, std::map<std::string, T>> {
public:
  std::map<std::string, T> operator()(const std::string &v) {
    YAML::Node node = YAML::Load(v);
    typename std::map<std::string, T> mp;
    std::stringstream ss;
    for (auto it = node.begin(); it != node.end(); ++it) {
      ss.str("");
      ss << it->second;
      mp.insert(std::make_pair(it->first.Scalar(),
                               LexicalCast<std::string, T>()(ss.str())));
    }
    return mp;
  }
};

/**
 * @brief 类型转换模板类片特化(std::map<std::string, T> 转换成 YAML String)
 */
template <typename T> class LexicalCast<std::map<std::string, T>, std::string> {
public:
  std::string operator()(const std::map<std::string, T> &v) {
    YAML::Node node(YAML::NodeType::Sequence);
    for (auto &it : v) {
      node[it.first] = YAML::Load(LexicalCast<T, std::string>()(it.second));
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
  }
};

/**
 * @brief 类型转换模板类片特化(YAML String 转换成
 * std::unordered_map<std::string, T>)
 */
template <typename T>
class LexicalCast<std::string, std::unordered_map<std::string, T>> {
public:
  std::unordered_map<std::string, T> operator()(const std::string &v) {
    YAML::Node node = YAML::Load(v);
    typename std::unordered_map<std::string, T> up;
    std::stringstream ss;
    for (auto it = node.begin(); it != node.end(); ++it) {
      ss.str("");
      ss << it->second;
      up.insert(std::make_pair(it->first.Scalar(),
                               LexicalCast<std::string, T>()(ss.str())));
    }
    return up;
  }
};

/**
 * @brief 类型转换模板类片特化(std::unordered_map<std::string, T> 转换成 YAML
 * String)
 */
template <typename T>
class LexicalCast<std::unordered_map<std::string, T>, std::string> {
public:
  std::string operator()(const std::unordered_map<std::string, T> &v) {
    YAML::Node node(YAML::NodeType::Sequence);
    for (auto &it : v) {
      node[it.first] = YAML::Load(LexicalCast<T, std::string>()(it.second));
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
  }
};

/**
 * @brief 配置参数模板子类,保存对应类型的参数值
 * @details T 参数的具体类型
 *          FromStr 从std::string转换成T类型的仿函数
 *          ToStr 从T转换成std::string的仿函数
 *          std::string 为YAML格式的字符串
 */
template <class T, class FromStr = LexicalCast<std::string, T>,
          class ToStr = LexicalCast<T, std::string>>
class ConfigVar : public ConfigVarBase {
public:
  using RWMutexType = RWMutex;
  using ptr = std::shared_ptr<ConfigVar>;
  using onChange_cb = std::function<void(const T &oldV, const T &newV)>;

  /**
   * @brief 通过参数名,参数值,描述构造ConfigVar
   * @param[in] name 参数名称有效字符为[0-9a-z_.]
   * @param[in] default_value 参数的默认值
   * @param[in] description 参数的描述
   */
  ConfigVar(const std::string &name, const T &defaultV,
            const std::string &desc = "")
      : ConfigVarBase(name, desc), m_val(defaultV) {}

  std::string toString() override {
    try {
      RWMutexType::ReadLock lock(m_mutex);
      return ToStr()(m_val);
    } catch (std::exception &e) {
      ZZ_LOG_ERROR(ZZ_LOG_ROOT()) << "ConfigVar::toString exception "
                                  << e.what() << " convert: " << TypeToName<T>()
                                  << " to string" << " name=" << m_name;
    }
    return "";
  }

  bool fromString(const std::string &val) override {
    try {
      setValue(FromStr()(val));
    } catch (std::exception &e) {
      ZZ_LOG_ERROR(ZZ_LOG_ROOT())
          << "ConfigVar::fromString exception " << e.what()
          << " convert: string to " << TypeToName<T>() << " name=" << m_name
          << " - " << val;
    }
    return false;
  }

  const T getValue() {
    RWMutexType::ReadLock lock(m_mutex);
    return m_val;
  }

  void setValue(const T &v) {
    {
      RWMutexType::ReadLock lock(m_mutex);
      if (v == m_val) {
        return;
      }
      for (auto &it : m_cbs) {
        it.second(m_val, v);
      }
    }
    RWMutexType::WriteLock lock(m_mutex);
    m_val = v;
  }

  /**
   * @brief 返回参数值的类型名称(typeinfo)
   */
  std::string getValueTypeName() const override { return TypeToName<T>(); }

  /**
   * @brief 变更回调函数管理
   */
  uint64_t addListener(onChange_cb cb) {
    static uint64_t s_fun_id = 0; // 标识回调函数
    RWMutexType::WriteLock lock(m_mutex);
    ++s_fun_id;
    m_cbs[s_fun_id] = cb;
    return s_fun_id;
  }

  void delListener(uint64_t key) {
    RWMutexType::WriteLock lock(m_mutex);
    m_cbs.erase(key);
  }

  onChange_cb getListener(uint64_t key) {
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_cbs.find(key);
    return it == m_cbs.end() ? nullptr : it->second;
  }

  void clearListener() {
    RWMutexType::WriteLock lock(m_mutex);
    m_cbs.clear();
  }

private:
  RWMutexType m_mutex;
  T m_val;
  std::map<uint64_t, onChange_cb> m_cbs;
};

/**
 * @brief ConfigVar的管理类
 * @details 提供便捷的方法创建/访问ConfigVar
 */
class Config {
public:
  using ConfigvarMap = std::unordered_map<std::string, ConfigVarBase::ptr>;
  using RWMutexType = RWMutex;

  /**
   * @brief 获取/创建对应参数名的配置参数
   * @param[in] name 配置参数名称
   * @param[in] default_value 参数默认值
   * @param[in] description 参数描述
   * @details 获取参数名为name的配置参数,如果存在直接返回
   *          如果不存在,创建参数配置并用default_value赋值
   * @return 返回对应的配置参数,如果参数名存在但是类型不匹配则返回nullptr
   * @exception 如果参数名包含非法字符[^0-9a-z_.] 抛出异常 std::invalid_argument
   */
  template <class T>
  static typename ConfigVar<T>::ptr Lookup(const std::string &name,
                                           const T &defaultV,
                                           const std::string &desc = "") {
    RWMutexType::WriteLock lock(GetMutex());
    auto it = GetDatas().find(name);
    if (it != GetDatas().end()) {
      auto tmp = std::dynamic_pointer_cast<ConfigVar<T>>(it->second);
      if (tmp) {
        ZZ_LOG_INFO(ZZ_LOG_ROOT()) << "Lookup name=" << name << " exists";
        return tmp;
      } else {
        ZZ_LOG_ERROR(ZZ_LOG_ROOT())
            << "Lookup name=" << name << " exists but type not "
            << TypeToName<T>()
            << " real_type=" << it->second->getValueTypeName() << " "
            << it->second->toString();
        return nullptr;
      }
    }

    if (name.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678") !=
        std::string::npos) {
      ZZ_LOG_ERROR(ZZ_LOG_ROOT()) << "Lookup name invalid " << name;
    }

    typename ConfigVar<T>::ptr v(new ConfigVar<T>(name, defaultV, desc));
    GetDatas()[name] = v;
    return v;
  }

  /**
   * @brief 查找配置参数
   * @param[in] name 配置参数名称
   * @return 返回配置参数名为name的配置参数
   */
  template <typename T>
  static typename ConfigVar<T>::ptr Lookup(const std::string &name) {
    RWMutexType::ReadLock lock(GetMutex());
    auto it = GetDatas().find(name);
    if (it == GetDatas().end()) {
      return nullptr;
    }
    return std::dynamic_pointer_cast<ConfigVar<T>>(it->second);
  }

  /**
   * @brief 使用YAML::Node初始化配置模块
   */
  static void LoadFromYaml(const YAML::Node &root);

  /**
   * @brief 加载path文件夹里面的配置文件
   */
  static void LoadFromConDir(const std::string &path, bool force = false);

  /**
   * @brief 查找配置参数,返回配置参数的基类
   * @param[in] name 配置参数名称
   */
  static ConfigVarBase::ptr LookupBase(const std::string &name);

  /**
   * @brief 遍历配置模块里面所有配置项
   * @param[in] cb 配置项回调函数
   */
  static void Visit(std::function<void(ConfigVarBase::ptr)> cb);

private:
  static ConfigvarMap &GetDatas() {
    static ConfigvarMap s_datas;
    return s_datas;
  }

  static RWMutexType &GetMutex() {
    // 只有一把锁
    static RWMutexType s_mutex;
    return s_mutex;
  }
};

} // namespace zz