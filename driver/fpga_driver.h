#ifndef FPGA_DRIVER_H
#define FPGA_DRIVER_H

#include "../fpga_common.h"

#define MAJOR_NUM 100

#define VENDOR_ID 0x10EE
#define DEVICE_ID 0x7038

/*
 * Size definitions.
 */
#define BUF_SIZE					(4*1024*1024)	   // DMA buffer size TODO: replace by MAX_TRANSFER_SIZE
#define MAX_CHANNELS  12

#define DISPATCH_QUEUE_LENGTH 1

enum fpga_data_direction {
	HOST_TO_FPGA = 0,
	FPGA_TO_HOST = 1
};


#define BITPOS_SEND_DDR_DATA            0
#define BITPOS_RECV_DDR_DATA            1
#define BITPOS_ENET                     2
#define BITPOS_USER                     3
#define BITPOS_REBOOT                   3
#define BITPOS_SEND_USER1_DATA          4
#define BITPOS_RECV_USER1_DATA          5
#define BITPOS_SEND_DDR_USER1_DATA      6
#define BITPOS_SEND_USER1_DDR_DATA      7
#define BITPOS_SEND_USER2_DATA          8
#define BITPOS_RECV_USER2_DATA          9
#define BITPOS_SEND_DDR_USER2_DATA     10
#define BITPOS_SEND_USER2_DDR_DATA     11
#define BITPOS_SEND_USER3_DATA         12
#define BITPOS_RECV_USER3_DATA         13
#define BITPOS_SEND_DDR_USER3_DATA     14
#define BITPOS_SEND_USER3_DDR_DATA     15
#define BITPOS_SEND_USER4_DATA         16
#define BITPOS_RECV_USER4_DATA         17
#define BITPOS_SEND_DDR_USER4_DATA     18
#define BITPOS_SEND_USER4_DDR_DATA     19
#define BITPOS_RECONFIG                20

#define IRSTATUSMASK(ir) (0x1<<(ir))


/*DMA destination enumeration*/
typedef enum dma_type {hostddr,ddrhost,hostuser1,hostuser2,hostuser3,hostuser4,user1host,user2host,user3host,user4host,ddruser1,ddruser2,ddruser3,ddruser4,user1ddr,user2ddr,user3ddr,user4ddr,enet,user,config} DMA_TYPE;

#endif
