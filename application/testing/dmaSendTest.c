#include <stdio.h>
#include <stdlib.h>
#include <libfpga.h>
#include <assert.h>
#include <sys/time.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/mman.h>

#define DATA_POINTS (1024*1024*1024) //Size of current DMA write
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
	uint64_t* buffer;
	int filepointer;
	int bufferNo = 0;

	struct packetInfo packetInfo;
	
	unsigned char* bufferBytes;

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

	snprintf(bufferPath, 128, "%s%02d/%s%02d/%s%02d", FPGA_DEV_PATH_BASE, boardNo, FPGA_DEF_PATH_CHANNEL, channel, FPGA_DEF_PATH_BUFFER, bufferNo);
	filepointer = open(bufferPath, O_RDWR);

	if(filepointer < 0) {
		printf("Could not open %s.\n", bufferPath);
		return -1;
	}

	buffer = (uint64_t*) mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, filepointer, 0);

	if(!buffer) {
		printf("Failed to mmap\n");
		return -1;
	}

	bufferBytes = (unsigned char*)buffer;
	
	for(int j=0; j<16; j++) {
		bufferBytes[j] = 0;
	}
	
	printf("\nBuffer contents: ");
	for(int j=0; j<16; j++) {
		printf("%02X ", bufferBytes[j]);
	}
	
	board = fpga_open(boardNo);

	packetInfo.pageHandle = fpga_bufferPageHandle(board, channel, bufferNo);
	packetInfo.size = 128;
	packetInfo.channel = channel;

	while(argc<4 || rounds--) {
		gettimeofday(&start, NULL);
		//fpga_send_data(board, channel,(unsigned char *) senddata,DATA_POINTS);
		for(int i=0; i<DATA_POINTS/MAX_TRANSFER_SIZE;++i) {
			//fpga_dma_send(board, &packetInfo);
			fpga_dma_receive(board, &packetInfo);
			while(1) {
				sleep(1);
				printf("\nBuffer contents: ");
				for(int j=0; j<16; j++) {
					printf("%02X ", ((volatile unsigned char*)bufferBytes)[j]);
				}
			}
			fpga_wait_page(board, (struct dmaPage*)packetInfo.pageHandle);
		}
		gettimeofday(&end, NULL);
		secs_used=(end.tv_sec - start.tv_sec); //avoid overflow by subtracting first
		micros_used= ((secs_used*1000000) + end.tv_usec) - (start.tv_usec);
		printf("[%ld] micros_used: %ld\n",start.tv_sec, micros_used);
		printf("Throughput %f MBytes/sec\n",DATA_POINTS*1.0/micros_used);
		fflush(stdout);
	}

	fpga_close(board);
	munmap((void*)buffer, 4096);

	return 0;
}
