#include "../include/MemPool.h"  

#include <cassert>

constexpr size_t CACHE_LINE = 64;
thread_local Slot* MemoryPool::threadFreeList_ = nullptr;

struct GrowthRule {
	int count;
	int step;
};

MemoryPool::MemoryPool(size_t BlockSize)
	:BlockSize_(BlockSize)
	, initialBlockSize_(BlockSize)
	, SlotSize_(0)
	, firstBlock_(nullptr)
	, lastSlot_(nullptr)
	, curSlot_(nullptr)
	, freeList_(nullptr)
{}

MemoryPool::~MemoryPool()
{
	BlockHeader* cur = firstBlock_;
	while (cur) {
		BlockHeader* next = cur->next;

		//等同于free(reinterpret_cast<void*>(firstBlock_))
		//转化成void指针，因为void类型不需要调用析构函数，只释放空间
		operator delete(reinterpret_cast<void*>(cur));
		cur = next;
	}
}

void MemoryPool::init(size_t size)
{
	assert(size > 0);
	SlotSize_ = size;
	BlockSize_ = initialBlockSize_;  //重置
	firstBlock_ = nullptr;
	curSlot_ = nullptr;
	freeList_ = nullptr;
	lastSlot_ = nullptr;
}

void* MemoryPool::allocate()
{
	if (threadFreeList_) {
		Slot* slot = threadFreeList_;
		threadFreeList_ = slot->next;
		return slot;
	}

	Slot* slot = popFreeList();
	if(slot)
		return slot;

	{
		std::lock_guard<std::mutex> lock(mutexForBlock_);
		if (curSlot_ >= lastSlot_)
		{
			allocateNewBlock();
		}

		slot = curSlot_;
		curSlot_++;
	}
	return slot;
}

void MemoryPool::deallocate(void* ptr)
{
	static const size_t MAX_THREAD_FREELIST_SIZE = 100; // 限制最大长度
	static thread_local size_t threadFreeListSize = 0;  // 每个线程维护自己的计数器

	Slot* slot = reinterpret_cast<Slot*>(ptr);

	// 将 Slot 放入 threadFreeList_
	slot->next = threadFreeList_;
	threadFreeList_ = slot;

	threadFreeListSize++;
	if (threadFreeListSize > MAX_THREAD_FREELIST_SIZE) {
		// 将多余的 Slot 归还到全局 freeList_
		pushFreeList(threadFreeList_);
		threadFreeList_ = nullptr;
		threadFreeListSize = 0;
	}
}

/*
BlockHeader* MemoryPool::findBlockHeader(Slot* slot)
{
	BlockHeader* cur = firstBlock_;
	while (cur) {
		char* blockStart = reinterpret_cast<char*>(cur);
		char* blockEnd = blockStart + BlockSize_;
		if (reinterpret_cast<char*>(slot) >= blockStart && reinterpret_cast<char*>(slot) < blockEnd) {
			return cur;
		}
		cur = cur->next;
	}
	return nullptr;
}

void MemoryPool::removeBlock(BlockHeader* header)
{
	if (firstBlock_ == header) {
		firstBlock_ = header->next;
		return;
	}
	BlockHeader* cur = firstBlock_;
	while (cur && cur->next != header) {
		cur = cur->next;
	}
	if (cur) {
		cur->next = header->next;
	}
}
*/

void MemoryPool::allocateNewBlock()
{
	// 根据 SlotSize_ 选择 block 大小
	size_t blockSize;
	if (SlotSize_ <= 64)
		setBlockSize(4096);     // 4K
	else if (SlotSize_ <= 192)
		setBlockSize(8092);		// 8K
	else
		setBlockSize(16384);    // 16K


	// 头插法插入新的内存块
	void* newBlock = operator new(BlockSize_);

	BlockHeader* header = reinterpret_cast<BlockHeader*>(newBlock);
	header->next = firstBlock_;
	header->usedSlots.store(0);
	firstBlock_ = header;

	char* body = reinterpret_cast<char*>(newBlock) + sizeof(Slot*);
	size_t paddingSize = padPointer(body, SlotSize_); // 计算对齐需要填充内存的大小
	curSlot_ = reinterpret_cast<Slot*>(body + paddingSize);

	// 超过该标记位置，则说明该内存块已无内存槽可用，需向系统申请新的内存块
	lastSlot_ = reinterpret_cast<Slot*>(reinterpret_cast<size_t>(newBlock) + BlockSize_ - SlotSize_ + 1);

	std::cout << "allocate new block, SlotSize: " << SlotSize_ 
		<< ", BlockSize: " << BlockSize_
		<< std::endl;
}

size_t MemoryPool::padPointer(char* p, size_t align)
{
	align = std::max(align, CACHE_LINE);
	size_t addr = reinterpret_cast<size_t>(p);
	size_t mis = addr % align;
	return (mis == 0) ? 0 : (align - mis);
}

// 实现无锁入队操作
bool MemoryPool::pushFreeList(Slot* slot)
{
	while (true)
	{
		Slot* oldHead = freeList_.load(std::memory_order_relaxed);
		slot->next.store(oldHead, std::memory_order_relaxed);
		if (freeList_.compare_exchange_weak(oldHead, slot,
				std::memory_order_release, std::memory_order_relaxed))
		{
			//std::cout << "Slot pushed from freeList_: " << slot << std::endl;
			return true;
		}
	}
}

// 实现无锁出队操作
Slot* MemoryPool::popFreeList()
{
	while (true)
	{
		Slot* oldHead = freeList_.load(std::memory_order_acquire);
		if (oldHead == nullptr) {
			//std::cout << "freeList_ is empty" << std::endl;
			return nullptr;
		}

		Slot* newHead = nullptr;
		try
		{
			newHead = oldHead->next.load(std::memory_order_relaxed);
		}
		catch (...)
		{
			// 如果返回失败，则continue重新尝试申请内存
			continue;
		}

		if (freeList_.compare_exchange_weak(oldHead, newHead,
			std::memory_order_acquire, std::memory_order_relaxed))
		{
			//std::cout << "Slot popped from freeList_: " << oldHead << std::endl;
			return oldHead;
		}
	}
}

//.................Size-Class 优化//.............
/*.................小对象细分，大对象粗分.........	
MemoryPool[0]   (0 + 1) * 8		-> 8
MemoryPool[1]   (1 + 1) * 8		-> 16
MemoryPool[2]	(2 + 1) * 8		-> 24
。。。。。。。。。。
MemoryPool[7]	(7 + 1) * 8		-> 64
-------------------------------------------------
MemoryPool[8]	8 * 8 + (8 - 7) * (2 * 8)	-> 80
MemoryPool[9]	8 * 8 + (9 - 7) * (2 * 8)	-> 96
MemoryPool[10]	8 * 8 + (10 - 7) * (2 * 8)	-> 112
。。。。。。。。。。
MemoryPool[15]	8 * 8 + (15 - 7) * (2 * 8)	-> 192
-------------------------------------------------
MemoryPool[16]	8 * 8 + 16 * 8 + (16 - 15) * (4 * 8)	-> 224
MemoryPool[17]	8 * 8 + 16 * 8 + (17 - 15) * (4 * 8)	-> 256
MemoryPool[18]	8 * 8 + 16 * 8 + (18 - 15) * (4 * 8)	-> 288
。。。。。。。。。。
MemoryPool[25]	8 * 8 + 16 * 8 + (31 - 15) * (4 * 8)	-> 512
*/

void HashBucket::initMemoryPool()
{
	//增长规则
	std::vector<GrowthRule> rules = {
		{7,  8},	// 0~7
		{8, 16},	// 8~15
		{11,32}		// 16~26
	};

	const size_t baseSize = SLOT_BASE_SIZE;   // 8
	size_t currentSize = baseSize;
	int index = 0;

	// 清空全局表
	if (!g_slotSizeInitialized)
	{
		for (int i = 0; i < MEMORY_POOL_NUM; i++)
			g_slotSizes[i] = 0;
	}

	for (auto& rule : rules)
	{
		for (int j = 0; j < rule.count && index < MEMORY_POOL_NUM; ++j, ++index)
		{
			// 保存到内存池	
			getMemoryPool(index).init(currentSize);

			// 保存到全局表（供 getIndexBySize() 的二分查找使用）
			g_slotSizes[index] = currentSize;

			std::cout << "[Init] index = " << index
				<< ", SlotSize = " << currentSize
				<< std::endl;

			currentSize += rule.step;
		}
	}

	g_slotSizeInitialized = true;

	if (index < MEMORY_POOL_NUM)
	{
		std::cout << "[Warning] MEMORY_POOL_NUM(" << MEMORY_POOL_NUM
			<< ") larger than generated pools (" << index << ")"
			<< std::endl;
	}
}

MemoryPool& HashBucket::getMemoryPool(int index)
{
	static MemoryPool memoryPools[MEMORY_POOL_NUM];
	return memoryPools[index];
}

int HashBucket::getIndexBySize(size_t size)
{
    if (!g_slotSizeInitialized)
        return -1;

    // 二分查找：找到第一个 >= size 的 SlotSize
    auto it = std::lower_bound(std::begin(g_slotSizes),
                               std::end(g_slotSizes),
                               size);

    if (it == std::end(g_slotSizes))
        return -1;

    return int(it - std::begin(g_slotSizes));
}



