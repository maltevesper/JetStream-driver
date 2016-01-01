#ifndef FPGA_COMM_H
#define FPGA_COMM_H

#include <stdint.h>
#include <fpga_common.h>

#ifdef __cplusplus
extern "C" {
#endif


/* Paths in proc and dev filesystems */
#define FPGA_DEV_PATH_BASE "/proc/" DEVICE_NAME "/" DEVICE_NAME
#define FPGA_DEV_FILE DEVICE_NAME
#define FPGA_DEF_PATH_CHANNEL "channel"
#define FPGA_DEF_PATH_BUFFER "buffer"
//#define FPGA_DEV_PATH_BASE "/dev/" DEVICE_NAME

struct thread_args;
typedef struct thread_args thread_args;
struct fpga_dev;
struct sys_stat;
typedef struct fpga_dev fpga_dev;

typedef enum dma_point {ICAP,USERPCIE1,USERPCIE2,USERPCIE3,USERPCIE4} DMA_PNT;


struct boardHandle fpga_open(int fpga);
void               fpga_close(struct boardHandle fpga);

uint64_t fpga_read_register(struct boardHandle fpga, uint32_t registerNo);
void     fpga_write_register(struct boardHandle fpga, uint32_t registerNo, uint64_t value);

int fpga_send_data(struct boardHandle fpga, int channel, unsigned char* start, int size);
int fpga_receive_data(struct boardHandle fpga, int channel, unsigned char* start, int size);

void fpga_dma_pin(struct boardHandle fpga, struct memoryInfo* userMemory);
void fpga_dma_unpin(struct boardHandle fpga, struct dmaPage* dmaPage);

void fpga_dma_map(struct boardHandle fpga, struct dmaPage* dmaPage);
void fpga_dma_unmap(struct boardHandle fpga, struct dmaPage* dmaPage);

uintptr_t fpga_bufferPageHandle(struct boardHandle fpga, int channel, int buffer);

void fpga_dma_send(struct boardHandle fpga, struct packetInfo* packetInfo);
void fpga_dma_receive(struct boardHandle fpga, struct packetInfo* packetInfo);
void fpga_wait_page(struct boardHandle fpga, struct dmaPage* dmaPage);

#ifdef __cplusplus
}
#endif

#endif
