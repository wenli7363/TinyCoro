#pragma once

#include <atomic> 

#ifdef ENABLE_MEMORY_ALLOC
    #include "coro/allocator/memory.hpp"
#endif
#include "coro/attribute.hpp"

namespace coro
{
class context;
}; // namespace coro

namespace coro::detail
{
using config::ctx_id;
using std::atomic;
class engine;

/**
 * @brief store thread local variables
 * 每个线程都有自己的本地信息
 */
// 线程本地信息
// CORO_ALIGN进行对齐优化，减少缓存行污染
struct CORO_ALIGN local_info
{
    context* ctx{nullptr};
    engine*  egn{nullptr};
    // TODO: Add more local var
};

/**
 * @brief store thread shared variables
 * 全局信息是所有线程共享的
 */
struct global_info
{
    atomic<ctx_id>   context_id{0};    // 全局协程上下文ID计数
    atomic<uint32_t> engine_id{0};    // 全局引擎ID计数
// TODO: Add more global var
#ifdef ENABLE_MEMORY_ALLOC
    coro::allocator::memory::memory_allocator<config::kMemoryAllocator>* mem_alloc;
#endif
};

inline thread_local local_info linfo;
inline global_info             ginfo;

// init global info
inline auto init_meta_info() noexcept -> void
{
    // 初始化全局计数器=0
    ginfo.context_id = 0;       
    ginfo.engine_id  = 0;
#ifdef ENABLE_MEMORY_ALLOC
    ginfo.mem_alloc = nullptr;
#endif
}

// This function is used to distinguish whether you are currently in a worker thread
// 判断当前线程是否在执行协程
inline auto is_in_working_state() noexcept -> bool
{
    return linfo.ctx != nullptr;
}
}; // namespace coro::detail