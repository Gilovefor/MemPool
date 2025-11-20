#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <algorithm>
#include <vector>

#define MEMORY_POOL_NUM 26
#define SLOT_BASE_SIZE 8
#define MAX_SLOT_SIZE 512

static size_t g_slotSizes[MEMORY_POOL_NUM];	// 每个内存池对应的slot大小
static bool g_slotSizeInitialized = false;	// 是否初始化过slot大小数组


struct BlockHeader {
	BlockHeader* next;
	std::atomic<size_t> usedSlots; // 当前Block已分配slot数
};

struct alignas(std::max_align_t) Slot
{
	std::atomic<Slot*> next;
};


class MemoryPool
{
public:
	MemoryPool(size_t BlockSize_ = 4096);
	~MemoryPool();
	
	void init(size_t);
	void* allocate();
	void deallocate(void*);

	size_t getBlockSize() const { return BlockSize_; }
	void setBlockSize(size_t size) { BlockSize_ = size; }
	int getSlotSize() const { return SlotSize_; }

	//调试接口
	//size_t getTotalAllocated() const;	// 已分配总内存大小
	//size_t getUsedSlots() const;		// 已使用的slot数量	
	//void dumpStatus(std::ostream& os);	// 输出内存池状态信息

	//辅助接口
	//BlockHeader* findBlockHeader(Slot* slot);
	void removeBlock(BlockHeader* header);

private:
	void allocateNewBlock();
	size_t padPointer(char* p, size_t align);

	bool pushFreeList(Slot* slot);
	Slot* popFreeList();

private:
	static thread_local Slot* threadFreeList_;
	size_t BlockSize_;
	size_t initialBlockSize_; // 记录最初的块大小
	int SlotSize_;


	BlockHeader* firstBlock_;
	Slot* curSlot_;
	std::atomic<Slot*> freeList_;
	Slot* lastSlot_;
	std::mutex mutexForBlock_;
};

class HashBucket
{
public:
	static void initMemoryPool();
	static MemoryPool& getMemoryPool(int index);
	static int getIndexBySize(size_t size);

	static void* useMemory(size_t size) {
		if (size <= 0)
			return nullptr;
		if (size > MAX_SLOT_SIZE)	// 超过最大内存池分配大小，直接使用全局new
			return ::operator new(size);

		int index = getIndexBySize(size);
		if (index < 0)
			return ::operator new(size);

		return getMemoryPool(index).allocate();
	}

	static void freeMemory(void* ptr, size_t size) {
		if(!ptr)
			return;
		if (size > MAX_SLOT_SIZE) {
			operator delete(ptr);
			return;
		}
		int index = getIndexBySize(size);
		if (index < 0) {
			operator delete(ptr);
			return;
		}

		getMemoryPool(index).deallocate(ptr);
	}

	template<typename T, typename... Args>
	friend T* newElement(Args&&... args);

	template<typename T>
	friend void deleteElement(T* p);
};

template<typename T, typename...Args>
T* newElement(Args&&... args) {
	assert(sizeof(T) <= MAX_SLOT_SIZE && "Object too large for memory pool!");

	T* p = nullptr;
	if ((p =static_cast<T*>(HashBucket::useMemory(sizeof(T)))) != nullptr) {
		new (p) T(std::forward<Args>(args)...);
	}

	return p;
}

template<typename T>
void deleteElement(T* p) {
	if (p) {
		p->~T();
		HashBucket::freeMemory(reinterpret_cast<void*>(p), sizeof(T));
	}
}


