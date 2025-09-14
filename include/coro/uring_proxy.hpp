#pragma once

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <liburing.h>
#include <sys/eventfd.h>
#include <vector>
// #ifdef ENABLE_SQPOOL
//     #include <time.h>
// #endif // ENABLE_SQPOOL

#include "config.h"
#include "coro/attribute.hpp"
#include "coro/log.hpp"
#include "coro/marked_buffer.hpp"
#include "coro/utils.hpp"

namespace coro::uring
{
using ursptr     = io_uring_sqe*;   // uring sqe entry指针
using urcptr     = io_uring_cqe*;   // uring cqe entry指针
using urchandler = std::function<void(urcptr)>;   // uring cqe entry回调函数

using uring_fds_item = ::coro::detail::marked_buffer<int, config::kFixFdArraySize>::item;

inline constexpr uring_fds_item invalid_fd_item = uring_fds_item{.idx = -1, .ptr = nullptr};

class uring_proxy
{
public:
    // 构造，初始化uring代理
    uring_proxy() noexcept
    {
        // must init in construct func
        m_efd = eventfd(0, 0);
        if (m_efd < 0)
        {
            log::error("uring_proxy init event_fd failed");
            std::exit(1);
        }
    }

    ~uring_proxy() noexcept = default;

    auto init(unsigned int entry_length) noexcept -> void
    {
        // 1. 初始化uring参数
        // 这里直接置空，没用到
        memset(&m_para, 0, sizeof(m_para));

        // 2. 条件编译：启用 SQPOLL 模式
#ifdef ENABLE_SQPOOL
        m_para.flags |= IORING_SETUP_SQPOLL;
        m_para.sq_thread_idle = config::kSqthreadIdle;
#endif // ENABLE_SQPOOL

        // 3. 初始化uring队列
        auto res = io_uring_queue_init_params(entry_length, &m_uring, &m_para);
        if (res != 0)
        {
            log::error("uring_proxy init uring failed");
            std::exit(1);
        }

        // 4. 绑定eventfd文件描述符
        res = io_uring_register_eventfd(&m_uring, m_efd);
        if (res != 0)
        {
            log::error("uring_proxy bind event_fd to uring failed");
            std::exit(1);
        }

        // 5. 条件编译：启用固定文件描述符
        if constexpr (config::kEnableFixfd)
        {
            // 5.1 初始化固定文件描述符缓冲区
            m_fds.init();

            // 5.2 准备空文件描述符
            m_null_fds = std::vector<int>{};
            for (int i = 0; i < config::kFixFdArraySize; i++)
            {
                // 5.2.1 获取空文件描述符
                auto fd = ::coro::utils::get_null_fd();
                if (fd <= 0)
                {
                    log::error("open null fd failed");
                    std::exit(1);
                }
                // 5.2.2 将空文件描述符添加到空文件描述符列表
                m_null_fds.push_back(fd);
            }

            // 5.3 设置固定文件描述符缓冲区
            m_fds.set_data(m_null_fds);

            // 5.4 注册固定文件描述符
            res = io_uring_register_files(&m_uring, m_fds.data, config::kFixFdArraySize);
            if (res != 0)
            {
                // 5.4.1 注册失败
                log::error("uring_proxy register files failed");
                std::exit(1);
            }
        }
    }

    auto deinit() noexcept -> void
    {
        // this operation cost too much time, so don't call this function
        // io_uring_unregister_eventfd(&m_uring);
        close(m_efd);
        m_efd = -1;

        if constexpr (config::kEnableFixfd)
        {
            for (auto fd : m_null_fds)
            {
                close(fd);
            }
        }

        io_uring_queue_exit(&m_uring);
    }

    /**
     * @brief return if uring has finished io
     * 非阻塞检查是否有完成的 I/O 操作
     * @note no-block function
     *
     * @return true
     * @return false
     */
    auto peek_uring() noexcept -> bool
    {
        urcptr cqe{nullptr};
        io_uring_peek_cqe(&m_uring, &cqe);
        return cqe != nullptr;   // 如果有完成的 I/O 操作，则返回 true
    }

    /**
     * @brief wait the number of finished io
     * 阻塞等待指定数量的 I/O 完成
     * @note block function
     *
     * @param num
     */
    auto wait_uring(int num = 1) noexcept -> void
    {
        urcptr cqe;
        if (num == 1) [[likely]]
        {
            io_uring_wait_cqe(&m_uring, &cqe);
        }
        else
        {
            io_uring_wait_cqe_nr(&m_uring, &cqe, num);   // 阻塞等待指定数量的 I/O 完成
        }
    }

    /**
     * @brief mark the cqe entry has beed processed
     * 标记 CQE 条目已处理，通知内核释放对应的CQE条目
     * @param cqe
     */
    inline auto seen_cqe_entry(urcptr cqe) noexcept -> void CORO_INLINE
    {
        io_uring_cqe_seen(&m_uring, cqe);
    }

    /**
     * @brief get the free uring sqe
     * 获取空闲的 uring sqe entry
     * @return ursptr
     */
    inline auto get_free_sqe() noexcept -> ursptr CORO_INLINE
    {
        return io_uring_get_sqe(&m_uring);
    }

    /**
     * @brief submit all sqe entry and return the number of submitted sqe entry
     * 提交所有 sqe entry 并返回提交的 sqe entry 数量
     * @return int
     */
    inline auto submit() noexcept -> int CORO_INLINE
    {
        return io_uring_submit(&m_uring);
    }

    /**
     * @brief use io_uring_for_each_cqe to process cqe entry
     * 使用 io_uring_for_each_cqe 处理 cqe entry
     * 高效处理多个 CQE 条目
     * @param f 处理CQE的回调函数
     * @param mark_finish
     * @return size_t  处理的CQE数目
     */
    auto handle_for_each_cqe(urchandler f, bool mark_finish = false) noexcept -> size_t
    {
        urcptr   cqe;
        unsigned head;
        unsigned i = 0;
        io_uring_for_each_cqe(&m_uring, head, cqe)
        {
            f(cqe);
            i++;
        };
        if (mark_finish)
        {
            cq_advance(i);
        }
        return i;
    }

    /**
     * @brief wait eventfd
     * 阻塞等待 eventfd 有数据可读
     * @return uint64_t
     */
    auto wait_eventfd() noexcept -> uint64_t
    {
        uint64_t u;
        auto     ret = eventfd_read(m_efd, &u);
        assert(ret != -1 && "eventfd read error");
        return u;
    }

    /**
     * @brief batch fetch cqe entry
     * 批量获取 CQE 条目
     * @param cqes
     * @param num
     * @return int CORO_INLINE
     */
    inline auto peek_batch_cqe(urcptr* cqes, unsigned int num) noexcept -> int CORO_INLINE
    {
        return io_uring_peek_batch_cqe(&m_uring, cqes, num);
    }

    /**
     * @brief write eventfd
     * 写入 eventfd 数据
     * @param num
     */
    inline auto write_eventfd(uint64_t num) noexcept -> void CORO_INLINE
    {
        auto ret = eventfd_write(m_efd, num);
        assert(ret != -1 && "eventfd write error");
    }

    /**
     * @brief an efficient way of seen cqe entry
     * 通知内核已处理完指定数量的 CQE，释放对应的CQE条目
     * @param num
     */
    inline auto cq_advance(unsigned int num) noexcept -> void CORO_INLINE
    {
        io_uring_cq_advance(&m_uring, num);
    }

    /**
     * @brief Get one fixed fd
     * 获取一个固定文件描述符
     * @return uring_fds_item, if fds is empty, uring_fds_item.index will be less than 0
     */
    auto get_fixed_fd() noexcept -> uring_fds_item CORO_INLINE
    {
        if constexpr (!config::kEnableFixfd)
        {
            return invalid_fd_item;
        }
        return m_fds.borrow();
    }

    /**
     * @brief return back fixed fd
     * 返回一个固定文件描述符
     * @param item
     */
    auto back_fixed_fd(uring_fds_item item) noexcept -> void CORO_INLINE
    {
        if (!item.valid())
        {
            return;
        }
        m_fds.data[item.idx] = m_null_fds[item.idx];
        update_register_fixed_fds(item.idx); // change back to null fd
        m_fds.return_back(item);
    }

    /**
     * @brief update register fixed fds
     * 更新注册的固定文件描述符
     * @param index
     */
    auto update_register_fixed_fds([[CORO_MAYBE_UNUSED]] int index) noexcept -> void
    {
        if constexpr (config::kEnableFixfd)
        {
            // TODO: Why local update is incorrect?
            // io_uring_register_files_update(&m_uring, index, m_fds.data, 1)

            auto res = io_uring_register_files_update(&m_uring, 0, m_fds.data, config::kFixFdArraySize);
            if (res != config::kFixFdArraySize)
            {
                log::error("update register files failed, result: {}", res);
                std::exit(1);
            }
        }
    }

private:
    int             m_efd{0};   // eventfd文件描述符，一个io_uring实例只能绑定一个eventfd文件描述符
    io_uring_params m_para;   // uring参数
    io_uring        m_uring;   // uring实例

    // Use m_fds to utilize the IOSQE_FIXED_FILE feature of io_uring
    std::vector<int>                                            m_null_fds;   // 空文件描述符
    ::coro::detail::marked_buffer<int, config::kFixFdArraySize> m_fds;   // 固定文件描述符缓冲区(池)
};

}; // namespace coro::uring