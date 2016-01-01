#include <stdio.h>
#include <stdlib.h>
#include <libfpga.h>

#include <unistd.h>

int main(int argc, char* argv[])
{
	int value;
	int boardNo = 0;
	
	if(argc>2) {
		boardNo = atoi(argv[2]);
	}
	
	struct boardHandle fpga = fpga_open(boardNo);
	
	if(fpga.id < 0) {
		printf("Failed to open FPGA\n");
		return 1;
	}
	
	unsigned int registerValue = fpga_read_register(fpga, SCR_REG);

	printf("Scratch register is %10u(%08x)\n", registerValue, registerValue);

	if(argc==1) {
		printf("Usage: %s <value> [<boardNo>]\n", argv[0]);
		return 1;
	}

	value = atoi(argv[1]);
	fpga_write_register(fpga, SCR_REG, value);

	printf("Wrote:              %10u(%08x)\n", value, value);
	return 0;
}
