module;
#include <sys/mman.h>

#include <array>
#include <cassert>
#include <mutex>
#include <new>
#include <unordered_map>

#include "shared.hpp"
export module page_cache;

import utils;

export class PageCache
{
private:
    std::array<Utils::SpanList, PAGE_CACHE_MAX_BUCKETS> span_lists;

    std::unordered_map<page_id_t, Utils::Span*> page_id_to_span;  // page_id 到 span 的映射表

    static PageCache page_cache_instance;

private:
    PageCache()                            = default;
    PageCache(const PageCache&)            = delete;
    PageCache& operator=(const PageCache&) = delete;

public:
    static PageCache* get_instance() { return &page_cache_instance; }
    std::mutex        the_mutex;


    // 返回一个管理 pages_amount 个页面的 span。
    // 供外部调用。
    Utils::Span* new_span(size_t pages_amount)
    {
        assert(pages_amount > 0);

        if (pages_amount < PAGE_CACHE_MAX_BUCKETS)
        {
            // 1. page_amount 对应的桶有可用 span
            if (!span_lists[pages_amount].empty())
            {
                Utils::Span* span = span_lists[pages_amount].pop_front();  // 获取该桶的一个 span
                for (page_id_t i = 0; i < span->page_amount; i++) { page_id_to_span[span->page_id + i] = span; }
                return span;
            }

            // 2. page_amount 对应的桶没有可用 span，但更大的桶有可用 span
            for (size_t i = pages_amount + 1; i < PAGE_CACHE_MAX_BUCKETS; ++i)
            {
                if (!span_lists[i].empty())
                {
                    Utils::Span* span_got = span_lists[i].pop_front();  // 获取该桶的一个 span，即 span_got

                    // 将 span_got 分割成一个 pages_amount 大小的和一个 (MAX - pages_amount) 大小的。
                    // 这是前者，准备返回。
                    Utils::Span* span_to_return = new Utils::Span;
                    span_to_return->page_id     = span_got->page_id;
                    span_to_return->page_amount = pages_amount;
                    // 这是后者，准备放回桶里。
                    span_got->page_id += pages_amount;
                    span_got->page_amount -= pages_amount;
                    span_lists[span_got->page_amount].push_front(span_got);
                    // 记录 span_got（留下来的那部分）的边缘页号的映射，以便后续合并。
                    page_id_to_span[span_got->page_id]                             = span_got;
                    page_id_to_span[span_got->page_id + span_got->page_amount - 1] = span_got;
                    // 记录 span_to_return 中的 page_id 到 span 的映射关系。
                    for (page_id_t i = 0; i < span_to_return->page_amount; i++)
                    {
                        page_id_to_span[span_to_return->page_id + i] = span_to_return;
                    }

                    return span_to_return;
                }
            };

            // 3. page_amount 对应的桶没有可用 span，更大的桶也没有可用 span
            // 直接向 OS 申请最大的 span（128 页）。
            void* mem_got = mmap(
                nullptr,
                (PAGE_CACHE_MAX_BUCKETS - 1) * (1 << PAGE_SHIFT),
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS,
                -1,
                0
            );
            if (mem_got == MAP_FAILED) { throw std::bad_alloc(); }

            // 把申请下来的内存挂载到最大的 span 桶里
            Utils::Span* span_from_os = new Utils::Span;
            span_from_os->page_id     = (size_t)mem_got >> PAGE_SHIFT;
            span_from_os->page_amount = PAGE_CACHE_MAX_BUCKETS - 1;
            span_lists[PAGE_CACHE_MAX_BUCKETS - 1].push_front(span_from_os);

            // 递归调用自己，复用前面的分割逻辑
            return new_span(pages_amount);
        }
        else
        {
            void* mem_got = mmap(
                nullptr, pages_amount * (1 << PAGE_SHIFT), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0
            );
            if (mem_got == MAP_FAILED) { throw std::bad_alloc(); }

            Utils::Span* span_from_os = new Utils::Span;
            span_from_os->page_id     = (size_t)mem_got >> PAGE_SHIFT;
            span_from_os->page_amount = pages_amount;
            span_from_os->block_size  = pages_amount << PAGE_SHIFT;

            // 记录该超大 span 的边缘页号的映射
            page_id_to_span[span_from_os->page_id]                                 = span_from_os;
            page_id_to_span[span_from_os->page_id + span_from_os->page_amount - 1] = span_from_os;

            return span_from_os;
        }
    }


    // 工具函数：通过块地址找到它所在的 span。
    Utils::Span* map_object_to_span(void* obj)
    {
        page_id_t                    page_id = ((page_id_t)obj) >> PAGE_SHIFT;  // 通过块地址计算页号
        std::unique_lock<std::mutex> lc(the_mutex);
        auto                         it = page_id_to_span.find(page_id);  // 通过页号找到 span
        assert(it != page_id_to_span.end());                              // 逻辑上一定能找到，否则就是出错了
        return it->second;
    }


    // central cache 将 span 归还给 page cache.
    //
    // 1. 先向左，再向右，尝试与相邻的 span 合并。如果没有可合并的 span，就停止。
    // 2. 相邻的 span 如果正被 central cache 使用，则不能合并。
    // 3. 相邻的 span 和自己合并后如果超过 128 页，就不能合并。
    void release_span_to_page_cache(Utils::Span* span)
    {
        // 先向左
        while (true)
        {
            page_id_t left_page_id = span->page_id - 1;
            auto      it           = page_id_to_span.find(left_page_id);
            if (it == page_id_to_span.end()) break;  // 没有左邻 span, 停止
            Utils::Span* left_span = it->second;
            if (left_span->used) break;  // 左邻 span 正被 central cache 使用, 停止
            if (span->page_amount + left_span->page_amount > PAGE_CACHE_MAX_BUCKETS - 1)
                break;  // 合并后超过最大页数, 停止

            // 现在可以合并
            span->page_id = left_span->page_id;
            span->page_amount += left_span->page_amount;

            span_lists[left_span->page_amount].erase(left_span);  // 从桶里移除左邻 span
            delete left_span;                                     // 释放左邻 span 的内存
        }

        // 再向右
        while (true)
        {
            page_id_t right_page_id = span->page_id + span->page_amount;
            auto      it            = page_id_to_span.find(right_page_id);
            if (it == page_id_to_span.end()) break;  // 没有右邻 span, 停止
            Utils::Span* right_span = it->second;
            if (right_span->used) break;  // 右邻 span 正被 central cache 使用, 停止
            if (span->page_amount + right_span->page_amount > PAGE_CACHE_MAX_BUCKETS - 1)
                break;  // 合并后超过最大页数, 停止

            // 现在可以合并。向右的合并不需要调整 page_id，因为 span 的 page_id 标记的是左边界。
            span->page_amount += right_span->page_amount;

            span_lists[right_span->page_amount].erase(right_span);  // 从桶里移除右邻 span
            delete right_span;                                      // 释放右邻 span 的内存
        }

        span_lists[span->page_amount].push_front(span);  // 把合并后的 span 挂回桶里
        span->used = false;                              // 标记 span 不再被 central cache 使用

        // 映射边缘页
        page_id_to_span[span->page_id]                         = span;
        page_id_to_span[span->page_id + span->page_amount - 1] = span;
    }
};
PageCache PageCache::page_cache_instance;