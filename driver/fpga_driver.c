/*
 * Version 1.0
 * History: @malte.vesper@gmx.net
 *   * Refactored to reduce LOC by approximately 30%
 *   * Ported to Linux Kernel >= 3.10
 */
//##############################Check Includes#################################################
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/major.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/irq.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/param.h>
#include <asm/uaccess.h>
#include "fpga_driver.h"

#include <asm/cacheflush.h>

#define DEBUG 1

#define CIRCULAR_QUEUES 21

/* =============================================================================
 * devm for proc_mkdir
 * ========================================================================== */
//TODO: submit kernel patch
struct devm_proc_entry {
	struct proc_dir_entry* proc_dir_entry;
};

static int devm_proc_match(struct device* dev, void* resource, void* match_data) {
	struct devm_proc_entry* devm_proc_entry = resource;
	return devm_proc_entry->proc_dir_entry == match_data;
}

static void _devm_proc_release(struct device* dev, void* resourceInfo) {
	struct devm_proc_entry* resource = resourceInfo;
	proc_remove(resource->proc_dir_entry);
}

struct proc_dir_entry* devm_proc_mkdir_data(struct device* dev, const char* name, umode_t mode, struct proc_dir_entry* parent, void* data) {
	struct devm_proc_entry* resourceInfo = devres_alloc(_devm_proc_release, sizeof(struct devm_proc_entry), GFP_KERNEL);

	if(!resourceInfo) {
		return 0;
	}

	resourceInfo->proc_dir_entry = proc_mkdir_data(name, mode, parent, data);

	devres_add(dev, resourceInfo);

	return resourceInfo->proc_dir_entry;
}

void devm_proc_remove(struct device* dev, struct proc_dir_entry* proc_dir_entry) {
	devres_release(dev, _devm_proc_release, devm_proc_match, proc_dir_entry);
}

/* =============================================================================
 * data structures
 * ========================================================================== */

enum boardStatus {
	BOARD_SLOT_UNUSED,
	BOARD_INITIALIZED
};

struct buffer {
	void*   memoryAddress;
	dma_addr_t dmaAddress;
};

struct dmaPage {
	struct sg_table sg_table;

	struct page** pages;
	uint32_t nrPages;

	atomic_t transferstatus;
	wait_queue_head_t waiters;
};

struct dmaOperation {
	size_t offset;
	atomic_t size;

	atomic_t pendingPackages;

	struct dmaPage* dmaPage;
	enum fpga_data_direction direction;

	struct list_head list;
	wait_queue_head_t waiters;
};

struct operationQueue {
	struct list_head operations;
	uint32_t   queueLength;

};

struct lockedOperationQueue {
	union {
		struct operationQueue;
		struct operationQueue operationQueue;
	};

	spinlock_t queueLock;
};

struct sg_memoryIterator {
	struct scatterlist* entry;
	size_t offset;
};

struct channel {
	struct buffer buffer[COHERENT_BUFFERS];

	//struct page** bufferPages;

	union {
		struct {
			atomic_t read;
			atomic_t write;
		} pending;

		atomic_t pendings[2]; //Goodbye grammar, hello consistency
	};

	union {
		struct {
			struct operationQueue read;
			struct operationQueue write;
		} dispatchQueue;

		struct operationQueue dispatchQueues[2];
	};

	union {
		struct {
			struct sg_memoryIterator read;
			struct sg_memoryIterator write;
		} dispatchIterator;

		struct sg_memoryIterator dispatchIterators[2];
	};

	union {
		struct {
			struct operationQueue read;
			struct operationQueue write;
		} postDispatchQueue;

		struct operationQueue postDispatchQueues[2];
	};

	union {
		struct {
			wait_queue_head_t read;
			wait_queue_head_t write;
		} waitQueue;

		wait_queue_head_t waitQueues[2];
	};

	union {
		struct {
			struct lockedOperationQueue read;
			struct lockedOperationQueue write;
		} preDispatchQueue;

		struct lockedOperationQueue preDispatchQueues[2];
	};


	atomic_t timeout;
};

struct fpga_board {
	void* const* ioTable;

	struct pci_dev* pcidev;

	struct channel channel[MAX_CHANNELS];

	struct proc_dir_entry* proc_dir;

	enum boardStatus status;
	char slot;
	//char name[];

	spinlock_t commandlock;
};

struct {
	struct fpga_board boards[MAX_BOARDS];
	struct semaphore semaphore;

	char activeBoards;

	int major;
	struct class* class;

	struct proc_dir_entry* proc_dir;
	struct scatterlist sentinelScatterlist;
} FPCI3;

/* =============================================================================
 * helper functions
 * ========================================================================== */

static int isOperationComplete(struct dmaOperation* operation) {
	return !atomic_read(&operation->size) && !atomic_read(&operation->pendingPackages);
}

/* =============================================================================
 * Scattergather memory iterator
 * ========================================================================== */

static void sg_memoryIteratorInit(struct sg_memoryIterator* iterator, struct scatterlist* scatterlist) {
	iterator->entry  = scatterlist;
	iterator->offset = 0;
}

static void sg_memoryIteratorSetNull(struct sg_memoryIterator* iterator) {
	sg_memoryIteratorInit(iterator, &FPCI3.sentinelScatterlist);
}

static size_t sg_MIAdvanceBytes(struct sg_memoryIterator* iterator, size_t bytes) {
	size_t bytesAdvanced;

	if(!iterator->entry) {
		return 0;
	}

	bytesAdvanced = sg_dma_len(iterator->entry) - iterator->offset;

	//printk("bAD %p - %p", iterator->entry, &FPCI3.sentinelScatterlist);

	if(bytesAdvanced > bytes) {
		bytesAdvanced = bytes;
		iterator->offset += bytesAdvanced;
	} else {
		iterator->offset = 0;
		iterator->entry = sg_next(iterator->entry);
	}

	printk("wekk FUCK\n");
	return bytesAdvanced;
}

static dma_addr_t sg_MIdmaAddress(struct sg_memoryIterator* iterator) {
	if(!iterator->entry) {
		return 0;
	}

	return sg_dma_address(iterator->entry)+iterator->offset;
}
/* =============================================================================
 * register accessors
 * ========================================================================== */

static unsigned int read_register(struct boardHandle board, const size_t registerNo) {
	return readl(FPCI3.boards[board.id].ioTable[0] + registerNo);
	//TODO: readq for 64 bit register
}

static void write_register(struct boardHandle board, const size_t registerNo, const unsigned long data) {
	writel(data, FPCI3.boards[board.id].ioTable[0] + registerNo);
}

/* =============================================================================
 * register mappers
 * ========================================================================== */

//map DMA_PNT to the defined register
const unsigned int DMA_POINT_SYS_MAPPER[] = {
	0, //dummy for padding
	PC_USER1_DMA_SYS,
	PC_USER2_DMA_SYS,
	PC_USER3_DMA_SYS,
	PC_USER4_DMA_SYS
};

//map DMA_PNT to the defined register
const unsigned int DMA_POINT_LEN_MAPPER[] = {
	0, //dummy for padding
	PC_USER1_DMA_LEN,
	PC_USER2_DMA_LEN,
	PC_USER3_DMA_LEN,
	PC_USER4_DMA_LEN
};

const unsigned int DMA_POINT_BITPOS_SEND_MAPPER[] = {
	0, //dummy for padding
	BITPOS_SEND_USER1_DATA,
	BITPOS_SEND_USER2_DATA,
	BITPOS_SEND_USER3_DATA,
	BITPOS_SEND_USER4_DATA
};

//map DMA_PNT to the defined register
const unsigned int DMA_POINT_SYS_RECV_MAPPER[] = {
	0, //dummy for padding
	USER1_PC_DMA_SYS,
	USER2_PC_DMA_SYS,
	USER3_PC_DMA_SYS,
	USER4_PC_DMA_SYS
};

//map DMA_PNT to the defined register
const unsigned int DMA_POINT_LEN_RECV_MAPPER[] = {
	0, //dummy for padding
	USER1_PC_DMA_LEN,
	USER2_PC_DMA_LEN,
	USER3_PC_DMA_LEN,
	USER4_PC_DMA_LEN
};


const unsigned int DMA_POINT_BITPOS_RECIVE_MAPPER[] = {
	0, //dummy for padding
	BITPOS_RECV_USER1_DATA,
	BITPOS_RECV_USER2_DATA,
	BITPOS_RECV_USER3_DATA,
	BITPOS_RECV_USER4_DATA
};

const unsigned int DMA_POINT_INTERRUPT_MAPPER[] = {
	0, //dummy for padding
	hostuser1,
	hostuser2,
	hostuser3,
	hostuser4
};

const unsigned int DMA_POINT_INTERRUPT_RECV_MAPPER[] = {
	0, //dummy for padding
	user1host,
	user2host,
	user3host,
	user4host
};

//fpgaDev->cfgMem

static void dispatch_send(struct boardHandle board, const dma_addr_t address, const unsigned int size, const int channel) {
	write_register(board, DMA_POINT_SYS_MAPPER[channel], address);
	write_register(board, DMA_POINT_LEN_MAPPER[channel], size);
	write_register(board, CTRL_REG,IRSTATUSMASK(DMA_POINT_BITPOS_SEND_MAPPER[channel])|0x00000001);
}

static void dispatch_receive(struct boardHandle board, const dma_addr_t address, const unsigned int size, const int channel) {
	write_register(board, DMA_POINT_SYS_RECV_MAPPER[channel], address);
	write_register(board, DMA_POINT_LEN_RECV_MAPPER[channel], size);
	write_register(board, CTRL_REG,IRSTATUSMASK(DMA_POINT_BITPOS_RECIVE_MAPPER[channel])|0x00000001);
}

static ssize_t waitInterrupt(struct boardHandle board, const int channel, const int direction) {
	long timeout;

	DEFINE_WAIT(wait);

	// Read timeout & convert to jiffies.
	timeout = (long) atomic_read(&FPCI3.boards[board.id].channel[channel].timeout);
	timeout = (timeout == 0 ? MAX_SCHEDULE_TIMEOUT : timeout * HZ / 1000);

	printk("[FPGA] Waiting on buffer %d\n", channel);

	if(direction) {
		wait_event_interruptible_timeout(
			FPCI3.boards[board.id].channel[channel].waitQueue.write,
			atomic_add_unless(
				&FPCI3.boards[board.id].channel[channel].pending.write,
				-1,
				0
			),
			timeout
		);
	} else {
		wait_event_interruptible_timeout(
			FPCI3.boards[board.id].channel[channel].waitQueue.read,
			atomic_add_unless(
				&FPCI3.boards[board.id].channel[channel].pending.read,
				-1,
				0
			),
			timeout
		);
	}
	//wait_event_interruptible_timeout(irqfile->readwait, !pop_circ_queue(queue, &msg, &info), timeout);
	return 0;
}

static struct dmaPage* dmaPageForBuffer(struct boardHandle board, int channel, int buffer);
static int dma_send(struct boardHandle board, struct packetInfo* packetInfo);
static int dma_receive(struct boardHandle board, struct packetInfo* packetInfo);
static void waitPage(struct dmaPage* page);

static void send(struct boardHandle board, const void* data, const size_t size, const int channel) {
	unsigned int len;
	unsigned int amt;
	unsigned int buf;
	unsigned int pre_buf;
	int sent = 0;
	const unsigned char * senddata=data;

	struct buffer* buffer = FPCI3.boards[board.id].channel[channel].buffer;

	struct packetInfo packetInfo[2];

	packetInfo[0].pageHandle = dmaPageForBuffer(board, channel, 0);
	packetInfo[1].pageHandle = dmaPageForBuffer(board, channel, 1);

	packetInfo[0].channel = channel;
	packetInfo[1].channel = channel;

	printk("Channel: %d\n", channel);

	buf = 0;
	pre_buf = 1;
	len = size;
	amt = len < BUF_SIZE ? len : BUF_SIZE;

	copy_from_user(buffer[buf].memoryAddress, senddata+sent, amt);
	//dispatch_send(board, buffer[buf].dmaAddress, amt, channel);
	packetInfo[buf].size = amt;
	dma_send(board, &packetInfo[buf]);

	sent += amt;

	if (sent < len) {
		amt = (len-sent < BUF_SIZE ? len-sent : BUF_SIZE);
		copy_from_user(buffer[pre_buf].memoryAddress, senddata+sent, amt);
	}


	//waitInterrupt(board, channel, 1);
	waitPage(packetInfo[buf].pageHandle);

	//printf("Sent is %d\n",sent);
	while (sent < len) {
			//dispatch_send(board, buffer[pre_buf].dmaAddress, amt, channel);
			packetInfo[pre_buf].size = amt;
			dma_send(board, &packetInfo[pre_buf]);
			//printf("Buffer address is %0x\n",rtn);
			sent += amt;
			swap(buf, pre_buf);
			if (sent < len){
				amt = (len-sent < BUF_SIZE ? len-sent : BUF_SIZE);
				copy_from_user(buffer[pre_buf].memoryAddress, senddata+sent, amt);
			}
			//waitInterrupt(board, channel, 1);
			waitPage(packetInfo[buf].pageHandle);
	}
}

/* Receiving data functions. */
static int receive(struct boardHandle board, void* data, size_t size, int channel) {
//void *fpga_recv_data(void *recv_parameters) {
	unsigned int amt;
	unsigned int buf=0;
	unsigned int pre_buf=1;
	int pre_amt = 0;

	struct buffer* buffer = FPCI3.boards[board.id].channel[channel].buffer;

	struct packetInfo packetInfo[2];

	packetInfo[0].pageHandle = dmaPageForBuffer(board, channel, 0);
	packetInfo[1].pageHandle = dmaPageForBuffer(board, channel, 1);

	packetInfo[0].channel = channel;
	packetInfo[1].channel = channel;

	amt = size < BUF_SIZE ? size : BUF_SIZE;

	//dispatch_receive(board, buffer[buf].dmaAddress, amt, channel);
	packetInfo[buf].size = amt;
	dma_receive(board, &packetInfo[buf]);

	size -= amt;
	pre_amt = amt;

	while(size){
		//waitInterrupt(board, channel, 0);
		waitPage(packetInfo[buf].pageHandle);

		amt = (size < BUF_SIZE ? size : BUF_SIZE);
		//dispatch_receive(board, buffer[pre_buf].dmaAddress, amt, channel);
		packetInfo[pre_buf].size = amt;
		dma_receive(board, &packetInfo[pre_buf]);
		size -= amt;

		copy_to_user(data, buffer[buf].memoryAddress, pre_amt);
		data += amt;

		pre_amt = amt;
		swap(buf, pre_buf);
	}

	//waitInterrupt(board, channel, 0);
	waitPage(packetInfo[buf].pageHandle);
	copy_to_user(data, buffer[buf].memoryAddress, pre_amt);

	return 0;
}

///////////////////////////////////////////////////////
// MEMORY ALLOCATION & HELPER FUNCTIONS
///////////////////////////////////////////////////////

/** 
 * Reads the interrupt vector from the FPGA.
 */
static inline unsigned int read_status(struct boardHandle board) {
	return read_register(board, STA_REG);
}

/** 
 * Clears the interrupt register in the FPGA.
 */
static inline void clear_interrupt_vector(struct boardHandle board, unsigned int vect) {
	write_register(board, STA_REG, vect);
}

/* =============================================================================
 * Pin Pages
 * ========================================================================== */

struct dmaPageOLDOLDOLD {
	struct scatterlist* scatterlist;
	uint32_t scatterlistLength;

	struct page** page;
	uint32_t nrPages;
};

static void unpinPages(struct dmaPage* const pageInfo) {
	for(int i=0; i<pageInfo->nrPages; ++i) {
		put_page(pageInfo->pages[i]);
	}
}

static void unpinArea(struct dmaPage* const dmaPage) {
	//TODO: error on page is still mapped. Use a counter to count mappings before we unpin?
	/*if(pageQuery_mappedAny(dmaPage)) {
		dma_unmap_page()
	}*/

	sg_free_table(&dmaPage->sg_table);

	unpinPages(dmaPage);

	kfree(dmaPage->pages);
	kfree(dmaPage);
}
/*
static inline void releaseAreaById(uintptr_t pageId) {
	if(mayManipulatePage(pageId)) {
		releaseArea(getPageFromId(pageId));
	}
}

*/

//return 1 if constraining was successful
static int pageConstrainMemoryInfo(struct memoryInfo* userMemory) {
	if(userMemory->size < PAGE_SIZE) { //this checks guarantess, that the adjusting of pagestart below, leaves a non negative size (ohterwise we would need another check against underflow there).
		printk("Pinned area is smaller than PAGE_SIZE %lu\n", PAGE_SIZE);
		return 0;
	}

	if(((uintptr_t)userMemory->start)%PAGE_SIZE) {
		uintptr_t newStart = (((uintptr_t)userMemory->start >> PAGE_SHIFT) + 1) << PAGE_SHIFT;

		printk(pr_fmt("Area start is not page aligned (Startingaddress: %p). Attempting to page align.\n"), userMemory->start);

		userMemory->size -= newStart - (uintptr_t)userMemory->start;
		userMemory->start = (void*)newStart;
	}

	if( userMemory->size & ~PAGE_MASK) {
		printk(pr_fmt("Area size is not a multiple of page size (Size: %zu). Truncating to multiple of pagesize.\n"), userMemory->size);
		userMemory->size &= PAGE_MASK;
	}

	if(!userMemory->size) {
		printk(pr_fmt("Area size is zero.\n"));
		return 0;
	}

	return 1;
}

static struct dmaPage* pinArea(struct memoryInfo* userMemory) {
	struct dmaPage* dmaPage;

	if(!pageConstrainMemoryInfo(userMemory)) {
		return 0;
	}

	dmaPage = kmalloc(sizeof(struct dmaPage), GFP_KERNEL);  //these are not board (device) bound... so no devm_*

	if(!dmaPage) {
		printk("Could not allocate dmaPage struct\n");
		return NULL;
	}

	atomic_set(&dmaPage->transferstatus, 0);
	dmaPage->nrPages = userMemory->size >> PAGE_SHIFT;
	dmaPage->pages = kmalloc(dmaPage->nrPages*sizeof(struct page*), GFP_KERNEL);

	if(!dmaPage->pages) {
		printk("Could not allocate dmaPage struct's page array\n");
		kfree(dmaPage);
		return NULL;
	}

	dmaPage->nrPages = get_user_pages_fast((unsigned long)userMemory->start, dmaPage->nrPages, 1, dmaPage->pages);

	if(dmaPage->nrPages <= 0) {
		printk("Failed to pin pages\n");
		kfree(dmaPage->pages);
		kfree(dmaPage);
		return NULL;
	}

	if(sg_alloc_table_from_pages(&dmaPage->sg_table, dmaPage->pages, dmaPage->nrPages, 0, userMemory->size, GFP_KERNEL)) {
		printk("Failed to generate scatter gather list\n");

		unpinPages(dmaPage);

		kfree(dmaPage->pages);
		kfree(dmaPage);
	}

	return dmaPage;
}

/* =============================================================================
 * Manage CoherentMapping lists
 * ========================================================================== */
static int getPageCount(size_t size) {
	return size >> PAGE_SHIFT;
}

/*static int getPages(void* virtual_address, size_t size, struct page** pageArray) {
	int pages=getPageCount(size);

	for(int i=0; i<getPageCount(size); ++i) {
		virtual_address += PAGE_SIZE;
		pageArray[i] = virt_to_page(virtual_address); //FIX:off by one? BUT IT DOES NOT CRASH THIS WAY, so...
		//in theory we could just do:
		//pageArray[i+1] = pageArray[i]+1; //pages are PHYSICALLY consistent as a precondition for this function.
	}

	return pages;
}

static int setBufferPageArray(int slot, int channel) {

	int pages = 0;
	struct channel* channelData = &FPCI3.boards[slot].channel[channel];
	channelData->bufferPages = (struct page**)devm_kmalloc(&FPCI3.boards[slot].pcidev->dev, sizeof(struct page*)*COHERENT_BUFFERS*getPageCount(BUF_SIZE), GFP_KERNEL);

	for(int i=0; i<COHERENT_BUFFERS; ++i) {
		pages += getPages(channelData->buffer[i].memoryAddress, BUF_SIZE, &channelData->bufferPages[pages]);
	}

	return pages;
}

static void* mapChannel(void) {
	struct mm_struct* mm = current->mm;

	uintptr_t address;
	const unsigned long size = COHERENT_BUFFERS*BUF_SIZE;
	vm_flags_t flags = VM_DONTEXPAND | VM_IO | VM_SOFTDIRTY | VM_READ | VM_WRITE | VM_LOCKED;
	struct vm_area_struct* vma;

	down_write(&mm->mmap_sem);
	address = get_unmapped_area(NULL, 0, size, 0, flags);
	//install_special_mapping(mm, address, size, flags, FPCI3.boards[0].channel[0].bufferPages);

	vma = devm_kmalloc(&FPCI3.boards[0].pcidev->dev, sizeof(struct vm_area_struct), GFP_KERNEL);//kmem_cache_zalloc(vm_area_cachep, GFP_KERNEL);
	if (!vma) {
		up_write(&mm->mmap_sem);
		return 0;
	}

	vma->vm_mm = mm;
	vma->vm_start = address;
	vma->vm_end = address + size;
	vma->vm_flags = flags;
	vma->vm_page_prot = vm_get_page_prot(flags);
	vma->vm_pgoff = 0;
	INIT_LIST_HEAD(&vma->anon_vma_chain);

	dma_mmap_attrs(&FPCI3.boards[0].pcidev->dev, vma, FPCI3.boards[0].channel[0].buffer[0].memoryAddress, FPCI3.boards[0].channel[0].buffer[0].dmaAddress, BUF_SIZE, 0);


	up_write(&mm->mmap_sem);

	return (void*)address;
}*/

/* =============================================================================
 * Manage ScatterGather lists
 * ========================================================================== */
/*static void generateSGforDmaPage(struct dmaPageOLDOLDOLD* const page) {
	for(int i=0; i<page->nrPages; ++i) {
		sg_set_page(&page->scatterlist[i], page->page[i], PAGE_SIZE, 0);
	}
}*/


/* =============================================================================
 * Manage mappings
 * ========================================================================== */
static void dma_unmapPage(struct pci_dev* const pcidev, struct dmaPage* const dmaPage, const enum dma_data_direction direction) {
	//TODO: dma_unmap_sg_attrs / dma_map_sg_attrs better? to mark it as noncoherent?
	//if(pageQuery_mappedAny(page)) {
		pci_unmap_sg(pcidev, dmaPage->sg_table.sgl, dmaPage->sg_table.orig_nents, direction);
		//pageClear_mapped(page, direction);
	//}
}

static void dma_mapPage(struct pci_dev* const pcidev, struct dmaPage* const dmaPage, const enum dma_data_direction direction) {
	//int minor = ((struct scas_driverInfo*)pci_get_drvdata(pcidev))->slot;

	/*if(page->mappedFor.direction != DMA_NONE) {
		if(minor == page->mappedFor.minor && (page->mappedFor.direction == direction || page->mappedFor.direction == DMA_BIDIRECTIONAL)) {
			return;
		} else {
			//TODO: support multiple mappings
			dev_dbg(&pcidev->dev, "Remapping DMA page, use dma_unmapPage first.");
			dma_unmapPage(pcidev, page, DMA_BIDIRECTIONAL);
		}
	}*/

	//generateSGforDmaPage(page);

	dmaPage->sg_table.nents = pci_map_sg(pcidev, dmaPage->sg_table.sgl, dmaPage->sg_table.orig_nents, direction);

	//pageSet_mapped(page, direction);
	//page->mappedFor.minor     = minor;
}

static inline void dma_syncForDev(struct pci_dev* const pcidev, struct dmaPage* const dmaPage) {
	pci_dma_sync_sg_for_device(pcidev, dmaPage->sg_table.sgl, dmaPage->sg_table.orig_nents, DMA_BIDIRECTIONAL);
}

static inline void dma_syncForCpu(struct pci_dev* const pcidev, struct dmaPage* const dmaPage) {
	pci_dma_sync_sg_for_cpu(pcidev, dmaPage->sg_table.sgl, dmaPage->sg_table.orig_nents, DMA_BIDIRECTIONAL);
}

/*static inline void dma_send(struct dmaPageOLDOLDOLD* page) {
	struct boardHandle board;
	const dma_addr_t address = sg_dma_address(&page->scatterlist[0]);
	printk("len is %d\n", sg_dma_len(&page->scatterlist[0]));
	for(int i=0; i<1024*1024*1024/BUF_SIZE; ++i) {
		dispatch_send(board, address, BUF_SIZE, 1);
		//dispatch(gDMAHWAddr[2], BUF_SIZE, 1);
		waitInterrupt(board, 1, 1);
	}
	/ *dispatch(sg_dma_address(&page->scatterlist[0]),0,1);
	for(int i=0; i<page->nrPages; ++i) {
		int offset = 0;
		while(offset < sg_dma_len(&page->scatterlist[i])) {
			int chunksize = min((int)sg_dma_len(&page->scatterlist[i])-offset, BUF_SIZE);
			//for(int j=0; j < 8 ; j++) {
			waitInterrupt(2);
			dispatch(sg_dma_address(&page->scatterlist[i])+offset,chunksize,1);
			//}
			offset += chunksize;
		//__dma_dispatch_send_chunk(pcidev, sg_dma_address(&dmaPage->mappedFor.scatterlist[page->scatterlistIndex]) + page->offset, chunksize, dmaPage->operation.channel);
			//printk("waiting\n");
			//waitInterrupt(2);
		}
	}
	waitInterrupt(2);/
}*/

/* =============================================================================
 * DMA Send/Receive
 * ========================================================================== */
static void waitPage(struct dmaPage* page) {
	//long timeout = (long) atomic_read(&FPCI3.boards[board.id].channel[channel].timeout);
	//timeout = (timeout == 0 ? MAX_SCHEDULE_TIMEOUT : timeout * HZ / 1000);

	//printk("[FPGA] Waiting on a page %p\n", page);

	if(page) {
		wait_event_interruptible(
			page->waiters,
			atomic_read(&page->transferstatus) == 2
		);
	}

	//printk("[FPGA]Awoken\n");
}

static int dma_dispatch_chunk(struct boardHandle board, int channelNo, struct dmaOperation* operation) {
	int operationSize = atomic_read(&operation->size);
	int opPending = 0;
	struct channel* channel = &FPCI3.boards[board.id].channel[channelNo];
	int direction = operation->direction;
	int pending = atomic_read(&channel->pendings[direction]);

	while(pending < DISPATCH_QUEUE_LENGTH) {
		dma_addr_t address = sg_MIdmaAddress(&channel->dispatchIterators[direction]);
		size_t chunksize = sg_MIAdvanceBytes(&channel->dispatchIterators[direction], min(operationSize, MAX_TRANSFER_SIZE));

		printk("CHUNKSIZE:%d\n", chunksize);

		if(!chunksize) {
			/*if(!list_empty(&channel->dispatchQueues[direction].operations)) {
				struct dmaPage* page = list_first_entry(&channel->dispatchQueues[direction].operations, struct dmaOperation, list)->dmaPage;
				sg_memoryIteratorInit(&channel->dispatchIterators[direction], page->sg_table.sgl);
			} else {
				sg_memoryIteratorSetNull(&channel->dispatchIterators[direction]);
			}*/
			printk("We are done here\n");
			break;
		}

		if(direction == 0) {
			printk("FLY YOU %d FOOLs\n", chunksize);
			dispatch_send(board, address, chunksize, channelNo);
		} else {
			//printk("FLY YOU %d FOOLs\n", chunksize);
			dispatch_receive(board, address, chunksize, channelNo);
		}

		pending++;
		opPending++;
		operationSize -= chunksize;
	};

	atomic_add(opPending, &operation->pendingPackages);
	atomic_set(&operation->size, operationSize);
	atomic_set(&channel->pendings[direction], pending);
	//printk("Pending of %p increased by %d to %d\n", operation->dmaPage, opPending, atomic_read(&operation->pendingPackages));

	return pending;
}

static void dma_dispatch(struct boardHandle board, int channelNo, int direction) {
	struct channel* channel = &FPCI3.boards[board.id].channel[channelNo];
	unsigned long flags;
	struct dmaOperation* operation;

	spin_lock_irqsave(&FPCI3.boards[board.id].commandlock, flags); {

		int pending = atomic_read(&channel->pendings[direction]);

		//printk("PENDING %d\n", pending);

		if(pending < DISPATCH_QUEUE_LENGTH) {
			spin_lock(&channel->preDispatchQueues[direction].queueLock); {
				if(!list_empty(&channel->preDispatchQueues[direction].operationQueue.operations)) {
					list_splice_tail_init(
						&channel->preDispatchQueues[direction].operationQueue.operations,
						&channel->dispatchQueues[direction].operations
					);

					channel->dispatchQueues[direction].queueLength += channel->preDispatchQueues[direction].operationQueue.queueLength;
					channel->preDispatchQueues[direction].operationQueue.queueLength = 0;

					if(channel->dispatchIterators[direction].entry == &FPCI3.sentinelScatterlist) {
						sg_memoryIteratorInit(&channel->dispatchIterators[direction], list_first_entry(&channel->dispatchQueues[direction].operations, struct dmaOperation, list)->dmaPage->sg_table.sgl);
					}
				}
			} spin_unlock(&channel->preDispatchQueues[direction].queueLock);



			if(!list_empty(&channel->postDispatchQueues[direction].operations)) {
				operation = list_last_entry(&channel->postDispatchQueues[direction].operations, struct dmaOperation, list);
				if(atomic_read(&operation->size)) {
					//printk("WE HAVE SOMETHING LEFT\n");
					pending = dma_dispatch_chunk(board, channelNo, operation);
				}
			}

			//printk("PENDING AFTER IF %d\n", pending);

			while(pending < DISPATCH_QUEUE_LENGTH && !list_empty(&channel->dispatchQueues[direction].operations)) {
				operation = list_first_entry(&channel->dispatchQueues[direction].operations, struct dmaOperation, list);
				sg_memoryIteratorInit(&channel->dispatchIterators[direction], operation->dmaPage->sg_table.sgl);
				//printk("LOOPEDIELOOP\n");
				list_move_tail(channel->dispatchQueues[direction].operations.next, &channel->postDispatchQueues[direction].operations);
				channel->dispatchQueues[direction].queueLength--;
				channel->postDispatchQueues[direction].queueLength++;

				pending = dma_dispatch_chunk(board, channelNo, operation);
			}
		}
	} spin_unlock_irqrestore(&FPCI3.boards[board.id].commandlock, flags);
}

static int dma_appendOperation(struct boardHandle board, struct dmaOperation* operation, int channelNo) {
	int direction = operation->direction;
	struct channel* channel = &FPCI3.boards[board.id].channel[channelNo];

	spin_lock(&channel->preDispatchQueues[direction].queueLock); {

		list_add_tail(&operation->list, &channel->preDispatchQueues[direction].operationQueue.operations);
		channel->preDispatchQueues[direction].operationQueue.queueLength++;

		//fast path, we have the lock so why not weave in a quick dispatch? CAREFULL DANGER OF DEADLOCK

	} spin_unlock(&channel->preDispatchQueues[direction].queueLock);

	return 0;
}

static int dma_send(struct boardHandle board, struct packetInfo* packetInfo) {
	struct dmaOperation* operation = kmalloc(sizeof(struct dmaOperation), GFP_KERNEL); //todo: use a memory pool

	if(!operation) {
		return ENOMEM;
	}

	operation->direction       = HOST_TO_FPGA;
	operation->dmaPage         = (struct dmaPage*)packetInfo->pageHandle;
	operation->offset          = 0; //TODO: enable non zero offsets

	atomic_set(&operation->dmaPage->transferstatus, 1);

	atomic_set(&operation->size, packetInfo->size);
	atomic_set(&operation->pendingPackages, 0);

	INIT_LIST_HEAD(&operation->list);
	init_waitqueue_head(&operation->waiters); //TODO externals can not attach waiters, operation dies, so no way to attach multiple waiters
	init_waitqueue_head(&operation->dmaPage->waiters); //TODO: good that we reiinit this ever so often?

	dma_appendOperation(board, operation, packetInfo->channel);
	dma_dispatch(board, packetInfo->channel, HOST_TO_FPGA);

	return 0;
}

static int dma_receive(struct boardHandle board, struct packetInfo* packetInfo) {
	struct dmaOperation* operation = kmalloc(sizeof(struct dmaOperation), GFP_KERNEL); //todo: use a memory pool

	if(!operation) {
		return ENOMEM;
	}

	operation->direction       = FPGA_TO_HOST;
	operation->dmaPage         = (struct dmaPage*)packetInfo->pageHandle;
	operation->offset          = 0; //TODO: enable non zero offsets

	atomic_set(&operation->dmaPage->transferstatus, 1);

	atomic_set(&operation->size, packetInfo->size);
	atomic_set(&operation->pendingPackages, 0);

	INIT_LIST_HEAD(&operation->list);
	init_waitqueue_head(&operation->waiters); //TODO externals can not attach waiters, operation dies, so no way to attach multiple waiters
	init_waitqueue_head(&operation->dmaPage->waiters); //TODO: good that we reiinit this ever so often?

	dma_appendOperation(board, operation, packetInfo->channel);
	dma_dispatch(board, packetInfo->channel, FPGA_TO_HOST);

	return 0;
}

static struct dmaPage* dmaPageForBuffer(struct boardHandle board, int channel, int buffer) {
	struct dmaPage* page;

	page = kmalloc(sizeof(struct dmaPage), GFP_KERNEL);

	if(!page) {
		printk("THAT SUCKS\n");
		return 0;
	}

	atomic_set(&page->transferstatus, 0);

	page->nrPages = getPageCount(BUF_SIZE);

	page->pages = kmalloc(page->nrPages*sizeof(struct page*), GFP_KERNEL);

	if(!page->pages) {
		printk("THAT SUCKS more\n");
		kfree(page);
		return 0;
	}

	for(int i=0; i<page->nrPages; ++i) {
		page->pages[i] = virt_to_page(FPCI3.boards[board.id].channel[channel].buffer[buffer].memoryAddress + i*PAGE_SIZE);
	}

	sg_alloc_table_from_pages(&page->sg_table, page->pages, page->nrPages, 0, BUF_SIZE, GFP_KERNEL);

	page->sg_table.nents = 1;
	page->sg_table.sgl->dma_address = FPCI3.boards[board.id].channel[channel].buffer[buffer].dmaAddress;

	#ifdef CONFIG_NEED_SG_DMA_LENGTH
		page->sg_table.sgl->dma_length = BUF_SIZE;
	#else
		page->sg_table.sgl->length = BUF_SIZE;
	#endif

	return page;
}
///////////////////////////////////////////////////////
// INTERRUPT HANDLER
///////////////////////////////////////////////////////


/**
 * Interrupt handler for all interrupts on all files. Reads data/values
 * from FPGA and wakes up waiting threads to process the data.
 */
static irqreturn_t interrupt_handler(int irq, void *dev_id) {
	static const int PERMUTATOR[] = {0, 0, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 3, 3, 0, 0, 4, 4, 0, 0, 0};
	unsigned int info;
	int slot = (uintptr_t)pci_get_drvdata(dev_id);
	struct boardHandle board = { .id = slot };
#ifdef DEBUG
	//printk("Some interrupt \n");
#endif
	info = read_status(board);   //Read the interrupt status register from FPGA

//#ifdef DEBUG
	printk("Status register value %0x\n", info);
//#endif

	for(int i=0; i<CIRCULAR_QUEUES; ++i) {
		if(info & IRSTATUSMASK(i)) {
#ifdef DEBUG
			//printk(DEBUG_STRINGS[i]);
#endif

			printk("FOUND ON %d[%d]\n", PERMUTATOR[i], i);

			//if (push_circ_queue(&(irqfile->buffers[i]), EVENT_DATA_SENT, info))
			//	printk(KERN_ERR "intrpt_handler, msg queue full for irq %d\n",						irqfile->channel);
			//printk("[FPGA] pushed to buffer %d[%d]\n", PERMUTATOR[i], slot);

			//todo wake_up_nr

			/*if(i%2==0) {
				atomic_inc(&FPCI3.boards[slot].channel[PERMUTATOR[i]].pending.write);
				wake_up(&FPCI3.boards[slot].channel[PERMUTATOR[i]].waitQueue.write);
			} else {
				printk("READINTERRUPT\n");
				atomic_inc(&FPCI3.boards[slot].channel[PERMUTATOR[i]].pending.read);
				wake_up(&FPCI3.boards[slot].channel[PERMUTATOR[i]].waitQueue.read);
			}*/

			//decrement op count
			//if opcount zero, finish page (wake waiters remove from queue)
			{
				struct dmaOperation* operation = list_first_entry(&FPCI3.boards[slot].channel[PERMUTATOR[i]].postDispatchQueues[i%2].operations, struct dmaOperation, list);

				//printk("VAL %d\n", atomic_read(&FPCI3.boards[slot].channel[PERMUTATOR[i]].pendings[i%2]));

				atomic_dec(&FPCI3.boards[slot].channel[PERMUTATOR[i]].pendings[i%2]);
				//printk("INT DISPTACH\n");
				dma_dispatch(board, PERMUTATOR[i], i%2);

				if(!atomic_dec_return(&operation->pendingPackages) && !atomic_read(&operation->size)) {
					list_del_init(operation->list.next);
					//TODO cpu syn for receive, remember to do send on dispatch
					//printk("WAKE THE FUCKERS\n");
					atomic_set(&operation->dmaPage->transferstatus, 2);
					wake_up_interruptible_sync(&operation->dmaPage->waiters);
				}

				printk("Well the op %p has a little bit pending %d\n", operation->dmaPage, atomic_read(&operation->pendingPackages));
			}
		}
	}

	clear_interrupt_vector(board, info);

	return IRQ_HANDLED;
}


//#########################################################################################
/**
 * Called to set the timeout value, allocate a buffer, and release
 * a buffer. Return value depends on ioctlnum and expected behavior.
 */
static long ioctl(struct file *filehandle, unsigned int ioctlnum, unsigned long ioctlparam) {

	struct boardHandle board = {
		.id = (uintptr_t)PDE_DATA(filehandle->f_inode)
	};

	switch (ioctlnum) {
		case IOCTL_REGISTER_WRITE: {
			struct registerInfo registerInfo;

			if(!copy_from_user(&registerInfo, (void*)ioctlparam, sizeof(struct registerInfo))) {
				write_register(board, registerInfo.registerNo, registerInfo.value);
			} else {
				return -EFAULT;
			}
		} break;

		case IOCTL_REGISTER_READ: {
			struct registerInfo registerInfo;

			if(!copy_from_user(&registerInfo, (void*)ioctlparam, sizeof(struct registerInfo))) {
				return read_register(board, registerInfo.registerNo);
			} else {
				return -EFAULT;
			}
		} break;



		case IOCTL_BOUNCE_SEND: {
			struct packetInfo packetInfo;

			//dev_dbg(&pcidev->dev, pr_fmt("IOCTL: IOCTL_FPGA_PIN_PAGE\n"));

			if(!copy_from_user(&packetInfo, (void*)ioctlparam, sizeof(struct packetInfo))) {
				send(board, packetInfo.start, packetInfo.size, packetInfo.channel);
			} else {
				return -EFAULT;
			}
		} break;

		case IOCTL_BOUNCE_RECEIVE: {
			struct packetInfo packetInfo;
			//int boardId = (uintptr_t)PDE_DATA(file->f_inode);
			//dev_dbg(&pcidev->dev, pr_fmt("IOCTL: IOCTL_FPGA_PIN_PAGE\n"));

			if(!copy_from_user(&packetInfo, (void*)ioctlparam, sizeof(struct packetInfo))) {
				receive(board, packetInfo.start, packetInfo.size, packetInfo.channel);
			} else {
				return -EFAULT;
			}
		} break;

		//TODO: we have to validate that the pointers are legal. A) hashtable with ok pointers B) have an array of pages hand check that the index is in range, also check that the element is initialized
		case IOCTL_DMA_PIN: {
			struct memoryInfo memoryInfo;

			if(!copy_from_user(&memoryInfo, (void*)ioctlparam, sizeof(struct memoryInfo))) {
				struct dmaPage* dmaPage = pinArea(&memoryInfo);
				__put_user(dmaPage, (uintptr_t*)&((struct memoryInfo*)ioctlparam)->pageHandle);
			}
		} break;

		case IOCTL_DMA_UNPIN: {
			unpinArea((struct dmaPage*)ioctlparam);
		} break;

		case IOCTL_DMA_MAP: {
			dma_mapPage(FPCI3.boards[board.id].pcidev, (struct dmaPage*)ioctlparam, DMA_BIDIRECTIONAL);
		} break;

		case IOCTL_DMA_UNMAP: {
			dma_unmapPage(FPCI3.boards[board.id].pcidev, (struct dmaPage*)ioctlparam, DMA_BIDIRECTIONAL);
		} break;

		case IOCTL_DMA_BUFFERPAGE: {
			struct bufferInfo bufferInfo;

			//dev_dbg(&pcidev->dev, pr_fmt("IOCTL: IOCTL_FPGA_PIN_PAGE\n"));

			if(!copy_from_user(&bufferInfo, (void*)ioctlparam, sizeof(struct bufferInfo))) {
				struct dmaPage* dmaPage = dmaPageForBuffer(board, bufferInfo.channel, bufferInfo.buffer);
				__put_user(dmaPage, (uintptr_t*)&((struct bufferInfo*)ioctlparam)->pageHandle);
			} else {
				return -EFAULT;
			}
		} break;

		case IOCTL_DISPATCH_SEND: {
			struct packetInfo packetInfo;

			//dev_dbg(&pcidev->dev, pr_fmt("IOCTL: IOCTL_FPGA_PIN_PAGE\n"));

			if(!copy_from_user(&packetInfo, (void*)ioctlparam, sizeof(struct packetInfo))) {
				dma_send(board, &packetInfo);
			} else {
				return -EFAULT;
			}
		} break;

		case IOCTL_DISPATCH_RECEIVE: {
			struct packetInfo packetInfo;

			//dev_dbg(&pcidev->dev, pr_fmt("IOCTL: IOCTL_FPGA_PIN_PAGE\n"));

			if(!copy_from_user(&packetInfo, (void*)ioctlparam, sizeof(struct packetInfo))) {
				dma_receive(board, &packetInfo);
			} else {
				return -EFAULT;
			}
		} break;

		case IOCTL_WAIT_PAGE: {
			waitPage((struct dmaPage*)ioctlparam);
		} break;
	}
	return 0;
}

///////////////////////////////////////////////////////
// FPGA DEVICE HANDLERS
///////////////////////////////////////////////////////

static struct dentry* __parent(struct dentry* dentry) {
	//missing locking and ordering according to SO (http://stackoverflow.com/a/29325055/258418)
	return dentry->d_parent;
}

static int mmap(struct file* file, struct vm_area_struct* vma) {
	int ret;
	int slot     = (uintptr_t)PDE_DATA(__parent(__parent(file->f_path.dentry))->d_inode);
	int channel  = (uintptr_t)proc_get_parent_data(file->f_inode);
	int bufferNo = (uintptr_t)PDE_DATA(file->f_inode);

	struct fpga_board* board = &FPCI3.boards[slot];
	struct buffer* buffer = &board->channel[channel].buffer[bufferNo];

	//printk("DATA Board id %d:%d:%d\n", slot, channel, buffer);

	vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;
	//vma->vm_page_prot = 0;
	vma->vm_page_prot   = pgprot_noncached(vma->vm_page_prot);

	//return ENOMEM;
	set_memory_uc((uintptr_t)buffer->memoryAddress, getPageCount(BUF_SIZE));
	ret=dma_mmap_attrs(&board->pcidev->dev, vma, buffer->memoryAddress, buffer->dmaAddress, BUF_SIZE, 0);
	set_memory_wb((uintptr_t)buffer->memoryAddress, getPageCount(BUF_SIZE));
	return ret;
}

///////////////////////////////////////////////////////
// MODULE INIT/EXIT FUNCTIONS
///////////////////////////////////////////////////////


static struct file_operations irq_proc_file_operations = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = ioctl,
	.mmap           = mmap
};

static int probe(struct pci_dev *dev, const struct pci_device_id *id) {
	char buf[20];
	int slot=0;
	int returnCode;

	down(&FPCI3.semaphore); {
		if(FPCI3.activeBoards >= MAX_BOARDS) {
			up(&FPCI3.semaphore);
			printk("Board rejected since the driver has no more slots. Recompile with larger `MAX_BOARDS`(currently %d) to work around this.\n", MAX_BOARDS);
			return ENOMEM;
		}

		FPCI3.activeBoards++;

		for(; slot<MAX_BOARDS; ++slot) {
			if(FPCI3.boards[slot].status == BOARD_SLOT_UNUSED) {
				FPCI3.boards[slot].status = BOARD_INITIALIZED;
				break;
			}
		}

	} up(&FPCI3.semaphore);

	FPCI3.boards[slot].pcidev = dev;
	//printk("Slot is: %d\n", slot);


	//FIXME: name
	printk("THE NAME:%s\n", pci_name(dev));
	if(IS_ERR(device_create(FPCI3.class, NULL, MKDEV(FPCI3.major, slot), 0, DEVICE_NAME"%02d", slot))) {
		printk("Failed to create the device\n");
		return ENODEV;
	}

	// == Enable PCI ==
	returnCode = pcim_enable_device(dev);

	if(returnCode) {
		printk(pr_fmt("Failed to ena(unsigned long)ble device (Error %d)\n"), returnCode);
		return returnCode;
	}

	pci_set_master(dev);

	//TODO: Performance improvement by supporting also 32 bit mode? (smaller addresses needed => less data to transfer for scatter gather).
	//Now we need to support 64 bits :(
	returnCode = pci_set_dma_mask(dev, DMA_BIT_MASK(32));

	if(returnCode) {
		printk(pr_fmt("Failed to set DMA MASK (Error %d)\n"), returnCode);
		return returnCode;
	}

	//Since the DMA api gurantees that the coherent DMA mask can be set to the same or smaller than the streaming DMA mask,
	//we dont need to check the return value.
	pci_set_consistent_dma_mask(dev,  DMA_BIT_MASK(32));

	//TODO: what if we have more than bar0 ? Should we use: pcim_iomap_regions_request_all (What do we need to request regions for?
	returnCode = pcim_iomap_regions(dev, 1, pci_name(dev));

	if(returnCode) {
		printk(pr_fmt("Failed to IOMAP (Error %d)\n"), returnCode);
		return returnCode;
	}

	FPCI3.boards[slot].ioTable = pcim_iomap_table(dev);

	// == Interrupts ==
	//clear_interrupts(dev);

	returnCode = pci_enable_msi(dev);

	if(returnCode) {
		printk(pr_fmt("Failed to enable MSI interrupt (Error %d)\n"), returnCode);
		return returnCode;
	}

	//TODO fix the name
	devm_request_irq(&dev->dev, dev->irq, interrupt_handler, 0, "BIERFIXME", dev);
	/*devm_request_threaded_irq(&pcidev->dev, pcidev->irq, interupt_handler, interupt_thread, 0, driverInfo->name, (void*)pcidev);

	// == Finalize ==
	dev_dbg(&pcidev->dev, pr_fmt("Board tied to slot %d\n"), boardId);
	 */

	printk("ASSIGNED SLOT %d\n", slot);

	//create Device folder/files
	sprintf(buf, "%s%02d", DEVICE_NAME, slot);
	FPCI3.boards[slot].proc_dir = devm_proc_mkdir_data(&dev->dev, buf, 0, FPCI3.proc_dir, (void*)(uintptr_t)slot);

	if (FPCI3.boards[slot].proc_dir == NULL) {
		printk("FAIL fpga dir\n");
		return -ENOMEM;
	}

	if (!proc_create_data(DEVICE_NAME, 0666, FPCI3.boards[slot].proc_dir, &irq_proc_file_operations, (void *)(uintptr_t)slot)) {
		printk("FAIL buff fpga file\n");
		return -ENOMEM;
	}

	for (int i = 0; i < MAX_CHANNELS; ++i) {
		static struct proc_dir_entry* channelProcDir;
		struct channel* channel = &FPCI3.boards[slot].channel[i];

		sprintf(buf, "%s%02d", FS_CHANNEL, i);
		channelProcDir = proc_mkdir_data(buf, 0, FPCI3.boards[slot].proc_dir, (void*)(uintptr_t)i);

		if (channelProcDir == NULL) {
			printk("FAIL channel dir\n");
			return -ENOMEM;
		}

		for(int j=0; j<COHERENT_BUFFERS; ++j) {
			channel->buffer[j].memoryAddress = dmam_alloc_coherent(&dev->dev, BUF_SIZE, &channel->buffer[j].dmaAddress, GFP_ATOMIC);

			if(!channel->buffer[j].memoryAddress) {
				printk("OUT OF CONSISTENT MEMORY\n");
				return ENOMEM;
			}

			sprintf(buf, "%s%02d", FS_BUFFER, j);

			if (!proc_create_data(buf, 0666, channelProcDir, &irq_proc_file_operations, (void *)(uintptr_t)j)) {
				printk("FAIL buff file\n");
				return -ENOMEM;
			}

			atomic_set(&FPCI3.boards[slot].channel[i].timeout, 10*1000); //10 sec

			//set_memory_uc(FPCI3.boards[slot].channel[i].buffer[j].memoryAddress, getPageCount(BUF_SIZE));
		}

		//setBufferPageArray(slot, i);

		for(int j=0; j<2; ++j) {
			atomic_set(&channel->pendings[j],  0);

			init_waitqueue_head(&channel->waitQueues[j] );

			INIT_LIST_HEAD(&channel->preDispatchQueues[j].operations);
			channel->preDispatchQueues[j].queueLength = 0;
			spin_lock_init(&channel->preDispatchQueues[j].queueLock);

			INIT_LIST_HEAD(&channel->dispatchQueues[j].operations);
			channel->dispatchQueues[j].queueLength = 0;

			INIT_LIST_HEAD(&channel->postDispatchQueues[j].operations);
			channel->postDispatchQueues[j].queueLength = 0;

			sg_memoryIteratorSetNull(&channel->dispatchIterators[j]);
		}
	}

	pci_set_drvdata(dev, (void*)(uintptr_t)slot);
	return 0;
}

static void remove(struct pci_dev *pcidev) {
	int slot=(uintptr_t)pci_get_drvdata(pcidev);

	//pcidev->dev.devt is 0 here
	device_destroy(FPCI3.class, MKDEV(FPCI3.major, slot));

	for(int i=0; i<MAX_CHANNELS;++i) {
		for(int j=0; j<COHERENT_BUFFERS;++j) {
			//set_memory_wb(FPCI3.boards[slot].channel[i].buffer[j].memoryAddress, getPageCount(BUF_SIZE));
		}
	}

	down(&FPCI3.semaphore);

	FPCI3.activeBoards--;
	FPCI3.boards[MINOR(pcidev->dev.devt)].status = BOARD_SLOT_UNUSED;

	up(&FPCI3.semaphore);
}

struct pci_device_id fpga_id_table[] = {
	{ PCI_DEVICE(VENDOR_ID, DEVICE_ID) },
	{ 0 }
};

//export device table for hotplug
MODULE_DEVICE_TABLE(pci, fpga_id_table);

struct pci_driver fpga_driver = {
	.name     = DEVICE_NAME,
	.id_table = fpga_id_table,
	.probe    = probe,
	.remove   = remove
};

static const struct file_operations fpga_fileOperations = {
	.owner   = THIS_MODULE//,
	//.open    = xc_open,
	//.release = xc_release//,
	//.mmap    = mmap
};

static int driver_load(void) {
	int errorCode;

	sg_init_table(&FPCI3.sentinelScatterlist, 1);
	FPCI3.activeBoards = 0;
	sema_init(&FPCI3.semaphore, 1);

	for(int i=0; i<MAX_BOARDS; ++i) {
		FPCI3.boards[i].status = BOARD_SLOT_UNUSED;
	}

	mb(); //make sure initialization values are visible to all cores befroe starting to add devices.
	//smp_mb__after_atomic(); //Force that our atomic initialization becomes visible before we start adding devices.


	FPCI3.major = register_chrdev(MAJOR_NUM, DEVICE_NAME, &fpga_fileOperations);
	if(FPCI3.major < 0) {
		printk(pr_fmt("Registering the character device failed with %d\n"), FPCI3.major);
		return FPCI3.major;
	}

	//register_chrdev returns 0 on success if the major number is set statically
	if(MAJOR_NUM) {
		FPCI3.major = MAJOR_NUM;
	}

	printk(pr_fmt(DEVICE_NAME " registered with major number %d\n"), FPCI3.major);

	FPCI3.class = class_create(THIS_MODULE, DEVICE_NAME);
	if(IS_ERR(FPCI3.class)) {
		printk(KERN_INFO pr_fmt("class_create() for " DEVICE_NAME " failed %ld\n"), PTR_ERR(FPCI3.class));
		return PTR_ERR(FPCI3.class);
	}

	FPCI3.proc_dir = proc_mkdir(DEVICE_NAME, NULL);

	if (FPCI3.proc_dir == NULL) {
		return -ENOMEM;
	}

	errorCode = pci_register_driver(&fpga_driver);
	if(errorCode) {
		printk(pr_fmt("Registering the driver failed with error code %d\n"), errorCode);
		return errorCode;
	}

	return (0);
}

static void driver_unload(void) {
	pci_unregister_driver(&fpga_driver);

	proc_remove(FPCI3.proc_dir);
	class_destroy(FPCI3.class);
	unregister_chrdev(FPCI3.major, DEVICE_NAME);
}

module_init(driver_load);
module_exit(driver_unload);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("PCIe driver for FPGA");
MODULE_AUTHOR("Malte Vesper");
MODULE_VERSION("0.001b");
