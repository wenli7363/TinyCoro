/**
 * @file context.hpp
 * @author JiahuiWang
 * @brief lab2b
 * @version 1.1
 * @date 2025-03-26
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include "config.h"
#include "coro/engine.hpp"
#include "coro/meta_info.hpp"
#include "coro/task.hpp"

namespace coro
{
/**
 * @brief Welcome to tinycoro lab2b, in this part you will use engine to build context. Like
 * equipping soldiers with weapons, context will use engine to execute io task and computation task.
 *
 * @warning You should carefully consider whether each implementation should be thread-safe.
 *
 * You should follow the rules below in this part:
 *
 * @note Do not modify existing functions and class declaration, which may cause the test to not run
 * correctly, but you can change the implementation logic if you need.
 *
 * @note The location marked by todo is where you must add code, but you can also add code anywhere
 * you want, such as function and class definitions, even member variables.
 */

using config::ctx_id;
using std::atomic;
using std::jthread;
using std::make_unique;
using std::memory_order_acq_rel;
using std::memory_order_acquire;
using std::memory_order_relaxed;
using std::stop_token;
using std::unique_ptr;

using detail::ginfo;
using detail::linfo;

using engine = detail::engine;
using stop_cb = std::function<void()>;   // context停止回调函数类型

class scheduler;

/**
 * @brief Each context own one engine, it's the core part of tinycoro,
 * which can process computation task and io task
 *
 */
class context
{
public:
    context() noexcept;
    ~context() noexcept                = default;
    context(const context&)            = delete;
    context(context&&)                 = delete;
    context& operator=(const context&) = delete;
    context& operator=(context&&)      = delete;

    [[CORO_TEST_USED(lab2b)]] auto init() noexcept -> void;

    [[CORO_TEST_USED(lab2b)]] auto deinit() noexcept -> void;

    /**
     * @brief work thread start running
     *
     */
    [[CORO_TEST_USED(lab2b)]] auto start() noexcept -> void;

    /**
     * @brief send stop signal to work thread
     *
     */
    [[CORO_TEST_USED(lab2b)]] auto notify_stop() noexcept -> void;

    /**
     * @brief wait work thread stop
     *
     */
    inline auto join() noexcept -> void { m_job->join(); }

    // 三种submit_task的重载
    inline auto submit_task(task<void>&& task) noexcept -> void
    {
        auto handle = task.handle();
        task.detach();
        this->submit_task(handle);
    }

    inline auto submit_task(task<void>& task) noexcept -> void { submit_task(task.handle()); }

    /**
     * @brief submit one task handle to context
     *
     * @param handle
     */
    [[CORO_TEST_USED(lab2b)]] auto submit_task(std::coroutine_handle<> handle) noexcept -> void;

    /**
     * @brief get context unique id
     *
     * @return ctx_id
     */
    inline auto get_ctx_id() noexcept -> ctx_id { return m_id; }

    /**
     * @brief add reference count of context
     *
     * @param register_cnt
     */
    [[CORO_TEST_USED(lab2b)]] auto register_wait(int register_cnt = 1) noexcept -> void;

    /**
     * @brief reduce reference count of context
     *
     * @param register_cnt
     */
    [[CORO_TEST_USED(lab2b)]] auto unregister_wait(int register_cnt = 1) noexcept -> void;

    inline auto get_engine() noexcept -> engine& { return m_engine; }

    /**
     * @brief main logic of work thread
     *
     * @param token
     */
    [[CORO_TEST_USED(lab2b)]] auto run(stop_token token) noexcept -> void;

    // TODO[lab2b]: Add more function if you need
    inline auto set_stop_cb(stop_cb cb) noexcept -> void{
        m_stop_cb = cb;
    }

private:
    CORO_ALIGN engine   m_engine;   // 引擎
    unique_ptr<jthread> m_job;   // 工作线程
    ctx_id              m_id;   // 上下文ID

    // TODO[lab2b]: Add more member variables if you need
    stop_cb m_stop_cb;   // context停止回调函数
    std::atomic<size_t> m_ref_count{0};  // 引用计数，跟踪等待的任务数量
};

inline context& local_context() noexcept
{
    return *linfo.ctx;   // 返回当前线程的上下文
}

inline void submit_to_context(task<void>&& task) noexcept
{
    local_context().submit_task(std::move(task));
}

inline void submit_to_context(task<void>& task) noexcept
{
    local_context().submit_task(task.handle());
}

inline void submit_to_context(std::coroutine_handle<> handle) noexcept
{
    local_context().submit_task(handle);
}

}; // namespace coro
