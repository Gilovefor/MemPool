#include "../include/MemPool.h"  

#include <cassert>

constexpr size_t CACHE_LINE = 64;

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
	//std::cout << "allocate-1" << std::endl;
	//std::lock_guard<std::mutex> lock(mutexForBlock_);
	//std::cout << "allocate-2" << std::endl;

	// 优先使用空闲链表中的内存槽
	Slot* slot = popFreeList();
	if (slot != nullptr) {
		//BlockHeader* header = findBlockHeader(slot);
		//header->usedSlots.fetch_add(1, std::memory_order_relaxed);
		return slot;
	}

	Slot* temp;
	{
		/* ********************************************* */
		std::lock_guard<std::mutex> lock(mutexForBlock_);
		if (curSlot_ >= lastSlot_)
		{
			allocateNewBlock();
		}

		temp = curSlot_;
		// 这里不能直接 curSlot_ += SlotSize_ 因为curSlot_是Slot*类型，所以需要除以SlotSize_再加1
		//curSlot_ += SlotSize_ / sizeof(Slot);
		curSlot_++;
		//BlockHeader* header = findBlockHeader(temp);
		//header->usedSlots.fetch_add(1, std::memory_order_relaxed);
	}

	return temp;
}

void MemoryPool::deallocate(void* ptr)
{
	if (!ptr) {
		return;
	}
	//std::lock_guard<std::mutex> lock(mutexForBlock_);

	Slot* slot = reinterpret_cast<Slot*>(ptr);
	/*BlockHeader* header = findBlockHeader(slot);
	std::cout << "Deallocate: slot=" << slot << ", header=" << header << std::endl;

	size_t prev = header->usedSlots.fetch_sub(1, std::memory_order_relaxed);*/

	pushFreeList(slot);

	//如果递减后为0，回收整块
	//if (prev == 1) {
	//	std::cout << "Delete Block: " << header << std::endl;

	//	removeBlock(header);
	//	operator delete(reinterpret_cast<void*>(header));
	//}

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
	//std::cout << "Push Slot: " << slot << std::endl;

	while (true)
	{
		Slot* oldHead = freeList_.load(std::memory_order_relaxed);
		slot->next.store(oldHead, std::memory_order_relaxed);
		if (freeList_.compare_exchange_weak(oldHead, slot,
			std::memory_order_release, std::memory_order_relaxed))
		{
			return true;
		}
		// 失败：说明另一个线程可能已经修改了 freeList_
		// CAS 失败则重试
	}
}

// 实现无锁出队操作
Slot* MemoryPool::popFreeList()
{
	while (true)
	{
		Slot* oldHead = freeList_.load(std::memory_order_acquire);
		//std::cout << "Pop Slot: " << oldHead << std::endl;
		if (oldHead == nullptr)
			return nullptr; // 队列为空

		// 在访问 newHead 之前再次验证 oldHead 的有效性
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

		// 尝试更新头结点
		// 原子性地尝试将 freeList_ 从 oldHead 更新为 newHead
		if (freeList_.compare_exchange_weak(oldHead, newHead,
			std::memory_order_acquire, std::memory_order_relaxed))
		{
			return oldHead;
		}
		// 失败：说明另一个线程可能已经修改了 freeList_
		// CAS 失败则重试
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



