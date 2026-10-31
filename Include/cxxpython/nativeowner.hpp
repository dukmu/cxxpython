#pragma once

#include <memory>

namespace cxxpython::core {
// GC payloads require standard layout; std::unique_ptr does not promise it.
// The pointed-to native state is owned here and traced by its containing object.
template <class T> class NativeOwner {
  public:
    NativeOwner() noexcept = default;
    explicit NativeOwner(std::unique_ptr<T> value) noexcept : pointer_(value.release()) {}
    ~NativeOwner() noexcept
    {
        delete pointer_;
    }
    NativeOwner(const NativeOwner &) = delete;
    NativeOwner &operator=(const NativeOwner &) = delete;
    T *get() const noexcept
    {
        return pointer_;
    }
    T *operator->() const noexcept
    {
        return pointer_;
    }
    T &operator*() const noexcept
    {
        return *pointer_;
    }
    explicit operator bool() const noexcept
    {
        return pointer_ != nullptr;
    }

  private:
    T *pointer_ = nullptr;
};
} // namespace cxxpython::core
