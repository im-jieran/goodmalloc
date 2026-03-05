module;
#include <cassert>
#include <cstddef>

#include "shared.hpp"

export module central_cache;

import utils;
import page_cache;


export class CentralCache
{
private:
    Utils::SpanList     span_lists[FREELIST_MAX_SCALE];  // central cache 的哈希结构
    static CentralCache instance;                        // central cache 的单例


    // 从本地或 page cache 中获取一个 span。
    // 供内部使用。
    Utils::Span* get_one_span(Utils::SpanList& list, size_t size)
    {
        // 若本地（list）有管理空间非空的 span，就返回它，否则向 page cache 申请新 span。
        Utils::Span* span_iter = list.begin();
        while (span_iter != list.end())
        {
            if (span_iter->free_list != nullptr)
                return span_iter;
            else
                span_iter = span_iter->next;
        }

        // 接下来，直到挂载 span 前，本函数都不会访问 list，因此可以先解锁，避免长时间占用锁。
        list.the_mutex.unlock();

        // 走到这里，说明 list 中没有管理空间非空的 span。向 page cache 申请新 span，即 new_span。
        size_t page_amount_wanted = Utils::SizeComputer::block_size_to_page_amount(size);
        PageCache::get_instance()->the_mutex.lock();  // 上锁
        Utils::Span* new_span = PageCache::get_instance()->new_span(page_amount_wanted);
        new_span->used        = true;                   // 标记 new_span 已投入使用
        new_span->block_size  = size;                   // 设置 new_span 管理的块大小
        PageCache::get_instance()->the_mutex.unlock();  // 解锁

        // 对 new_span 进行初始化（空间划分）。
        char* start = (char*)(new_span->page_id << PAGE_SHIFT);
        char* end   = (char*)(start + (new_span->page_amount << PAGE_SHIFT));

        new_span->free_list = start;
        void* tail          = start;
        start += size;
        while (start < end)
        {
            Utils::FreeList::get_next_domain(tail) = start;
            start += size;
            tail = Utils::FreeList::get_next_domain(tail);
        }
        Utils::FreeList::get_next_domain(tail) = nullptr;  // 切断链表

        // 挂载 new_span。在挂载前需要加锁。
        list.the_mutex.lock();
        list.insert(list.begin(), new_span);
        return new_span;
    }

private:
    CentralCache()                               = default;  // 禁止访问默认构造函数
    CentralCache(const CentralCache&)            = delete;   // 禁止拷贝构造
    CentralCache& operator=(const CentralCache&) = delete;   // 禁止拷贝赋值

public:
    // 获取 central cache 的单例对象
    static CentralCache& get_instance() { return instance; }


    // 获取 amount_to_fetch 个 size 大小的空间。这些空间将会被组织成空闲链表，并通过 start 和 end 标定位置。
    // 供外部（主要是 thread cache）调用。
    size_t fetch_range_object(void*& start, void*& end, size_t amount_to_fetch, size_t size)
    {
        // 确定哈希桶索引 index。
        auto expected_index = Utils::SizeComputer::freelist_index(size);
        assert(expected_index.has_value());
        size_t index = expected_index.value();

        span_lists[index].the_mutex.lock();  // 上锁

        // 获取一个管理空间非空的 span，即 span_fetched。
        auto span_fetched = get_one_span(span_lists[index], size);

        assert(span_fetched != nullptr);
        assert(span_fetched->free_list != nullptr);

        // 从 span_fetched 的 free_list 中取出 amount_to_fetch 个对象，组成一个链表.
        // 用 start 和 end 标定边界。
        // 若 span_fetched 中的对象不足 amount_to_fetch 个，就尽可能多地取。
        start = end             = span_fetched->free_list;
        size_t actually_fetched = 1;
        size_t i                = 0;
        while (i < amount_to_fetch - 1 && Utils::FreeList::get_next_domain(end) != nullptr)
        {
            end = Utils::FreeList::get_next_domain(end);
            actually_fetched++;
            i++;
        }

        // 将 span_fetched 的 free_list 指向剩余的对象，切断链表。
        span_fetched->free_list = Utils::FreeList::get_next_domain(end);
        span_fetched->used_amount += actually_fetched;
        Utils::FreeList::get_next_domain(end) = nullptr;  // 切断链表
        span_lists[index].the_mutex.unlock();             // 解锁
        return actually_fetched;
    }


    // 将 thread cache 归还的空间放到 central cache span 中。
    // 供外部（主要是 thread cache）调用。
    void release_list_to_spans(void* start, size_t size)
    {
        // 先计算下标
        size_t index = Utils::SizeComputer::freelist_index(size).value();

        // 下面要对 central cache 中的 span 进行修改，先上锁。
        span_lists[index].the_mutex.lock();

        // 遍历每个块，将其放回 span 中。
        while (start != nullptr)
        {
            void* next = Utils::FreeList::get_next_domain(start);  // 先保存下一个地址

            // 通过块地址找到它所在的 span。
            Utils::Span* span = PageCache::get_instance()->map_object_to_span(start);

            // 将块插回 span 的 free_list 中。
            Utils::FreeList::get_next_domain(start) = span->free_list;
            span->free_list                         = start;
            span->used_amount--;

            if (span->used_amount == 0)
            {
                // 如果该 span 管理的所有页都回来了，则把它还给 page cache。
                span->free_list = nullptr;
                span->next      = nullptr;
                span->prev      = nullptr;

                // 归还时，page cache 会合并页号相邻的 span，因此需要加锁。
                PageCache::get_instance()->the_mutex.lock();
                PageCache::get_instance()->release_span_to_page_cache(span);
                PageCache::get_instance()->the_mutex.unlock();
            }

            start = next;  // 继续处理下一个地址
        }
        span_lists[index].the_mutex.unlock();  // 解锁
    }
};
CentralCache CentralCache::instance;  // 单例 central cache 对象