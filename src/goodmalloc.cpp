#include "goodmalloc.h"

#include <cstddef>

#include "shared.hpp"

import thread_cache;
import page_cache;

import utils;

extern "C"
{
    void* goodalloc(size_t size)
    {
        if (size > MAX_APPLICABLE)
        {
            // 如果申请的空间超过了 MAX_APPLICABLE，就直接以页为单位向 page cache 申请。
            size_t aligned_size       = Utils::SizeComputer::round_up(size).value();      // 对齐后的大小
            size_t page_amount_wanted = aligned_size >> PAGE_SHIFT;                       // 需要的页数
            PageCache::get_instance()->the_mutex.lock();                                  // 上锁
            Utils::Span* span = PageCache::get_instance()->new_span(page_amount_wanted);  // 得到的空间
            PageCache::get_instance()->the_mutex.unlock();                                // 解锁
            void* ptr = (void*)(span->page_id << PAGE_SHIFT);                             // 转换成地址
            return ptr;
        }
        else
        {
            ThreadCache* p_thread_cache = ThreadCache::get_instance();  // thread cache 唯一实例
            return p_thread_cache->allocate(size);
        }
    }

    void goodfree(void* ptr)
    {
        Utils::Span* span      = PageCache::get_instance()->map_object_to_span(ptr);  // 通过地址找到它所在的 span
        size_t       span_size = span->block_size;                                    // 该 span 管理的块大小

        if (span_size > MAX_APPLICABLE)
        {
            // 如果该 span 管理的块大小超过了 MAX_APPLICABLE，就直接以页为单位向 page cache 归还。
            PageCache::get_instance()->the_mutex.lock();  // 上锁
            PageCache::get_instance()->release_span_to_page_cache(span);
            PageCache::get_instance()->the_mutex.unlock();  // 解锁
        }
        else
        {
            ThreadCache* p_thread_cache = ThreadCache::get_instance();  // thread cache 唯一实例
            p_thread_cache->deallocate(ptr, span_size);                 // 归还给 thread cache
        }
    }
}  // extern "C"
