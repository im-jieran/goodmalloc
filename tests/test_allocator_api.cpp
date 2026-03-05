#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "goodmalloc.h"

// Test basic allocation and deallocation
TEST(AllocatorApiTest, BasicAllocFree)
{
    void* ptr1 = goodalloc(128);
    EXPECT_NE(ptr1, nullptr);

    void* ptr2 = goodalloc(256);
    EXPECT_NE(ptr2, nullptr);

    goodfree(ptr1);
    goodfree(ptr2);
}

// Test edge case allocations
TEST(AllocatorApiTest, EdgeCases)
{
    void* ptr = goodalloc(1);
    EXPECT_NE(ptr, nullptr);
    goodfree(ptr);

    // Test a reasonably large allocation
    void* large_ptr = goodalloc(64 * 1024);
    EXPECT_NE(large_ptr, nullptr);
    goodfree(large_ptr);
}

// Simple concurrency smoke test
TEST(AllocatorApiTest, ConcurrentAllocations)
{
    auto worker = []()
    {
        std::vector<void*> ptrs;
        for (int i = 0; i < 100; ++i) { ptrs.push_back(goodalloc(64)); }
        for (void* p : ptrs) { goodfree(p); }
    };

    std::thread t1(worker);
    std::thread t2(worker);
    std::thread t3(worker);

    t1.join();
    t2.join();
    t3.join();
}
