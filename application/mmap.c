#include <stdio.h>
#include <stdlib.h>
#include <libfpga.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>

#include <fcntl.h>

int main(int argc, char* argv[])
{
	char bufferPath[128];
	uint64_t* buffer;
	int filepointer;
	int board = 0;
	int channel = 1;
	int bufferNo = 0;


	snprintf(bufferPath, 128, "%s%02d/%s%02d/%s%02d", FPGA_DEV_PATH_BASE, board, FPGA_DEF_PATH_CHANNEL, channel, FPGA_DEF_PATH_BUFFER, bufferNo);
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

	if(argc>1) {
		int offset=atoi(argv[1]);
		printf("Value read: %16ld  (%016lx)\n", buffer[offset], buffer[offset]);

		if(argc>=3) {
			uint64_t value=atol(argv[2]);
			buffer[offset]=value;
			printf("Wrote       %16ld  (%016lx)\n", value, value);
		}
	} else {
		printf(
			"Usage: %s[ offset[ value]\n"
			"read mapped file @ offset. If value is present write this value to the location.\n",
			argv[0]
		);
	}

	munmap((void*)buffer, 4096);

	return 0;
}
