#include <gtest/gtest.h>
// Since page_cache.cppm is a module, we might not be able to do standard unit testing
// directly without C++20 module support in testing, but we can test it indirectly
// via the goodmalloc API or mock its behavior if it exposes specific headers.
// Assuming we're testing the overall memory management behavior and boundaries here.

#include <vector>

#include "goodmalloc.h"

// Testing page-level boundaries
TEST(PageCacheTest, LargePageRequests)
{
    // Standard pages are usually 4KB or 8KB. Test allocations larger than that.
    const size_t large_size = 1024 * 64;  // 64KB
    void*        ptr        = goodalloc(large_size);
    EXPECT_NE(ptr, nullptr);

    // Write pattern to ensure no obvious segfault and memory is backed
    char* char_ptr = static_cast<char*>(ptr);
    for (size_t i = 0; i < large_size; ++i) { char_ptr[i] = static_cast<char>(i % 256); }

    // Verify pattern
    for (size_t i = 0; i < large_size; ++i) { EXPECT_EQ(char_ptr[i], static_cast<char>(i % 256)); }

    goodfree(ptr);
}

// Test edge case allocations mapping to spans/pages
TEST(PageCacheTest, ExactPageMultiples)
{
    std::vector<void*> blocks;
    // Allocate 10 chunks of 4KB
    for (int i = 0; i < 10; ++i)
    {
        void* ptr = goodalloc(4096);
        EXPECT_NE(ptr, nullptr);
        blocks.push_back(ptr);
    }

    for (void* p : blocks) { goodfree(p); }
}
