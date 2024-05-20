#include <memory>

/*
template <class T, class X, int n> T &GetInstanceX() {
  static T v;
  return v;
}

template <class T, class X, int n> std::shared_ptr<T> GetInstancePtr() {
  static std::shared_ptr<T> v(new T);
  return v;
}
*/
namespace zz {
/**
 * @brief ����ģʽ��װ��
 * @details T ����
 *          X Ϊ�˴�����ʵ����Ӧ��Tag
 *          N ͬһ��Tag������ʵ������
 */
template <class T, class X = void, int N = 0> class Singleton {
public:
  static T *GetInstance() {
    static T v;
    return &v;
  }
};

/**
 * @brief ����ģʽ����ָ���װ��
 * @details T ����
 *          X Ϊ�˴�����ʵ����Ӧ��Tag
 *          N ͬһ��Tag������ʵ������
 */
template <class T, class X = void, int N = 0> class SingletonPtr {
public:
  static std::shared_ptr<T> GetInstance() {
    static std::shared_ptr<T> v(new T);
    return v;
  }
};

} // namespace zz
