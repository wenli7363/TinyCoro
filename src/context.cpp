#include "coro/context.hpp"
#include "coro/scheduler.hpp"
#include <chrono>
#include <thread>

namespace coro
{
context::context() noexcept
{
    m_id = ginfo.context_id.fetch_add(1, std::memory_order_relaxed);
}

auto context::init() noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_engine.init();
    linfo.ctx = this;
}

auto context::deinit() noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_engine.deinit();
    linfo.ctx = nullptr;
}

auto context::start() noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_job = make_unique<jthread>(
        [this](stop_token token)
        {
            this->init();
            // 如果外部没有注入 stop_cb，那么自行为其添加逻辑
            if (!(this->m_stop_cb))
            {
                m_stop_cb = [&]() { m_job->request_stop(); };
            }
            this->run(token);
            this->deinit();
        });
}

auto context::notify_stop() noexcept -> void
{
    // TODO[lab2b]: Add you codes    
    m_job->request_stop();  // 通知 jthread 停止
    m_engine.get_uring().write_eventfd(1); // 唤醒可能阻塞的线程
}

auto context::submit_task(std::coroutine_handle<> handle) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_engine.submit_task(handle);
}

auto context::register_wait(int register_cnt) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_ref_count.fetch_add(register_cnt, memory_order_acq_rel);
}

auto context::unregister_wait(int register_cnt) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_ref_count.fetch_sub(register_cnt, memory_order_acq_rel);
}

auto context::run(stop_token token) noexcept -> void
{
    while (!token.stop_requested()) {
        // 执行所有可用的任务
        auto num = m_engine.num_task_schedule();
        for (int i = 0; i < num; i++) {
            m_engine.exec_one_task();
        }

        // 检查是否完成所有任务
        if (m_ref_count.load(memory_order_acquire) == 0 && m_engine.empty_io()) {
            if (!m_engine.ready()) {
                // 所有任务完成，调用停止回调
                m_stop_cb();
            } else {
                continue;  // 还有任务，继续执行
            }
        }

        m_engine.poll_submit();
    }
}

}; // namespace coro