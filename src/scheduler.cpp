#include "coro/scheduler.hpp"
#include <chrono>
#include <thread>

namespace coro
{
auto scheduler::init_impl(size_t ctx_cnt) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    detail::init_meta_info();   // 初始化全局计数器和状态信息
    m_ctx_cnt = ctx_cnt;   // 设置上下文数量
    m_ctxs    = detail::ctx_container{};   // 初始化上下文容器
    m_ctxs.reserve(m_ctx_cnt);   // 预留空间
    for (int i = 0; i < m_ctx_cnt; i++)
    {
        m_ctxs.emplace_back(std::make_unique<context>());   // 创建上下文
    }
    m_dispatcher.init(m_ctx_cnt, &m_ctxs);  // 初始化调度器

    m_ctx_stop_flag = stop_flag_type(m_ctx_cnt, atomic_ref_wrapper<int>{.val = 1});
    m_stop_token    = m_ctx_cnt;   // 设置停止标志

#ifdef ENABLE_MEMORY_ALLOC
    coro::allocator::memory::mem_alloc_config config;
    m_mem_alloc.init(config);
    ginfo.mem_alloc = &m_mem_alloc;
#endif
}

auto scheduler::loop_impl() noexcept -> void
{
    // TODO[lab2b]: Add you codes
    start_impl();  // 启动所有上下文
    
    // 等待所有上下文完成
    for (int i = 0; i < m_ctx_cnt; i++)
    {
        m_ctxs[i]->join();
    }
}

auto scheduler::stop_impl() noexcept -> void
{
    // TODO[lab2b]: example function
    // This is an example which just notify stop signal to each context,
    // if you don't need this, function just ignore or delete it
    for (int i = 0; i < m_ctx_cnt; i++)
    {
        m_ctxs[i]->notify_stop();
    }
}

auto scheduler::submit_task_impl(std::coroutine_handle<> handle) noexcept -> void
{
    // 不要在 scheduler::loop 结束后再添加新任务
    assert(this->m_stop_token.load(std::memory_order_acquire) != 0 && "error! submit task after scheduler loop finish");
    size_t ctx_id = m_dispatcher.dispatch();
    // 直接增加引用计数是不合理的，
    // 根据 context 的运行状态来增加引用计数，避免冗余增加
    m_stop_token.fetch_add(
        1 - std::atomic_ref(m_ctx_stop_flag[ctx_id].val).fetch_or(1, memory_order_acq_rel), memory_order_acq_rel);
    m_ctxs[ctx_id]->submit_task(handle);
}

// 启动所有 context
auto scheduler::start_impl() noexcept -> void
{
    for (int i = 0; i < m_ctx_cnt; i++)
    {
        m_ctxs[i]->set_stop_cb(
            [&, i]()
            {
                // context 将其关联的状态设置为 0 即已执行完所有任务，cnt 总是为 1
                auto cnt = std::atomic_ref(this->m_ctx_stop_flag[i].val).fetch_and(0, memory_order_acq_rel);
                // 将 scheduler 的引用计数减 1，如果引用计数降至 0，那么触发 scheduler 发送停止信号
                if (this->m_stop_token.fetch_sub(cnt) == cnt)
                {
                    this->stop_impl();
                }
            });
        m_ctxs[i]->start();
    }
}
}; // namespace coro
