#include "coro/engine.hpp"
#include "coro/io/io_info.hpp"
#include "coro/task.hpp"

namespace coro::detail
{
using std::memory_order_relaxed;

auto engine::init() noexcept -> void
{
    // TODO[lab2a]: Add you codes
    linfo.egn            = this;
    m_num_io_wait_submit = 0;
    m_num_io_running     = 0;
    m_upxy.init(config::kEntryLength);
}

auto engine::deinit() noexcept -> void
{
    // TODO[lab2a]: Add you codes
    m_upxy.deinit();
    m_num_io_wait_submit = 0;
    m_num_io_running     = 0;
    mpmc_queue<coroutine_handle<>> task_queue;
    m_task_queue.swap(task_queue);
}

auto engine::ready() noexcept -> bool
{
    // TODO[lab2a]: Add you codes
    return !m_task_queue.was_empty();
}

auto engine::get_free_urs() noexcept -> ursptr
{
    // TODO[lab2a]: Add you codes
    return m_upxy.get_free_sqe();  // 直接从uring代理获取空闲的
}

auto engine::num_task_schedule() noexcept -> size_t
{
    // TODO[lab2a]: Add you codes
    return m_task_queue.was_size();
}

auto engine::schedule() noexcept -> coroutine_handle<>
{
    // TODO[lab2a]: Add you codes
    coroutine_handle<> task = m_task_queue.pop();   // 乐观pop，如果失败会阻塞
    return task;
}

// 这里只提交非IO任务，可由其他线程调用
auto engine::submit_task(coroutine_handle<> handle) noexcept -> void
{
    // TODO[lab2a]: Add you codes
    m_task_queue.push(handle);
    // 写入eventfd来唤醒可能阻塞在poll_submit中的线程
    m_upxy.write_eventfd(1);
}

auto engine::exec_one_task() noexcept -> void
{
    auto coro = schedule();
    coro.resume();
    if (coro.done())
    {
        clean(coro);
    }
}

auto engine::handle_cqe_entry(urcptr cqe) noexcept -> void
{
    // 从 CQE 中获取绑定的 io_info，并调用回调函数
    // 回调函数会将协程句柄重新提交到任务队列
    auto data = reinterpret_cast<io::detail::io_info*>(io_uring_cqe_get_data(cqe));
    data->cb(data, cqe->res);
}

// 真正提交任务 专注于IO任务
auto engine::poll_submit() noexcept -> void
{
    // 1. 有待提交IO任务，则提交
    if(m_num_io_wait_submit > 0)
    {
        int sqe_count = m_upxy.submit();
        if (sqe_count > 0) {
            m_num_io_wait_submit -= sqe_count;
            m_num_io_running += sqe_count;
        }
    }

    // 检查是否有CQE可处理,没有则阻塞等待
    if (!m_upxy.peek_uring()) {
        m_upxy.wait_eventfd(); // 阻塞等待
    }

    // 2. 处理已经完成的CQE
    int cqe_count = m_upxy.peek_batch_cqe(m_urc.data(), m_urc.size());
    // 批量获取CQE
    if(cqe_count > 0)
    {
        m_num_io_running -= cqe_count;
        for(int i = 0; i < cqe_count; i++)
        {
            handle_cqe_entry(m_urc[i]);
        }
        m_upxy.cq_advance(cqe_count);
    }
}

auto engine::add_io_submit() noexcept -> void
{
    // TODO[lab2a]: Add you codes
    m_num_io_wait_submit += 1;
}

auto engine::empty_io() noexcept -> bool
{
    // TODO[lab2a]: Add you codes
    return m_num_io_wait_submit == 0 && m_num_io_running == 0;
}
}; // namespace coro::detail
