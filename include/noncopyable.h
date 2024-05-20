#pragma once

namespace zz {
class Noncopyable {
public:
  Noncopyable() = default;

  ~Noncopyable() = default;

  Noncopyable(const Noncopyable &) = delete;
  Noncopyable &operator=(const Noncopyable &) = delete;
};
} // namespace zz