#include <gtest/gtest.h>

#include <cstdint>

import utils;

TEST(PageMap3Test, BasicOperations)
{
    // 创建一个32位地址空间的三层基数树
    Utils::PageMap3<32> pageMap;
    // 测试2: Ensure 分配空间
    bool success = pageMap.ensure(0, 10);
    EXPECT_TRUE(success);

    // 测试3: 设置和获取值
    void* test_ptr1 = reinterpret_cast<void*>(0x12345678);
    pageMap.set(0, test_ptr1);
    void* retrieved1 = pageMap.get(0);
    EXPECT_EQ(retrieved1, test_ptr1);

    // 测试4: 多个位置的设置和获取
    void* test_ptr2 = reinterpret_cast<void*>(0x87654321);
    pageMap.set(5, test_ptr2);
    void* retrieved2 = pageMap.get(5);
    EXPECT_EQ(retrieved2, test_ptr2);
    EXPECT_EQ(pageMap.get(0), test_ptr1);  // 确保之前的值没有被覆盖
}

TEST(PageMap3Test, LargeRange)
{
    Utils::PageMap3<32> pageMap;

    // 测试不同区域的页号
    const uintptr_t test_pages[] = {0, 1000, 10000, 100000, 1000000, 10000000};

    for (size_t i = 0; i < sizeof(test_pages) / sizeof(test_pages[0]); ++i)
    {
        uintptr_t page_num = test_pages[i];

        // 确保空间已分配
        bool success = pageMap.ensure(page_num, 1);
        EXPECT_TRUE(success);

        // 设置和获取值
        void* test_ptr = reinterpret_cast<void*>(0xDEADBEEF + i);
        pageMap.set(page_num, test_ptr);
        void* retrieved = pageMap.get(page_num);
        EXPECT_EQ(retrieved, test_ptr);
    }
}

TEST(PageMap3Test, BatchAllocation)
{
    Utils::PageMap3<32> pageMap;

    // 一次性确保大量连续页面
    const size_t start_page = 1000;
    const size_t page_count = 5000;

    bool success = pageMap.ensure(start_page, page_count);
    EXPECT_TRUE(success);

    // 验证所有页面都可以正常使用
    for (size_t i = 0; i < page_count; i += 100)  // 抽样测试
    {
        uintptr_t page_num = start_page + i;
        void*     test_ptr = reinterpret_cast<void*>(page_num * 1000);
        pageMap.set(page_num, test_ptr);
        void* retrieved = pageMap.get(page_num);
        EXPECT_EQ(retrieved, test_ptr);
    }
}

TEST(PageMap3Test, CrossBoundaries)
{
    Utils::PageMap3<32> pageMap;

    // 计算节点边界
    // 假设 LEAF_BITS = 12 (32 - 10 - 10)
    // 叶子节点覆盖 2^12 = 4096 个页
    const size_t leaf_size   = (1 << 12);
    const size_t middle_size = leaf_size * (1 << 10);  // 中间节点覆盖的范围

    // 测试跨越叶子节点边界
    uintptr_t page_near_leaf_boundary = leaf_size - 5;
    pageMap.ensure(page_near_leaf_boundary, 10);

    for (size_t i = 0; i < 10; ++i)
    {
        void* test_ptr = reinterpret_cast<void*>(0xAAAA0000 + i);
        pageMap.set(page_near_leaf_boundary + i, test_ptr);
        EXPECT_EQ(pageMap.get(page_near_leaf_boundary + i), test_ptr);
    }

    // 测试跨越中间节点边界
    uintptr_t page_near_middle_boundary = middle_size - 5;
    pageMap.ensure(page_near_middle_boundary, 10);

    for (size_t i = 0; i < 10; ++i)
    {
        void* test_ptr = reinterpret_cast<void*>(0xBBBB0000 + i);
        pageMap.set(page_near_middle_boundary + i, test_ptr);
        EXPECT_EQ(pageMap.get(page_near_middle_boundary + i), test_ptr);
    }
}

TEST(PageMap3Test, NullValues)
{
    Utils::PageMap3<32> pageMap;

    pageMap.ensure(0, 100);

    // 设置 nullptr
    pageMap.set(10, nullptr);
    void* retrieved = pageMap.get(10);
    EXPECT_EQ(retrieved, nullptr);

    // 设置零值
    pageMap.set(20, reinterpret_cast<void*>(0));
    retrieved = pageMap.get(20);
    EXPECT_EQ(retrieved, nullptr);
}

TEST(PageMap3Test, AddressSpace64Bit)
{
    Utils::PageMap3<64> pageMap;

    // 测试真实的64位地址场景
    // PageMap3 映射 32 位页号空间，可以覆盖 16TB 地址(2^32 页 * 4KB)
    // 这对绝大多数应用已经足够
    const uintptr_t test_addresses[] = {
        0x0ULL,           // 低地址
        0x1000ULL,        // 4KB 边界
        0x100000ULL,      // 1MB 边界
        0x40000000ULL,    // 1GB 边界
        0x1000000000ULL,  // 64GB 边界（需要跨越多个中间节点）
        0xFFFFFFF000ULL,  // 接近 16TB（最大可映射地址）
    };

    for (size_t i = 0; i < sizeof(test_addresses) / sizeof(test_addresses[0]); ++i)
    {
        uintptr_t address  = test_addresses[i];
        uintptr_t page_num = address >> 12;  // 转换为页号

        // 确保空间已分配
        bool success = pageMap.ensure(page_num, 1);
        EXPECT_TRUE(success);

        // 设置和获取值
        void* test_ptr = reinterpret_cast<void*>(0xCAFEBABE00000000ULL + i);
        pageMap.set(page_num, test_ptr);
        void* retrieved = pageMap.get(page_num);
        EXPECT_EQ(retrieved, test_ptr);
    }

    // 测试典型的内存池场景：从某个基地址开始的连续分配
    // 注意：127TB 的地址超出了 32 位页号的映射范围(16TB)
    // 这里我们使用一个更小的基地址来测试
    uintptr_t base_addr = 0x100000000ULL;   // 4GB 基地址
    uintptr_t base_page = base_addr >> 12;  // 转换为页号

    pageMap.ensure(base_page, 1000);
    for (size_t i = 0; i < 1000; i += 100)
    {
        void* test_ptr = reinterpret_cast<void*>(base_addr + i * 4096);
        pageMap.set(base_page + i, test_ptr);
        EXPECT_EQ(pageMap.get(base_page + i), test_ptr);
    }
}
