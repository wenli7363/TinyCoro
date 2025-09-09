/**
 * @file task.hpp
 * @author jaye chen
 * @brief lab1
 * @version 1.1
 * @date 2025-09-08
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <cassert>
#include <coroutine>
#include <stdexcept>
#include <utility>

#include "coro/attribute.hpp"
#include "coro/detail/container.hpp"

#ifdef ENABLE_MEMORY_ALLOC
    #include "coro/meta_info.hpp"
#endif

namespace coro
{
/**
 * @brief Welcome to tinycoro lab1, in this part you will add codes for task.hpp to make task can be
 * detached and after the execution is completed, the executive power will be transferred to the
 * parent level.
 *
 * You should follow the rules below in this part:
 *
 * @note Do not modify existing functions and class declaration, which may cause the test to not run
 * correctly, but you can change the implementation logic if you need.
 *
 * @note The location marked by todo is where you must add code, but you can also add code anywhere
 * you want, such as function and class definitions, even member variables.
 */

template<typename return_type = void>
class task;

namespace detail
{
struct promise_base
{
public:
    using coroutine_handle = std::coroutine_handle<>;
    
    promise_base() noexcept = default;
    ~promise_base()         = default;

    constexpr auto initial_suspend() noexcept { return std::suspend_always{}; }


    [[CORO_TEST_USED(lab1)]] auto final_suspend() noexcept -> auto
    {
        // TODO[lab1]: Add you codes
        // 当前协程执行完毕，如果下一个应该执行的m_coroutine存在，则resume
        return final_awaiter{};
    }

    auto set_continuation(coroutine_handle coroutine) noexcept -> void
    {
        m_coroutine = coroutine;
    }

    auto get_continuation() noexcept -> coroutine_handle
    {
        return m_coroutine;
    }

    auto is_detached() const noexcept -> bool
    {
        return m_is_detached;
    }

    auto set_detached(bool detached) noexcept -> void
    {
        m_coroutine = nullptr;
        m_is_detached = detached;
    }

#ifdef ENABLE_MEMORY_ALLOC
    void* operator new(std::size_t size)
    {
        return ::coro::detail::ginfo.mem_alloc->allocate(size);
    }

    void operator delete(void* ptr, [[CORO_MAYBE_UNUSED]] std::size_t size)
    {
        ::coro::detail::ginfo.mem_alloc->release(ptr);
    }
#endif

#ifdef DEBUG
public:
    int promise_id{0};
#endif // DEBUG

protected:
    coroutine_handle m_coroutine{nullptr};  // 下个协程句柄
    bool m_is_detached{false};              // detach状态标志

private:
    struct final_awaiter
    {
        bool await_ready() const noexcept
        {
            return false;  // 总是挂起
        }

        void await_suspend(std::coroutine_handle<> current_coroutine) noexcept
        {
            // 从当前协程的 promise 中获取父协程句柄
            auto specific_handle = std::coroutine_handle<promise_base>::from_address(current_coroutine.address());
            auto& promise = specific_handle.promise();
            
            if (promise.m_coroutine)
            {
                // 恢复父协程
                promise.m_coroutine.resume();
            }
            // 如果没有父协程，协程就结束了
        }

        void await_resume() noexcept
        {
            // final_suspend 不需要返回值
        }
    };
};

// return_type的特化版本
template<typename return_type>
struct promise final : public promise_base, public container<return_type>
{
public:
    using task_type        = task<return_type>;
    using coroutine_handle = std::coroutine_handle<promise<return_type>>;

#ifdef DEBUG
    template<typename... Args>
    promise(int id, Args&&... args) noexcept
    {
        promise_id = id;
    }
#endif // DEBUG
    promise() noexcept
    {
    }
    promise(const promise&)             = delete;
    promise(promise&& other)            = delete;
    promise& operator=(const promise&)  = delete;
    promise& operator=(promise&& other) = delete;
    ~promise()                          = default;

    // 创建并返回对应的task对象
    auto get_return_object() noexcept -> task_type;

    // 处理未处理的异常
    auto unhandled_exception() noexcept -> void
    {
        this->set_exception();
    }
};


// void的特化版本
template<>
struct promise<void> : public promise_base
{
    using task_type        = task<void>;
    using coroutine_handle = std::coroutine_handle<promise<void>>;

#ifdef DEBUG
    template<typename... Args>
    promise(int id, Args&&... args) noexcept
    {
        promise_id = id;
    }
#endif // DEBUG
    promise() noexcept                  = default;
    promise(const promise&)             = delete;
    promise(promise&& other)            = delete;
    promise& operator=(const promise&)  = delete;
    promise& operator=(promise&& other) = delete;
    ~promise()                          = default;

    auto get_return_object() noexcept -> task_type;

    constexpr auto return_void() noexcept -> void
    {
    }

    // 存储异常
    auto unhandled_exception() noexcept -> void
    {
        m_exception_ptr = std::current_exception();
    }

    auto result() -> void
    {
        if (m_exception_ptr)
        {
            std::rethrow_exception(m_exception_ptr);
        }
    }

private:
    std::exception_ptr m_exception_ptr{nullptr};
};

} // namespace detail

// return_type的特化版本 TASK模板
template<typename return_type>
class [[CORO_AWAIT_HINT]] task
{
public:
    using task_type        = task<return_type>;
    using promise_type     = detail::promise<return_type>;
    using coroutine_handle = std::coroutine_handle<promise_type>;

    struct awaitable_base
    {
        awaitable_base(coroutine_handle coroutine) noexcept : m_coroutine(coroutine) {}

        // 如果m_coroutine为空或已经完成，则返回true
        auto await_ready() const noexcept -> bool { return !m_coroutine || m_coroutine.done(); }

        auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> std::coroutine_handle<>
        {
            // TODO[lab1]: Add you codes
            // 暂停当前协程(awaiting_coroutine)，并返回新的协程句柄(m_coroutine)
            m_coroutine.promise().set_continuation(awaiting_coroutine);    // 把task1的协程句柄存入task2的promise中
            return m_coroutine;    // 返回task2的协程句柄
        }

        std::coroutine_handle<promise_type> m_coroutine{nullptr};   // 注意这里是task2的协程句柄
    };

    // 默认构造函数
    task() noexcept : m_coroutine(nullptr) {}

    // 显式构造函数
    explicit task(coroutine_handle handle) : m_coroutine(handle) {}
    // 禁用拷贝构造函数
    task(const task&) = delete;
    // 移动构造函数
    task(task&& other) noexcept : m_coroutine(std::exchange(other.m_coroutine, nullptr)) {}

    ~task()
    {
        if (m_coroutine != nullptr)
        {
            m_coroutine.destroy();
        }
    }

    // 禁用拷贝赋值运算符
    auto operator=(const task&) -> task& = delete;

    // 移动赋值运算符
    auto operator=(task&& other) noexcept -> task&
    {
        if (std::addressof(other) != this)
        {
            if (m_coroutine != nullptr)
            {
                m_coroutine.destroy();
            }

            m_coroutine = std::exchange(other.m_coroutine, nullptr);
        }

        return *this;
    }

    /**
     * @return True if the task is in its final suspend or if the task has been destroyed.
     */
    auto is_ready() const noexcept -> bool { return m_coroutine == nullptr || m_coroutine.done(); }

    auto resume() -> bool
    {
        if (!m_coroutine.done())
        {
            m_coroutine.resume();
        }
        return !m_coroutine.done();
    }

    auto destroy() -> bool
    {
        if (m_coroutine != nullptr)
        {
            m_coroutine.destroy();
            m_coroutine = nullptr;
            return true;
        }

        return false;
    }

    [[CORO_TEST_USED(lab1)]] auto detach() -> void
    {
        if (m_coroutine != nullptr)
        {
            m_coroutine.promise().set_detached(true);
        }
        m_coroutine = nullptr;
    }

    auto operator co_await() const& noexcept
    {
        // 定义一个awaitable类，继承自awaitable_base
        struct awaitable : public awaitable_base
        {
            auto await_resume() -> decltype(auto) { return this->m_coroutine.promise().result(); }
        };

        // co_await操作符这里只获取一个awaitable对象，并返回
        return awaitable{m_coroutine};
    }

    auto operator co_await() const&& noexcept
    {
        struct awaitable : public awaitable_base
        {
            auto await_resume() -> decltype(auto) { return std::move(this->m_coroutine.promise()).result(); }
        };

        return awaitable{m_coroutine};
    }

    auto promise() & -> promise_type& { return m_coroutine.promise(); }
    auto promise() const& -> const promise_type& { return m_coroutine.promise(); }
    auto promise() && -> promise_type&& { return std::move(m_coroutine.promise()); }

    auto handle() & -> coroutine_handle { return m_coroutine; }
    auto handle() && -> coroutine_handle { return std::exchange(m_coroutine, nullptr); }

private:
    coroutine_handle m_coroutine{nullptr};  // 协程句柄
};

using coroutine_handle = std::coroutine_handle<detail::promise_base>;

/**
 * @brief do clean work when handle is done
 *
 * @param handle
 */
[[CORO_TEST_USED(lab1)]] inline auto clean(std::coroutine_handle<> handle) noexcept -> void
{
    if (handle != nullptr)
    {
        // 将通用句柄转换为promise_base句柄来访问is_detached方法
        auto specific_handle = std::coroutine_handle<detail::promise_base>::from_address(handle.address());
        auto& promise = specific_handle.promise();
        
        if (promise.is_detached())
        {
            handle.destroy();
        }
    }
}


namespace detail
{
template<typename return_type>
inline auto promise<return_type>::get_return_object() noexcept -> task<return_type>
{
    return task<return_type>{coroutine_handle::from_promise(*this)};
}

inline auto promise<void>::get_return_object() noexcept -> task<>
{
    return task<>{coroutine_handle::from_promise(*this)};
}

#ifdef DEBUG
template<typename T = void>
inline auto get_promise(std::coroutine_handle<> handle) -> promise<T>&
{
    auto  specific_handle = std::coroutine_handle<detail::promise<T>>::from_address(handle.address());
    auto& promise         = specific_handle.promise();
    return promise;
}
#endif // DEBUG

} // namespace detail

} // namespace coro
