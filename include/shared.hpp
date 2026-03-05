#include <cstddef>

constexpr std::size_t MAX_APPLICABLE     = 256 * 1024;  // ThreadCache::allocate 允许的最大可申请大小
constexpr std::size_t FREELIST_MAX_SCALE = 208;

// page cache 维护的 spanlist 数量。为了方便，索引从 1 开始，总数 128，故该值为 129。
constexpr std::size_t PAGE_CACHE_MAX_BUCKETS = 129;

// 页面大小
constexpr std::size_t PAGE_SHIFT = 12;  // 4KB 页面

using page_id_t = size_t;
