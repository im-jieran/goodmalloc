module;
#include <cassert>
#include <cstddef>
#include <expected>
#include <mutex>

#include "shared.hpp"

export module utils;

using std::size_t;

export namespace Utils
{

// 空闲链表
class FreeList
{
private:
    void* free_list = nullptr;

    // 当 tc 中某个大小的块告罄，就需要向 central cache 申请一批新块。
    // 这批新块的数量由慢开始调节算法确定，同时也被内存利用率要求限制。
    // max_application_size 是慢开始调节的参数。
    size_t max_application_size = 1;
    size_t size                 = 0;  // 链表中块的数量

public:
    // 返回 obj 的 next 域的引用，这是一个左值，你可以：
    // get_next_domain(obj) = new_value;
    static void*& get_next_domain(void* obj) { return *reinterpret_cast<void**>(obj); }


    bool empty() const { return free_list == nullptr; }


    // 头插法将 obj 加入空闲链表。
    void push(void* obj)
    {
        get_next_domain(obj) = free_list;
        free_list            = obj;
        size++;
    }


    // 头插法，将一批新块加入空闲链表。
    void push_range(void* start, void* end, size_t block_amount)
    {
        get_next_domain(end) = free_list;
        free_list            = start;
        size += block_amount;
    }


    // 从空闲链表中弹出一个对象，如果链表为空则返回 nullptr。
    void* pop()
    {
        if (free_list == nullptr) return nullptr;

        void* obj = free_list;
        free_list = get_next_domain(obj);
        size--;
        return obj;
    }


    // 从头部移除 n 个对象，并通过 start 和 end 返回被移除的对象
    void pop_range(void*& start, void*& end, size_t n)
    {
        assert(n <= size);
        start = free_list;
        end   = free_list;

        for (size_t i = 0; i < n - 1; i++) { end = get_next_domain(end); }
        free_list = get_next_domain(end);
        size -= n;
        get_next_domain(end) = nullptr;  // 切断链表
    }


    // 获取和设置 max_application_size。
    // 返回值是左值，你可以：
    // get_max_application_size() = new_value;
    size_t& get_max_application_size() { return max_application_size; }

    size_t get_size() const { return size; }
};


// 一些关于 size 的工具函数
class SizeComputer
{
public:
    enum class SizeComputerError
    {
        SizeTooLarge
    };

    // 将 size 向上舍入到最近的对齐边界，并返回舍入后的值
    static std::expected<size_t, SizeComputerError> round_up(size_t size)
    {
        if (size <= 128)
            return (size + 7) & ~7;  // 8B 对齐
        else if (size <= 1024)
            return (size + 15) & ~15;  // 16B 对齐
        else if (size <= 8 * 1024)
            return (size + 127) & ~127;  // 128B 对齐
        else if (size <= 64 * 1024)
            return (size + 1023) & ~1023;  // 1024B 对齐
        else if (size <= MAX_APPLICABLE)
            return (size + 8191) & ~8191;  // 8*1024B 对齐
        // 允许超过 MAX_APPLICABLE。
        // 如果大于 MAX_APPLICABLE，就不经过前两级 cache 了，直接以页为单位向 page cache 申请。
        else
            return (size + ((size_t(1) << PAGE_SHIFT) - 1)) & ~((size_t(1) << PAGE_SHIFT) - 1);  // 按页对齐
    }


    // 根据 size 计算出它应该落在哪个哈希桶中
    // 我们的哈希桶（free_lists）是用 array 实现的，因而也就是计算出 size 应该落在哪个 array 下标上
    static std::expected<size_t, SizeComputerError> freelist_index(size_t size)
    {
        if (size <= 128)
            return (size - 1) >> 3;  // [1,128] 8B对齐，16个桶
        else if (size <= 1024)
            return ((size - 113) >> 4) + 15;  // [129,1024] 16B对齐，56个桶
        else if (size <= 8 * 1024)
            return ((size - 897) >> 7) + 71;  // [1025,8K] 128B对齐，56个桶
        else if (size <= 64 * 1024)
            return ((size - 7169) >> 10) + 127;  // [8K+1,64K] 1024B对齐，56个桶
        else if (size <= MAX_APPLICABLE)
            return ((size - 57345) >> 13) + 183;  // [64K+1,256K] 8KB对齐，24个桶
        else
            return std::unexpected(SizeComputerError::SizeTooLarge);
    }


    // 当 thread cache 向 central cache 申请一批新块时，该函数基于内存利用率和申请效率的要求对 size 进行约束
    static size_t applicable_constraint(size_t size)
    {
        assert(size > 0);
        int block_amount_at_most = static_cast<int>(MAX_APPLICABLE / size);

        // 单个块很小，总的块数就很多。但不能让块数过多，否则会制造浪费。
        if (block_amount_at_most > 512) block_amount_at_most = 512;
        // 单个块很大，总的块数就很少。但不能让块数过少，否则申请效率会很差。
        else if (block_amount_at_most < 2)
            block_amount_at_most = 2;
        return block_amount_at_most;
    }


    // 当 central cache 向 page cache 申请 span 时，需要根据需求的空间大小计算出需求的页面大小。
    static size_t block_size_to_page_amount(size_t space_size)
    {
        size_t block_amount_wanted = applicable_constraint(space_size);
        size_t total_size          = block_amount_wanted * space_size;
        size_t page_amount_wanted  = (total_size + (1 << PAGE_SHIFT) - 1) >> PAGE_SHIFT;

        // 如果需求的块空间不足一个页面，就至少申请一个页面。
        page_amount_wanted = page_amount_wanted < 1 ? 1 : page_amount_wanted;
        return page_amount_wanted;
    }
};


// central cache 和 page cache 管理的基本单位
struct Span
{
    page_id_t page_id;      // 被 Span 管理的首页面页号
    size_t    page_amount;  // 被 Span 管理的页面数量
    size_t    block_size;   // 被 Span 管理的块大小

    void*  free_list;    // Span 托管的小块内存
    size_t used_amount;  // 已经分配出去的小块内存数量

    Span* prev;
    Span* next;

    // 我们需要一个标志位来区分该 span 是否正被 central cache 管理。
    // 不能复用 used_amount，因为 central cache 是在 fetch_range_object 中设置的 used_amount，
    // 但在更早的 get_one_span 中就已经使用了 span（做切分）了。两者不是完全同步的。
    // 于是我们新添一个标志位 used。
    bool used = false;
};


// 带头双向链表，元素是 Span*。
// 用来管理 central cache 和 page cache 中的 Span。
class SpanList
{
private:
    Span* head = nullptr;

public:
    std::mutex the_mutex;  // 每个桶都有一把锁

    SpanList()
    {
        head       = new Span;  // 哨兵位（头节点）
        head->prev = head;
        head->next = head;
    }


    // 在 pos 前面插入一个 span
    void insert(Span* pos, Span* span)
    {
        assert(pos != nullptr);
        assert(span != nullptr);

        Span* prev_span = pos->prev;
        prev_span->next = span;
        span->prev      = prev_span;
        span->next      = pos;
        pos->prev       = span;
    }


    // 从链表中移除一个 span
    void erase(Span* span)
    {
        assert(span != nullptr);
        assert(span != head);

        Span* prev_span = span->prev;
        Span* next_span = span->next;
        prev_span->next = next_span;
        next_span->prev = prev_span;

        // TODO: 回收
    }


    // 获取链表中第一个 span。
    Span* pop_front()
    {
        Span* front = head->next;
        erase(front);
        return front;
    }

    void push_front(Span* span) { insert(head->next, span); }

    Span* begin() const { return head->next; }

    Span* end() const { return head; }

    // 判断该 SpanList 是否托管有 Span
    bool empty() const { return head->next == head; }
};
}  // namespace Utils