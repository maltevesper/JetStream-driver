//Based on code by Vipin, adjusted for new driver by Malte Vesper
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 
#include <sys/time.h>
#include <libfpga.h>
#include <errno.h>

//gcc -g filter.c -lfpga -o rfilter

#define HEADER_SIZE 1078
#define IMAGE_SIZE (512*512)

int main(int argc, char *argv[]) {

	FILE *in_file;
	FILE *out_file;
	char *file_data;

	int sent = 0;

	struct timeval start, end;
	long secs_used,micros_used;

	int channel=1;
	struct boardHandle fpga = fpga_open(0);


	in_file  = fopen("lena.bmp","rb");

	if (!in_file) {
		fprintf(stderr, "Unable to open input file lena.bmp\n");
		return -1;
	}

	out_file = fopen("lena_out.bmp","wb");

	if (!out_file) {
		fprintf(stderr, "Unable to open output file lena_out.bmp\n");
		return -1;
	}


	file_data   = (char *)malloc(IMAGE_SIZE);

	if(!file_data) {
		fprintf(stderr, "Unable to allocate buffer\n");
		return -ENOMEM;
	}

	//copy header
	fread( file_data, 1, HEADER_SIZE, in_file);
	fwrite(file_data, 1, HEADER_SIZE, out_file);

	fread(file_data, 1, IMAGE_SIZE, in_file);

	fclose(in_file);



	gettimeofday(&start, NULL);

	while(sent < IMAGE_SIZE)
	{
		fpga_send_data(fpga, channel,(unsigned char *) file_data+sent, 4096);
		fpga_receive_data(fpga, channel,(unsigned char *) file_data+sent, 4096);
		sent += 4096;
	}

	gettimeofday(&end, NULL);

	secs_used=(end.tv_sec - start.tv_sec); //avoid overflow by subtracting first
	micros_used= ((secs_used*1000000) + end.tv_usec) - (start.tv_usec);

	printf("Throughput %f MBytes/sec\n", IMAGE_SIZE*2.0/micros_used);

	fwrite(file_data, 1, IMAGE_SIZE, out_file);
	fclose(out_file);

	system("eog lena.bmp &");
	system("eog lena_out.bmp &");

	free(file_data);

	fpga_close(fpga);

	return 0;
}
