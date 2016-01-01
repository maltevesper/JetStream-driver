#include <stdio.h>
#include <stdlib.h>
#include <libfpga.h>

#include <unistd.h>

int main(int argc, char* argv[])
{
	int value;
	int boardNo = 0;
	int registerNo = SCR_REG;
	
	if(argc > 1) {
		registerNo = atoi(argv[1]);
	}
	
	if(argc>3) {
		boardNo = atoi(argv[3]);
	}
	
	struct boardHandle fpga = fpga_open(boardNo);
	
	if(fpga.id < 0) {
		printf("Failed to open FPGA\n");
		return 1;
	}
	
	unsigned int registerValue = fpga_read_register(fpga, registerNo);

	printf("Register is %10u(%08x)\n", registerValue, registerValue);

	if(argc==1) {
		printf("Usage: %s [<register>] [<value>] [<boardNo>]\n", argv[0]);
		printf("[<boardNo>] defaults to 0, [<register>] defaults to the scratch register, and without a value no write occurs\n");
	}

	if(argc > 2) {
		value = atoi(argv[2]);
		fpga_write_register(fpga, registerNo, value);
		
		printf("Wrote:      %10u(%08x)\n", value, value);
	}
	return 0;
}
