#include "ns.h"
#include <net/ns.h>
#include <inc/lib.h>

extern union Nsipc nsipcbuf;
void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
	struct jif_pkt *p = (struct jif_pkt *)&nsipcbuf;
	envid_t from;
	int32_t req, perm;

	while(1) { //listening to new ipcs
		//cprintf("ana hon\n");
		req = ipc_recv(&from, &nsipcbuf ,&perm);

		if(! (perm & PTE_P)) {
			cprintf("Output env: invalid request from %08x: page not present\n",from);

			sys_yield();
			continue;
		}
		if(ns_envid != from) {
			cprintf("Output env: request not from network server: %08x, %08x\n",from,ns_envid);
			sys_yield();
			continue;
		}
		if(req != NSREQ_OUTPUT) {
			cprintf("Output env: invalid request\n");
			sys_yield();
			continue;
		}
		if((req = sys_send_packet(p->jp_data, p->jp_len)) <0) {
			cprintf("Output env: couldn't transmit output: %e\n",req);
		}
	}
	//sys_page_unmap(0, (void *)PKTMAP);
	
}
