module;
#include <cassert>
#include <cstddef>
#include <cstring>
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


// 三层基数树
// 适用于64位系统，能够高效映射大地址空间
// BITS 参数指定了地址空间的位数
template <int BITS>
class PageMap3
{
private:
    // 三层分割策略：
    // ROOT: 10位(1024项), MIDDLE: 10位(1024项), LEAF: 12位(4096项)
    // 总共可以映射 2^32 个页号，足够覆盖实际使用的地址空间
    // 我们没有使用全量 LEAF_BITS，因为那样叶子节点数量会多到无法接受。

    static constexpr int    ROOT_BITS   = 15;
    static constexpr size_t ROOT_LENGTH = 1 << ROOT_BITS;

    static constexpr int    MIDDLE_BITS   = 15;
    static constexpr size_t MIDDLE_LENGTH = 1 << MIDDLE_BITS;

    static constexpr int    LEAF_BITS   = 12;
    static constexpr size_t LEAF_LENGTH = 1 << LEAF_BITS;

    static_assert(BITS >= 32, "PageMap3 至少需要 32 位才有意义");

    // 第三层：叶子节点，存储实际的指针数组
    struct Leaf
    {
        void* values[LEAF_LENGTH];
    };

    // 第二层：中间节点，存储指向叶子节点的指针数组
    struct Middle
    {
        Leaf* leaves[MIDDLE_LENGTH];
    };

    // 第一层：根节点数组，存储指向中间节点的指针
    Middle* root_[ROOT_LENGTH];

public:
    typedef uintptr_t Number;

    explicit PageMap3() { memset(root_, 0, sizeof(root_)); }

    // 通过页号 k 获取对应的指针
    // 返回 NULL 表示未设置或 k 超出范围
    void* get(Number k) const
    {
        // 计算三层索引
        const Number i1 = k >> (MIDDLE_BITS + LEAF_BITS);          // 第一层索引
        const Number i2 = (k >> LEAF_BITS) & (MIDDLE_LENGTH - 1);  // 第二层索引
        const Number i3 = k & (LEAF_LENGTH - 1);                   // 第三层索引

        // 检查是否越界或未分配
        if (i1 >= ROOT_LENGTH || root_[i1] == NULL || root_[i1]->leaves[i2] == NULL) { return NULL; }

        return root_[i1]->leaves[i2]->values[i3];
    }

    // 设置页号 k 对应的指针为 v
    // 约束：k 必须在有效范围内，且对应的路径必须已经通过 Ensure 分配
    void set(Number k, void* v)
    {
        const Number i1 = k >> (MIDDLE_BITS + LEAF_BITS);
        const Number i2 = (k >> LEAF_BITS) & (MIDDLE_LENGTH - 1);
        const Number i3 = k & (LEAF_LENGTH - 1);

        assert(i1 < ROOT_LENGTH);
        assert(root_[i1] != NULL);
        assert(root_[i1]->leaves[i2] != NULL);

        root_[i1]->leaves[i2]->values[i3] = v;
    }

    // 确保从 start 开始往后的 n 页空间已经分配
    // 返回 false 表示分配失败（通常是因为超出地址范围）
    bool ensure(Number start, size_t n)
    {
        for (Number key = start; key <= start + n - 1;)
        {
            const Number i1 = key >> (MIDDLE_BITS + LEAF_BITS);
            const Number i2 = (key >> LEAF_BITS) & (MIDDLE_LENGTH - 1);

            // 检查溢出：i1 不能超过根层的大小
            if (i1 >= ROOT_LENGTH) return false;

            // 如果第二层未分配，则分配一个 Middle 节点
            if (root_[i1] == NULL)
            {
                Middle* middle = new Middle;
                memset(middle, 0, sizeof(*middle));
                root_[i1] = middle;
            }

            // 如果第三层未分配，则分配一个 Leaf 节点
            if (root_[i1]->leaves[i2] == NULL)
            {
                Leaf* leaf = new Leaf;
                memset(leaf, 0, sizeof(*leaf));
                root_[i1]->leaves[i2] = leaf;
            }

            // 跳到下一个叶子节点覆盖的范围
            key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;
        }
        return true;
    }

    // 预分配内存
    // 对于三层树，地址空间可能非常大，通常不预分配所有内存
    // 可以根据实际需求部分预分配以提高性能
    void preallocate_more_memory()
    {
        // 这里采用保守策略：预分配前 64MB 对应的页映射空间
        // 64MB / 4KB = 16384 页
        // 这部分只占用约 16KB 的基数树节点内存（非常小）
        const size_t conservative_preallocate_mb = 64;
        const size_t pages_to_preallocate        = (conservative_preallocate_mb * 1024 * 1024) >> PAGE_SHIFT;

        ensure(0, pages_to_preallocate);
    }
};
}  // namespace Utils