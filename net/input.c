#include "ns.h"

extern union Nsipc nsipcbuf;

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
	struct jif_pkt *p = (struct jif_pkt *)&nsipcbuf;
	int size;
	int r;
	uint8_t* data = (uint8_t *) UTEMP;
	while(1) {
		r = sys_page_alloc(0, UTEMP, PTE_U|PTE_P|PTE_W);
		//while( (p->jp_len = sys_receive_packet(p->jp_data)) < 0) {
	//		sys_yield();
	//	}
		while ( (r = sys_receive_packet((void*)(((int*)UTEMP)+1 ))) < 0) {
			sys_yield();
		}
		((int *) UTEMP) [0] = r;
		ipc_send(ns_envid, NSREQ_INPUT, UTEMP,  PTE_P|PTE_W|PTE_U);
		r = sys_page_alloc(0, UTEMP, PTE_U|PTE_P|PTE_W);
		if( r< 0)
			panic("not enough memory..");
			
		
	}
	
}
