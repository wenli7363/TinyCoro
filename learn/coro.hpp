// file: coro.hpp
#pragma once

#include <coroutine>
#include <exception>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

#include "utils.hpp"

template <typename return_type = void> class task;

// promise 基类，定义了协程的初始以及结束时的调度逻辑
struct promise_base {
  friend struct final_awaitable;

  // 用作 final_suspend 返回的 awaiter
  struct final_awaitable {
    auto await_ready() const noexcept -> bool { return false; }
    
    // 确保协程的栈式调用正确返回
    template <typename promise_type>
    auto await_suspend(std::coroutine_handle<promise_type> coroutine) noexcept
        -> std::coroutine_handle<> {
      auto &promise = coroutine.promise();
      if (promise.m_continuation != nullptr) {
        return promise.m_continuation;
      } else {
        return std::noop_coroutine();
      }
    }

    auto await_resume() noexcept -> void {}
  };

  promise_base() noexcept = default;
  ~promise_base() = default;

  // 协程创建后立刻处于 suspend 状态
  auto initial_suspend() noexcept { return std::suspend_always{}; }

  auto final_suspend() noexcept { return final_awaitable{}; }

  // 记录当前协程执行结束后应当恢复的协程
  auto continuation(std::coroutine_handle<> continuation) noexcept -> void {
    m_continuation = continuation;
  }

protected:
  std::coroutine_handle<> m_continuation{nullptr};
};

// 忽略复杂类型的存取
template <typename return_type> struct promise final : public promise_base {
public:
  using task_type = task<return_type>;
  using coroutine_handle = std::coroutine_handle<promise<return_type>>;

  promise() noexcept {}
  promise(const promise &) = delete;
  promise(promise &&other) = delete;
  promise &operator=(const promise &) = delete;
  promise &operator=(promise &&other) = delete;
  ~promise() = default;

  auto get_return_object() noexcept -> task_type;

  auto return_value(return_type &value) -> void {
    m_value = std::make_optional<return_type>(value);
  }

  auto return_value(return_type &&value) -> void {
    m_value = std::make_optional<return_type>(std::move(value));
  }

  auto unhandled_exception() noexcept -> void {
    m_except = std::current_exception();
  }

  auto result() -> return_type {
    if (m_value.has_value()) {
      return *m_value;
    } else {
      throw std::runtime_error{
          "The return value was never set, did you execute the coroutine?"};
    }
  }

private:
  std::optional<return_type> m_value;
  std::exception_ptr m_except;
};

template <> struct promise<void> : public promise_base {
  using task_type = task<void>;
  using coroutine_handle = std::coroutine_handle<promise<void>>;

  promise() noexcept = default;
  promise(const promise &) = delete;
  promise(promise &&other) = delete;
  promise &operator=(const promise &) = delete;
  promise &operator=(promise &&other) = delete;
  ~promise() = default;

  auto get_return_object() noexcept -> task_type;

  auto return_void() noexcept -> void {}

  auto unhandled_exception() noexcept -> void {
    m_exception_ptr = std::current_exception();
  }

  auto result() -> void {
    if (m_exception_ptr) {
      std::rethrow_exception(m_exception_ptr);
    }
  }

private:
  std::exception_ptr m_exception_ptr{nullptr};
};

// 面向用户的协程对象
template <typename return_type> class [[nodiscard]] task {
public:
  using task_type = task<return_type>;
  using promise_type = promise<return_type>;
  using coroutine_handle = std::coroutine_handle<promise_type>;

  struct awaitable_base {
    awaitable_base(coroutine_handle coroutine) noexcept
        : m_coroutine(coroutine) {}

    auto await_ready() const noexcept -> bool {
      return !m_coroutine || m_coroutine.done();
    }

    // 连接调用与调用者之间的调用关系
    auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept
        -> std::coroutine_handle<> {
      m_coroutine.promise().continuation(awaiting_coroutine);
      return m_coroutine;
    }

    std::coroutine_handle<promise_type> m_coroutine{nullptr};
  };

  task() noexcept : m_coroutine(nullptr) {}

  explicit task(coroutine_handle handle) : m_coroutine(handle) {}
  task(const task &) = delete;
  task(task &&other) noexcept
      : m_coroutine(std::exchange(other.m_coroutine, nullptr)) {}

  ~task() {
    if (m_coroutine != nullptr) {
      m_coroutine.destroy();
    }
  }

  auto operator=(const task &) -> task & = delete;

  auto operator=(task &&other) noexcept -> task & {
    if (std::addressof(other) != this) {
      if (m_coroutine != nullptr) {
        m_coroutine.destroy();
      }

      m_coroutine = std::exchange(other.m_coroutine, nullptr);
    }

    return *this;
  }

  auto is_ready() const noexcept -> bool {
    return m_coroutine == nullptr || m_coroutine.done();
  }

  auto resume() -> bool {
    if (!m_coroutine.done()) {
      m_coroutine.resume();
    }
    return !m_coroutine.done();
  }

  auto destroy() -> bool {
    if (m_coroutine != nullptr) {
      m_coroutine.destroy();
      m_coroutine = nullptr;
      return true;
    }

    return false;
  }

  auto operator co_await() noexcept {
    struct awaitable : public awaitable_base {
      auto await_resume() -> decltype(auto) {
        return this->m_coroutine.promise().result();
      }
    };

    return awaitable{m_coroutine};
  }

  auto handle() -> coroutine_handle { return m_coroutine; }

private:
  coroutine_handle m_coroutine{nullptr};
};

template <typename return_type>
inline auto promise<return_type>::get_return_object() noexcept
    -> task<return_type> {
  return task<return_type>{coroutine_handle::from_promise(*this)};
}

inline auto promise<void>::get_return_object() noexcept -> task<> {
  return task<>{coroutine_handle::from_promise(*this)};
}