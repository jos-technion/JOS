#ifndef JOS_KERN_E100_H
#define JOS_KERN_E100_H

#include <kern/pci.h>
#include <kern/pmap.h>
#include <inc/e100.h>


#define E100_VENDOR_ID  32902
#define E100_DEVICE_ID 4617
#define MAX_ETH_FRAME_SIZE 1518


struct socket_data {
	int size;
	void *buf; //a max value of 4096-4=4092 bytes
};


int 	e100_attach(struct pci_func *pcif);
int 	e100_send_packet(void *buf, int size);
int 	e100_receive_packet(void *buf);
void	e100_handle_interrupt();
int	e100_get_mac_address(uint8_t hwaddr[]);
extern uint8_t e100_irq;
#endif	// JOS_KERN_E100_H
