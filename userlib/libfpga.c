
#define _GNU_SOURCE
#define ERRINUSE -2
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "libfpga.h"

struct boardHandle fpga_open(int fpga) {
	struct boardHandle fpgaHandle;
	char buf[50];

	sprintf(buf, "%s%02d/%s", FPGA_DEV_PATH_BASE, fpga, FPGA_DEV_FILE);
	printf("Opening %s\n", buf);
	fpgaHandle.id = open(buf, O_RDWR);

	return fpgaHandle;
}

void fpga_close(struct boardHandle fpga) {
	close(fpga.id);
}


uint64_t fpga_read_register(struct boardHandle fpga, uint32_t registerNo) {
	struct registerInfo registerInfo = {
		.registerNo = registerNo
	};

	return ioctl(fpga.id, IOCTL_REGISTER_READ, &registerInfo);
}

void fpga_write_register(struct boardHandle fpga, uint32_t registerNo, uint64_t value) {
	struct registerInfo registerInfo = {
		.registerNo = registerNo,
		.value      = value
	};

	ioctl(fpga.id, IOCTL_REGISTER_WRITE, &registerInfo);
}


int fpga_send_data(struct boardHandle fpga, int channel, unsigned char* start, int size) {
	struct packetInfo packet = {
		.start   = start,
		.size    = size,
		.channel = channel
	};

	ioctl(fpga.id, IOCTL_BOUNCE_SEND, &packet);

	return 0;
}

int fpga_receive_data(struct boardHandle fpga, int channel, unsigned char* start, int size) {
	struct packetInfo packet = {
		.start   = start,
		.size    = size,
		.channel = channel
	};

	ioctl(fpga.id, IOCTL_BOUNCE_RECEIVE, &packet);

	return 0;
}

void fpga_dma_pin(struct boardHandle fpga, struct memoryInfo* userMemory) {
	ioctl(fpga.id, IOCTL_DMA_PIN, userMemory);
}

void fpga_dma_unpin(struct boardHandle fpga, struct dmaPage* dmaPage) {
	ioctl(fpga.id, IOCTL_DMA_UNPIN, dmaPage);
}

void fpga_dma_map(struct boardHandle fpga, struct dmaPage* dmaPage) {
	ioctl(fpga.id, IOCTL_DMA_MAP, dmaPage);
}

void fpga_dma_unmap(struct boardHandle fpga, struct dmaPage* dmaPage) {
	ioctl(fpga.id, IOCTL_DMA_UNMAP, dmaPage);
}

uintptr_t fpga_bufferPageHandle(struct boardHandle fpga, int channel, int buffer) {
	struct bufferInfo bufferInfo = {
		.buffer  = buffer,
		.channel = channel,
		.pageHandle = 12
	};
	ioctl(fpga.id, IOCTL_DMA_BUFFERPAGE, &bufferInfo);
	return bufferInfo.pageHandle;
}

void fpga_dma_send(struct boardHandle fpga, struct packetInfo* packetInfo) {
	ioctl(fpga.id, IOCTL_DISPATCH_SEND, packetInfo);
}

void fpga_dma_receive(struct boardHandle fpga, struct packetInfo* packetInfo) {
	ioctl(fpga.id, IOCTL_DISPATCH_RECEIVE, packetInfo);
}

void fpga_wait_page(struct boardHandle fpga, struct dmaPage* dmaPage) {
	ioctl(fpga.id, IOCTL_WAIT_PAGE, dmaPage);
}
