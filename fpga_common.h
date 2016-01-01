/*
 * fpga_common.h
 *
 *  Created on: 4 Apr 2015
 *      Author: ted
 */

#ifndef FPGA_COMMON_H_
#define FPGA_COMMON_H_

#include <linux/ioctl.h>
#include <linux/types.h>

/* ======================================================================================================
 * Configure here
 * ====================================================================================================== */

//ioctl number used by driver (129 ü (0x81) not used according to Documentation/ioctl/ioctl-number.txt)
#define IOCTL_NUMBER 0x81
//ioctl base added to the operations index (make sure we are somewhat in the upper end to avoid collisions
#define IOCTL_OFFSET 0x00

//maximum number of supported boards
#define MAX_BOARDS 4

//coherent buffers per channel
#define COHERENT_BUFFERS 3

#define MAX_TRANSFER_SIZE			(4*1024*1024)	   // DMA buffer size

#define FS_CHANNEL "channel"

#define FS_BUFFER  "buffer"


#define DEVICE_NAME "fpga"

/* ======================================================================================================
 * Data Structures
 * ====================================================================================================== */

struct dmaPage;

struct boardHandle {
	int id;
};

struct bufferInfo {
	int channel;
	int buffer;
	uintptr_t pageHandle;
};

struct registerInfo {
	uint32_t registerNo;
	uint64_t value;
};

struct memoryInfo {
	void* start;
	size_t size;
	uintptr_t pageHandle;
};

struct packetInfo {
	union {
		void* start;
		uintptr_t pageHandle;
	};

	size_t size;
	int channel;
};

/* ======================================================================================================
 * IOCTLs
 * ====================================================================================================== */

#define IOCTL_REGISTER_WRITE   _IOW(IOCTL_NUMBER, IOCTL_OFFSET + 0, unsigned int)
#define IOCTL_REGISTER_READ    _IOR(IOCTL_NUMBER, IOCTL_OFFSET + 1, unsigned int)


#define IOCTL_BOUNCE_SEND      _IOW(IOCTL_NUMBER, IOCTL_OFFSET + 20, unsigned int)
#define IOCTL_BOUNCE_RECEIVE   _IOW(IOCTL_NUMBER, IOCTL_OFFSET + 21, unsigned int)


#define IOCTL_DMA_PIN          _IOR(IOCTL_NUMBER, IOCTL_OFFSET + 30, unsigned int)
#define IOCTL_DMA_UNPIN        _IOW(IOCTL_NUMBER, IOCTL_OFFSET + 31, unsigned int)
#define IOCTL_DMA_MAP          _IOW(IOCTL_NUMBER, IOCTL_OFFSET + 32, unsigned int)
#define IOCTL_DMA_UNMAP        _IOW(IOCTL_NUMBER, IOCTL_OFFSET + 33, unsigned int)

#define IOCTL_DMA_BUFFERPAGE   _IOR(IOCTL_NUMBER, IOCTL_OFFSET + 35, unsigned int)


#define IOCTL_DISPATCH_SEND    _IOW(IOCTL_NUMBER, IOCTL_OFFSET + 40, unsigned int)
#define IOCTL_DISPATCH_RECEIVE _IOW(IOCTL_NUMBER, IOCTL_OFFSET + 41, unsigned int)

#define IOCTL_WAIT_PAGE        _IOW(IOCTL_NUMBER, IOCTL_OFFSET + 46, unsigned int)

/* ======================================================================================================
 * REGISTER NUMBERS
 * ====================================================================================================== */
#define VER_REG                	0x00      // Version
#define SCR_REG                	0x04      // Scratch pad
#define CTRL_REG               	0x08      // Control
#define STA_REG                	0x10      // Status
#define UCTR_REG               	0x18      // User control register
#define PIOA_REG               	0x20      // PIO address
#define PIOD_REG               	0x24      // PIO read/write register
#define PC_DDR_DMA_SYS_REG     	0x28      // DMA system memory address
#define PC_DDR_DMA_FPGA_REG    	0x2C      // DMA local DDR address
#define PC_DDR_DMA_LEN_REG     	0x30      // DMA length
#define DDR_PC_DMA_SYS_REG     	0x34
#define DDR_PC_DMA_FPGA_REG    	0x38
#define DDR_PC_DMA_LEN_REG     	0x3C
#define ETH_SEND_DATA_SIZE 	    0x40
#define ETH_RCV_DATA_SIZE  	    0x44
#define ETH_DDR_SRC_ADDR        0x48
#define ETH_DDR_DST_ADDR        0x4C
#define RECONFIG_ADDR           0x50
#define PC_USER1_DMA_SYS   	    0x60
#define PC_USER1_DMA_LEN     	0x64
#define USER1_PC_DMA_SYS    	0x68
#define USER1_PC_DMA_LEN     	0x6C
#define USER1_DDR_STR_ADDR  	0x70
#define USER1_DDR_STR_LEN    	0x74
#define DDR_USER1_STR_ADDR  	0x78
#define DDR_USER1_STR_LEN   	0x7C
#define PC_USER2_DMA_SYS   	    0x80
#define PC_USER2_DMA_LEN    	0x84
#define USER2_PC_DMA_SYS    	0x88
#define USER2_PC_DMA_LEN    	0x8C
#define USER2_DDR_STR_ADDR  	0x90
#define USER2_DDR_STR_LEN   	0x94
#define DDR_USER2_STR_ADDR  	0x98
#define DDR_USER2_STR_LEN   	0x9C
#define PC_USER3_DMA_SYS    	0xA0
#define PC_USER3_DMA_LEN    	0xA4
#define USER3_PC_DMA_SYS    	0xA8
#define USER3_PC_DMA_LEN     	0xAC
#define USER3_DDR_STR_ADDR   	0xB0
#define USER3_DDR_STR_LEN   	0xB4
#define DDR_USER3_STR_ADDR  	0xB8
#define DDR_USER3_STR_LEN   	0xBC
#define PC_USER4_DMA_SYS     	0xC0
#define PC_USER4_DMA_LEN     	0xC4
#define USER4_PC_DMA_SYS     	0xC8
#define USER4_PC_DMA_LEN     	0xCC
#define USER4_DDR_STR_ADDR  	0xD0
#define USER4_DDR_STR_LEN   	0xD4
#define DDR_USER4_STR_ADDR  	0xD8
#define DDR_USER4_STR_LEN   	0xDC

#endif /* FPGA_COMMON_H_ */
