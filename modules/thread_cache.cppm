module;
#include <array>
#include <cassert>
#include <cstddef>

#include "shared.hpp"

export module thread_cache;

import utils;
import central_cache;


using std::array;
using std::size_t;

export class ThreadCache
{
private:
    // | size 范围          | 对齐标准  | 对应哈希桶下表范围 |
    // | ------------------ | -------- | ------------------ |
    // | [1,128]            | 8B       | [0,15]            |
    // | (128,1024]         | 16B      | [16,71]           |
    // | (1024,8*1024]      | 128B     | [72,127]          |
    // | (8*1024,64*1024]   | 1024B    | [128,183]         |
    // | (64*1024,256*1024] | 8*1024B  | [184,208]         |
    array<Utils::FreeList, FREELIST_MAX_SCALE> free_lists;

public:
    // 分配一块空间，大小至少为 size 字节
    void* allocate(std::size_t size)
    {
        auto result       = Utils::SizeComputer::round_up(size);
        auto align_size   = result.value();
        auto index_result = Utils::SizeComputer::freelist_index(align_size);
        auto index        = index_result.value();

        if (free_lists[index].empty())
            return fetch_from_central_cache(index, align_size);
        else
            return free_lists[index].pop();
    }

    // 将 obj 归还给 ThreadCache
    // obj 必须是之前通过 ThreadCache::allocate 分配的
    void deallocate(void* obj, size_t size)
    {
        assert(obj != nullptr);
        assert(size <= MAX_APPLICABLE);

        auto index_result = Utils::SizeComputer::freelist_index(size);
        auto index        = index_result.value();  // 找到 size 对应的自由链表
        free_lists[index].push(obj);               // 回收
    }

    // 从 central cache 中获取空间。
    // 当 thread cache 中对应 size 的块告罄时，调用这个函数。
    void* fetch_from_central_cache(size_t index, size_t align_size)
    {
        size_t amount_to_fetch = std::min(
            free_lists[index].get_max_application_size(), Utils::SizeComputer::applicable_constraint(align_size)
        );

        // 如果没有达到 applicable constraint 的上限，下次可以多给几块。
        if (amount_to_fetch == free_lists[index].get_max_application_size())
        {
            free_lists[index].get_max_application_size() += 1;
        }

        void*  start = nullptr;
        void*  end   = nullptr;
        size_t actually_fetched =
            CentralCache::get_instance().fetch_range_object(start, end, amount_to_fetch, align_size);

        if (actually_fetched == 1)
        {
            assert(start == end);
            return start;
        }
        else
        {
            free_lists[index].push_range(Utils::FreeList::get_next_domain(start), end, actually_fetched - 1);
            return start;
        }
        return nullptr;  // 临时返回 nullptr 避免编译警告
    }

    // 获取 thread local 的 ThreadCache 实例。懒加载。
    static ThreadCache* get_instance()
    {
        static thread_local ThreadCache instance;
        return &instance;
    }


    // 当 thread cache 中某个桶的自由链表过长，就调用该函数向 central cache 归还一些块。
    // “自由链表过长”的标准：当空闲块的数量超过 slow start 调节算法的参数时，就认为过长了。
    // list：要归还的自由链表。
    // size：该自由链表装载的块大小。
    void list_too_long(Utils::FreeList& list, size_t size)
    {
        void* start = nullptr;
        void* end   = nullptr;
        list.pop_range(start, end, list.get_max_application_size());
        CentralCache::get_instance().release_list_to_spans(start, size);
    }
};