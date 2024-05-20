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
   * @brief ������ֵת����YAML String
   */
  virtual std::string toString() = 0;

  /**
   * @brief ���ַ�����ʼ������ֵ
   */
  virtual bool fromString(const std::string &val) = 0;

  /**
   * @brief �������ò���ֵ����������
   */
  virtual std::string getValueTypeName() const = 0;

protected:
  std::string m_name; // ���ò�������
  std::string m_desc; // ���ò�������
};

/**
 * @brief ����ת��ģ����(F Դ����, T Ŀ������)
 */
template <typename F, typename T> class LexicalCast {
public:
  /**
   * @brief ����ת��
   * @param[in] v Դ����ֵ
   * @return ����vת�����Ŀ������
   * @exception �����Ͳ���ת��ʱ�׳��쳣
   */
  T operator()(const F &v) { return boost::lexical_cast<T>(v); }
};

/**
 * @brief ����ת��ģ����Ƭ�ػ�(YAML String ת���� std::vector<T>)
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
 * @brief ����ת��ģ����Ƭ�ػ�(std::vector<T> ת���� YAML String)
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
 * @brief ����ת��ģ����Ƭ�ػ�(YAML String ת���� std::list<T>)
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
 * @brief ����ת��ģ����Ƭ�ػ�(std::list<T> ת���� YAML String)
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
 * @brief ����ת��ģ����Ƭ�ػ�(YAML String ת���� std::set<T>)
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
 * @brief ����ת��ģ����Ƭ�ػ�(std::set<T> ת���� YAML String)
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
 * @brief ����ת��ģ����Ƭ�ػ�(YAML String ת���� std::unordered_set<T>)
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
 * @brief ����ת��ģ����Ƭ�ػ�(std::unordered_set<T> ת���� YAML String)
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
 * @brief ����ת��ģ����Ƭ�ػ�(YAML String ת���� std::map<std::string, T>)
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
 * @brief ����ת��ģ����Ƭ�ػ�(std::map<std::string, T> ת���� YAML String)
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
 * @brief ����ת��ģ����Ƭ�ػ�(YAML String ת����
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
 * @brief ����ת��ģ����Ƭ�ػ�(std::unordered_map<std::string, T> ת���� YAML
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
 * @brief ���ò���ģ������,�����Ӧ���͵Ĳ���ֵ
 * @details T �����ľ�������
 *          FromStr ��std::stringת����T���͵ķº���
 *          ToStr ��Tת����std::string�ķº���
 *          std::string ΪYAML��ʽ���ַ���
 */
template <class T, class FromStr = LexicalCast<std::string, T>,
          class ToStr = LexicalCast<T, std::string>>
class ConfigVar : public ConfigVarBase {
public:
  using RWMutexType = RWMutex;
  using ptr = std::shared_ptr<ConfigVar>;
  using onChange_cb = std::function<void(const T &oldV, const T &newV)>;

  /**
   * @brief ͨ��������,����ֵ,��������ConfigVar
   * @param[in] name ����������Ч�ַ�Ϊ[0-9a-z_.]
   * @param[in] default_value ������Ĭ��ֵ
   * @param[in] description ����������
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
   * @brief ���ز���ֵ����������(typeinfo)
   */
  std::string getValueTypeName() const override { return TypeToName<T>(); }

  /**
   * @brief ����ص���������
   */
  uint64_t addListener(onChange_cb cb) {
    static uint64_t s_fun_id = 0; // ��ʶ�ص�����
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
 * @brief ConfigVar�Ĺ�����
 * @details �ṩ��ݵķ�������/����ConfigVar
 */
class Config {
public:
  using ConfigvarMap = std::unordered_map<std::string, ConfigVarBase::ptr>;
  using RWMutexType = RWMutex;

  /**
   * @brief ��ȡ/������Ӧ�����������ò���
   * @param[in] name ���ò�������
   * @param[in] default_value ����Ĭ��ֵ
   * @param[in] description ��������
   * @details ��ȡ������Ϊname�����ò���,�������ֱ�ӷ���
   *          ���������,�����������ò���default_value��ֵ
   * @return ���ض�Ӧ�����ò���,������������ڵ������Ͳ�ƥ���򷵻�nullptr
   * @exception ��������������Ƿ��ַ�[^0-9a-z_.] �׳��쳣 std::invalid_argument
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
   * @brief �������ò���
   * @param[in] name ���ò�������
   * @return �������ò�����Ϊname�����ò���
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
   * @brief ʹ��YAML::Node��ʼ������ģ��
   */
  static void LoadFromYaml(const YAML::Node &root);

  /**
   * @brief ����path�ļ�������������ļ�
   */
  static void LoadFromConDir(const std::string &path, bool force = false);

  /**
   * @brief �������ò���,�������ò����Ļ���
   * @param[in] name ���ò�������
   */
  static ConfigVarBase::ptr LookupBase(const std::string &name);

  /**
   * @brief ��������ģ����������������
   * @param[in] cb ������ص�����
   */
  static void Visit(std::function<void(ConfigVarBase::ptr)> cb);

private:
  static ConfigvarMap &GetDatas() {
    static ConfigvarMap s_datas;
    return s_datas;
  }

  static RWMutexType &GetMutex() {
    // ֻ��һ����
    static RWMutexType s_mutex;
    return s_mutex;
  }
};

} // namespace zz