#include <stdio.h>
#include <stdlib.h>
#include <fpga.h>

#include <assert.h>
#include <sys/time.h>
#include <unistd.h>

#define DATA_POINTS (1024*1024*1024) //Size of current DMA write
unsigned int senddata[DATA_POINTS/4]; //Buffer to hold the send data

int main(int argc, char* argv[])
{
	int offset, value;
	int* buffer;
	if(argc!=3&&argc!=2) {
		printf("Usage (write): %s <offset> <value>\n", argv[0]);
		printf("Usage (read) : %s <offset>\n", argv[0]);
		return 1;
	}

	buffer=fpga_mapBuffer();

	offset = atoi(argv[1]);

	switch(argc) {
	case 2:
		printf("Read: %x\n", buffer[offset]);
		break;
	case 3:
		value = atoi(argv[2]);
		buffer[offset] = value;
		printf("Wrote: %x\n", value);
		break;
	}
	return 0;
}
