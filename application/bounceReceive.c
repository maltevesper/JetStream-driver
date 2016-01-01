#include <stdio.h>
#include <stdlib.h>
#include <libfpga.h>
#include <assert.h>
#include <sys/time.h>
#include <unistd.h>

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

	for(int i=0;i<1024;i++)
		senddata[i] = i;

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
		sleep(5);
	}

	printf("BOARD[CHANNEL]: %d[%d] @ %d Rounds\n\n", boardNo, channel, rounds);

	board = fpga_open(boardNo);

	while(argc<4 || rounds--) {
		gettimeofday(&start, NULL);
		fpga_receive_data(board, channel,(unsigned char *) senddata,DATA_POINTS);
		gettimeofday(&end, NULL);
		secs_used=(end.tv_sec - start.tv_sec); //avoid overflow by subtracting first
		micros_used= ((secs_used*1000000) + end.tv_usec) - (start.tv_usec);
		printf("[%ld] micros_used: %ld\n",start.tv_sec, micros_used);
		printf("Throughput %f MBytes/sec\n",DATA_POINTS*1.0/micros_used);
		fflush(stdout);
	}

	fpga_close(board);

	return 0;
}
