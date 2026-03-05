// 在 nthread 个线程里，对不同大小的内存进行 ntimes 次分配和释放, 以测试性能
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

#include "goodmalloc.h"

// 不同的内存分配大小
const std::vector<size_t> ALLOCATION_SIZES = {
    8,
    16,
    32,
    64,
    128,
    256,
    512,
    1024,  // 小对象
    2048,
    4096,
    8192,  // 中等对象
    16384,
    32768,
    65536  // 大对象
};

// 单个线程的测试函数 - GoodMalloc
void benchmark_thread_goodmalloc(int thread_id, int ntimes, bool verbose)
{
    std::random_device              rd;
    std::mt19937                    gen(rd() + thread_id);
    std::uniform_int_distribution<> size_dist(0, ALLOCATION_SIZES.size() - 1);

    std::vector<void*> allocations;
    allocations.reserve(ntimes);

    // 分配阶段
    for (int i = 0; i < ntimes; ++i)
    {
        size_t size = ALLOCATION_SIZES[size_dist(gen)];
        void*  ptr  = goodalloc(size);
        if (ptr)
        {
            // 写入一些数据以确保内存真正被使用
            memset(ptr, 0xAB, size);
            allocations.push_back(ptr);
        }
    }

    // 释放阶段
    for (void* ptr : allocations) { goodfree(ptr); }

    if (verbose) { std::cout << "Thread " << thread_id << " completed " << ntimes << " allocations\n"; }
}

// 单个线程的测试函数 - new/delete
void benchmark_thread_traditional(int thread_id, int ntimes, bool verbose)
{
    std::random_device              rd;
    std::mt19937                    gen(rd() + thread_id);
    std::uniform_int_distribution<> size_dist(0, ALLOCATION_SIZES.size() - 1);

    std::vector<void*> allocations;
    allocations.reserve(ntimes);

    // 分配阶段
    for (int i = 0; i < ntimes; ++i)
    {
        size_t size = ALLOCATION_SIZES[size_dist(gen)];
        void*  ptr  = ::operator new(size);
        if (ptr)
        {
            // 写入一些数据以确保内存真正被使用
            memset(ptr, 0xAB, size);
            allocations.push_back(ptr);
        }
    }

    // 释放阶段
    for (void* ptr : allocations) { ::operator delete(ptr); }

    if (verbose) { std::cout << "Thread " << thread_id << " completed " << ntimes << " allocations\n"; }
}

// 运行基准测试 - GoodMalloc
void run_benchmark(int nthreads, int ntimes, bool verbose = false)
{
    std::cout << "\n=== GoodMalloc 基准测试：" << nthreads << " 线程，每线程 " << ntimes << " 次分配 ===\n";

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(nthreads);

    // 启动所有线程
    for (int i = 0; i < nthreads; ++i) { threads.emplace_back(benchmark_thread_goodmalloc, i, ntimes, verbose); }

    // 等待所有线程完成
    for (auto& t : threads) { t.join(); }

    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    int    total_ops   = nthreads * ntimes;
    double ops_per_sec = (total_ops * 1000.0) / duration.count();

    std::cout << "总操作数: " << total_ops << "\n";
    std::cout << "总时间: " << duration.count() << " ms\n";
    std::cout << "吞吐量: " << ops_per_sec << " ops/sec\n";
    std::cout << "平均每次操作: " << (duration.count() * 1000.0 / total_ops) << " μs\n";
}

// 运行基准测试 - new/delete
void run_benchmark_new_delete(int nthreads, int ntimes, bool verbose = false)
{
    std::cout << "\n=== new/delete 基准测试：" << nthreads << " 线程，每线程 " << ntimes << " 次分配 ===\n";

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(nthreads);

    // 启动所有线程
    for (int i = 0; i < nthreads; ++i) { threads.emplace_back(benchmark_thread_traditional, i, ntimes, verbose); }

    // 等待所有线程完成
    for (auto& t : threads) { t.join(); }

    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    int    total_ops   = nthreads * ntimes;
    double ops_per_sec = (total_ops * 1000.0) / duration.count();

    std::cout << "总操作数: " << total_ops << "\n";
    std::cout << "总时间: " << duration.count() << " ms\n";
    std::cout << "吞吐量: " << ops_per_sec << " ops/sec\n";
    std::cout << "平均每次操作: " << (duration.count() * 1000.0 / total_ops) << " μs\n";
}

int main(int argc, char* argv[])
{
    int nthreads = 4;
    int ntimes   = 10000;

    // 解析命令行参数
    if (argc >= 2) { nthreads = std::atoi(argv[1]); }
    if (argc >= 3) { ntimes = std::atoi(argv[2]); }

    std::cout << "GoodMalloc 性能基准测试\n";
    std::cout << "========================\n";
    std::cout << "测试内存大小: ";
    for (size_t size : ALLOCATION_SIZES) { std::cout << size << " "; }
    std::cout << "字节\n";

    // 运行不同配置的测试
    std::cout << "\n--- 单线程测试 ---\n";
    run_benchmark(1, ntimes);
    run_benchmark_new_delete(1, ntimes);

    std::cout << "\n--- 多线程测试 ---\n";
    run_benchmark(nthreads, ntimes);
    run_benchmark_new_delete(nthreads, ntimes);

    std::cout << "\n--- 高并发测试 ---\n";
    run_benchmark(nthreads * 2, ntimes / 2);
    run_benchmark_new_delete(nthreads * 2, ntimes / 2);

    std::cout << "\n测试完成！\n";

    return 0;
}
