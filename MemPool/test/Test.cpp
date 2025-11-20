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


int main()
{
    HashBucket::initMemoryPool(); // 使用内存池接口前一定要先调用该函数
    BenchmarkMemoryPool(200, 10, 10); // 测试内存池
    std::cout << "===========================================================================" << std::endl;
    BenchmarkNew(200, 10, 10); // 测试 new delete

    return 0;
}