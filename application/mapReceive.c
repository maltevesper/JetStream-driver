/*
 * mapSend.c
 *
 *  Created on: 9 Apr 2015
 *      Author: ted
 */

#include <stdio.h>
#include <stdlib.h>
#include <libfpga.h>
#include <assert.h>
#include <sys/time.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/mman.h>

#define DATA_POINTS (1024*12) //*1024) //Size of current DMA write
unsigned int senddata[DATA_POINTS/4]; //Buffer to hold the send data

int main(int argc, char* argv[])
{
	struct timeval start, end;
	long secs_used,micros_used;
	int channel=1;
	int boardNo=0;
	int rounds=0;
	struct boardHandle board;

	char bufferPath[128];
	void* buffer;
	int filepointer;
	int bufferNo = 0;

	struct packetInfo packetInfo;
	struct memoryInfo memoryInfo;

	int error;

	//for(int i=0;i<1024;i++)
	//	senddata[i] = i;

	if(argc>1) {
		channel = atoi(argv[1]);
	}

	if(argc>2) {
		boardNo = atoi(argv[2]);
	}

	if(argc>3) {
		rounds = atoi(argv[3]);
	}

	if(argc==1) {
		printf("Usage: %s[ channel[ board[ rounds]]]\n\n", argv[0]);
		//sleep(5);
	}

	printf("BOARD[CHANNEL]: %d[%d] @ %d Rounds\n\n", boardNo, channel, rounds);

	if(error = posix_memalign(&buffer, 4096, DATA_POINTS)) {
		//std::cout << "Could not get a page." << std::endl;
		return error;
	}

	memoryInfo.start = buffer;
	memoryInfo.size  = DATA_POINTS;

	board = fpga_open(boardNo);

	fpga_dma_pin(board, &memoryInfo);
	fpga_dma_map(board, (struct dmaPage*)memoryInfo.pageHandle);

	packetInfo.pageHandle = memoryInfo.pageHandle;
	packetInfo.size = DATA_POINTS;
	packetInfo.channel = channel;

	while(argc<4 || rounds--) {
		gettimeofday(&start, NULL);
		//fpga_send_data(board, channel,(unsigned char *) senddata,DATA_POINTS);
		fpga_dma_receive(board, &packetInfo);
		fpga_wait_page(board, (struct dmaPage*)packetInfo.pageHandle);
		gettimeofday(&end, NULL);
		secs_used=(end.tv_sec - start.tv_sec); //avoid overflow by subtracting first
		micros_used= ((secs_used*1000000) + end.tv_usec) - (start.tv_usec);
		printf("micros_used: %ld\n",micros_used);
		printf("Throughput %f MBytes/sec\n",DATA_POINTS*1.0/micros_used);
	}

	fpga_close(board);

	fpga_dma_unmap(board, (struct dmaPage*)memoryInfo.pageHandle);
	fpga_dma_unpin(board, (struct dmaPage*)memoryInfo.pageHandle);

	return 0;
}
