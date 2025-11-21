#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include "../include/MemPool.h"

// 测试用例
class P1 { int id_; };
class P2 { int id_[5]; };
class P3 { int id_[10]; };
class P4 { int id_[20]; int value_[5]; };

// 单轮次申请释放次数 线程数 轮次
void BenchmarkMemoryPool(size_t ntimes, size_t nworks, size_t rounds)
{
    std::vector<std::thread> vthread(nworks);
    std::vector<size_t> thread_costtime(nworks, 0);

    for (size_t k = 0; k < nworks; ++k)
    {
        vthread[k] = std::thread([&, k]() {
            for (size_t j = 0; j < rounds; ++j)
            {
                auto begin1 = std::chrono::high_resolution_clock::now();
                for (size_t i = 0; i < ntimes; i++)
                {
                    P1* p1 = newElement<P1>();
                    deleteElement<P1>(p1);
                    P2* p2 = newElement<P2>();
                    deleteElement<P2>(p2);
                    P3* p3 = newElement<P3>();
                    deleteElement<P3>(p3);
                    P4* p4 = newElement<P4>();
                    deleteElement<P4>(p4);
                }
                auto end1 = std::chrono::high_resolution_clock::now();
                thread_costtime[k] += std::chrono::duration_cast<std::chrono::milliseconds>(end1 - begin1).count();
            }
            });
    }
    for (auto& t : vthread) { t.join(); }

    size_t total_costtime = 0;
    for (auto t : thread_costtime) total_costtime += t;

    std::cout << "P1 size: " << sizeof(P1) << std::endl;
    std::cout << "P2 size: " << sizeof(P2) << std::endl;
    std::cout << "P3 size: " << sizeof(P3) << std::endl;
    std::cout << "P4 size: " << sizeof(P4) << std::endl;
    std::cout << "==========================================================================" << std::endl;
    std::cout << nworks << " threads executed "
        << rounds << " rounds concurrently, each round newElement & deleteElement "
        << ntimes << " times, total time cost: "
        << total_costtime << " ms\n";
}


void BenchmarkNew(size_t ntimes, size_t nworks, size_t rounds)
{
    std::vector<std::thread> vthread(nworks);
    size_t total_costtime = 0;
    for (size_t k = 0; k < nworks; ++k)
    {
        vthread[k] = std::thread([&]() {
            for (size_t j = 0; j < rounds; ++j)
            {
                size_t begin1 = clock();
                for (size_t i = 0; i < ntimes; i++)
                {
                    P1* p1 = new P1;
                    delete p1;
                    P2* p2 = new P2;
                    delete p2;
                    P3* p3 = new P3;
                    delete p3;
                    P4* p4 = new P4;
                    delete p4;
                }
                size_t end1 = clock();
                total_costtime += end1 - begin1;
            }
            });
    }
    for (auto& t : vthread) { t.join(); }
    std::cout << nworks << " threads executed "
        << rounds << " rounds concurrently, each round malloc & free "
        << ntimes << " times, total time cost: "
        << total_costtime << " ms\n";
}


// MemPoolAllocator 基本功能测试
void TestMemPoolAllocatorBasic() {
    std::cout << "==== MemPoolAllocator Basic Function Test ====" << std::endl;
    std::vector<P1, MemPoolAllocator<P1>> v1;
    for (int i = 0; i < 10; ++i) {
        v1.emplace_back();
    }
    std::cout << "v1.size() = " << v1.size() << std::endl;
    v1.clear();
    std::cout << "v1 cleared, size = " << v1.size() << std::endl;

    std::vector<P2, MemPoolAllocator<P2>> v2(5);
    std::cout << "v2.size() = " << v2.size() << std::endl;
    v2.clear();
    std::cout << "v2 cleared, size = " << v2.size() << std::endl;
}

// MemPoolAllocator 扩容与多类型测试
void TestMemPoolAllocatorAdvanced() {
    std::cout << "==== MemPoolAllocator Advanced Test ====" << std::endl;
    std::vector<P3, MemPoolAllocator<P3>> v3;
    for (int i = 0; i < 50; ++i) {
        v3.push_back(P3());
    }
    std::cout << "v3.size() = " << v3.size() << std::endl;

    std::vector<P4, MemPoolAllocator<P4>> v4;
    v4.reserve(20);
    for (int i = 0; i < 20; ++i) {
        v4.emplace_back();
    }
    std::cout << "v4.size() = " << v4.size() << std::endl;
}

// 简单性能对比
void TestMemPoolAllocatorPerformance() {
    std::cout << "==== MemPoolAllocator Performance Test ====" << std::endl;
    auto t1 = std::chrono::high_resolution_clock::now();
    std::vector<P1, MemPoolAllocator<P1>> v;
    for (int i = 0; i < 10000; ++i) v.emplace_back();
    auto t2 = std::chrono::high_resolution_clock::now();
    std::cout << "MemPoolAllocator: "
        << std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count()
        << " us" << std::endl;

    t1 = std::chrono::high_resolution_clock::now();
    std::vector<P1> vstd;
    for (int i = 0; i < 10000; ++i) vstd.emplace_back();
    t2 = std::chrono::high_resolution_clock::now();
    std::cout << "std::allocator: "
        << std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count()
        << " us" << std::endl;
}

// 验证内存池分配日志
void TestMemPoolAllocatorLog() {
    std::cout << "==== MemPoolAllocator Block Allocation Log ====" << std::endl;
    std::vector<P1, MemPoolAllocator<P1>> vlog;
    for (int i = 0; i < 200; ++i) vlog.emplace_back();
    vlog.clear();
}

int main()
{
    HashBucket::initMemoryPool(); // 使用内存池接口前一定要先调用该函数

    BenchmarkMemoryPool(500, 20, 10); // 测试内存池
    std::cout << "===========================================================================" << std::endl;
    BenchmarkNew(500, 20, 10); // 测试 new delete

	std::cout << "===========================================================================" << std::endl;
    TestMemPoolAllocatorBasic();
    TestMemPoolAllocatorAdvanced();
    TestMemPoolAllocatorPerformance();
    TestMemPoolAllocatorLog();

    return 0;
}