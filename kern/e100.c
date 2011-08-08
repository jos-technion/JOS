#include <kern/e100.h>
#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/x86.h>
#include <kern/pmap.h>
#include <kern/picirq.h>
#include <inc/error.h>




/***************************************************
* E100 Variables
***************************************************/
#define E100_TX_SLOTS			64 // # of transmit slots
#define E100_RX_SLOTS			5 // # of receive slots

/***************************************************
* E100 Constants
**************************************************/
#define E100_NULL      			0xffffffff
#define E100_SIZE_MASK			0x3fff  // mask out status/control bits
#define E100_CSR_PORT			0x08  // port (4 bytes)
#define E100_THRS			0xE0
#define E100_MAX_PACKET_SIZE		1518

#define E100_SCB_STATACK_CNA		0x20
#define E100_SCB_STATACK_CXTNO		0x80
#define E100_SCB_STATACK_FR		0x40

#define E100_SCB_STATUS			0x0
#define E100_SCB_CMD_HIGH_BITS		0x3
#define E100_SCB_CMD_LOW_BITS		0x2

#define E100_CSR_SCB_STATACK		0x01
#define E100_CSR_SCB_COMMAND		0x02
#define E100_SCB_GENERAL_POINTER	0x4

#define E100_EEPROM_CONTROL		0x0E
#define E100_EEPROM_EESK		(1 << 0)
#define E100_EEPROM_EECS		(1 << 1)
#define E100_EEPROM_EEDI		(1 << 2)
#define E100_EEPROM_EEDO		(1 << 3)

#define E100_MAC_ADDRESS_SIZE		6



/**************************************************
* E100 Commands
**************************************************/
#define E100_PORT_SOFTWARE_RESET  	0  // Software reset command
#define E100_SCB_COMMAND_CU_START	0x10
#define E100_SCB_COMMAND_CU_RESUME	0x20
#define E100_SCB_COMMAND_RU_START	0x01
#define E100_SCB_COMMAND_RU_RESUME	0x02
#define E100_CB_COMMAND_TX	    	0x4
	
/**************************************************
* E100 Flags
**************************************************/

#define E100_CB_COMMAND_I    		0x2000  // interrupt on completion
#define E100_CB_COMMAND_S    		0x4000  // suspend on completion

#define E100_RU_COMMAND_S		(1<< 14)
#define E100_RFA_STATUS_OK		0x2000
#define E100_RFA_STATUS_C		0x8000
#define E100_RFA_S			(1<<14)
#define E100_CB_STATUS_C    0x8000
#define E100_RFA_SUSPENDED		(1<<2)

#define E100_RFA_RNR			(1 << 12)
#define E100_SCB_CNA			(1 << 13)



uint8_t e100_irq;

static int initialize_dma();
struct cb_tx_header {
	volatile uint16_t status;
	volatile uint16_t cmd;
	volatile uint32_t link;
	volatile uint32_t tbd_array_addr;
	volatile uint16_t byte_count;
	volatile uint8_t thrs;
	volatile uint8_t tbd_count;
};
struct cb_tx {
	volatile struct cb_tx_header header;
	char buff[MAX_ETH_FRAME_SIZE];
};
struct cb_rx {
	volatile uint16_t status;
	volatile uint16_t cmd;
	volatile uint32_t link;
	volatile uint32_t rbd_array_addr;
	volatile uint16_t actual_size;
	volatile uint16_t size;
	char buf[MAX_ETH_FRAME_SIZE];
};



static struct  {
	uint8_t irq_line;
	uint32_t io_base;
	struct cb_tx tx[E100_TX_SLOTS];
	struct cb_rx rx[E100_RX_SLOTS];
	int rx_suspended;
	int rx_get;
	int rx_last;
	int tx_suspended;
	int tx_get;
	int tx_put;
	

} e100_card;



static void
e100_send_cmd(int cmd) {
	outb(e100_card.io_base + E100_CSR_SCB_COMMAND, cmd);
}
// Delay, time in us (micro second).
static void
delay(unsigned int time)
{
	unsigned int i;
	for( i =0; i <=time; i+=1.25)
		inb(0x84); // takes ~ 1.25us
}
static int
is_transmit_full() {
	return e100_card.tx_get == ((e100_card.tx_put + 1) % E100_TX_SLOTS);
}
static int
is_transmit_empty() {
	return e100_card.tx_get == e100_card.tx_put;
}
static void 
e100_start_cu() {
	outl(e100_card.io_base + E100_SCB_GENERAL_POINTER, PADDR(&e100_card.tx[e100_card.tx_get]));
	e100_send_cmd(E100_SCB_COMMAND_CU_START);
}

static void 
e100_start_ru() {
	outl(e100_card.io_base + E100_SCB_GENERAL_POINTER, PADDR(&e100_card.rx[0]));
	e100_send_cmd(E100_SCB_COMMAND_RU_START);
	e100_card.rx_suspended = 0;
	//} else { //not idle, should resume latest command;
//		delay(10);
//		e100_send_cmd(E100_SCB_COMMAND_RU_RESUME);
//	}
}

static void
e100_resume_ru() {
	// Check if there's any empty slots that can
	// be used.
	if( (e100_card.rx_last + 1) % E100_RX_SLOTS == e100_card.rx_get)
		return;
	
	if(e100_card.rx_get ==0)
		e100_card.rx_last = E100_RX_SLOTS - 1;
	else
		e100_card.rx_last = e100_card.rx_get - 1;
	
	e100_card.rx[e100_card.rx_last].status = 0;
	e100_card.rx[e100_card.rx_last].cmd = E100_RU_COMMAND_S;
	e100_card.rx[e100_card.rx_last].actual_size = 0;
	e100_card.rx[e100_card.rx_last].size = MAX_ETH_FRAME_SIZE;
	
	e100_send_cmd(E100_SCB_COMMAND_RU_RESUME);
	
	cprintf("E100 - RU is resumed\n");
}


static int
e100_is_rnr() {
	int16_t status;
	status = inw(e100_card.io_base + E100_SCB_STATUS);
	return (( status & E100_RFA_RNR) || (status & E100_RFA_SUSPENDED));
}

static int
e100_is_cna() {
	int16_t status;
	status = inw(e100_card.io_base + E100_SCB_STATUS);
	return (status & E100_SCB_CNA);
}


void
e100_handle_interrupt() {
	//cprintf("Receiving interrupt...\n");
	int status;
	//cprintf("Ana hon2\n");
	if(e100_is_cna()) {
		// Indicates that the packet is sent.
		e100_card.tx_get = (e100_card.tx_get+1) % E100_TX_SLOTS;
		cprintf("E100- Packet sent!\n");
		if(!is_transmit_empty()) 
			e100_start_cu();
		// clear the interrupt
		outb(e100_card.io_base + E100_SCB_STATUS + 1, (1 << 5));
	
	}
	if(e100_is_rnr()) {
		e100_card.rx_suspended = 1;
		e100_resume_ru();
		outb(e100_card.io_base + E100_SCB_STATUS + 1, (1 << 4));
	
	}
	outb(e100_card.io_base + E100_SCB_STATUS + 1, ( (1 << 15) | (1 << 14) ) >> 8);

	
	
	
}
int
e100_send_packet(void *buf, int size) {
	int slot;
	int is_empty = is_transmit_empty();
	cprintf("E100 - Send packet called with size: %d\n",size);
	 if(is_transmit_full()) {
		return -E_NO_MEM;
	} 

	if(size > MAX_ETH_FRAME_SIZE)
		return -E_SIZE_TOO_BIG;
	slot = e100_card.tx_put;
	e100_card.tx[slot].header.cmd =  E100_CB_COMMAND_TX  | E100_CB_COMMAND_S;
	e100_card.tx[slot].header.status = 0;
	e100_card.tx[slot].header.byte_count = size & E100_SIZE_MASK;
	e100_card.tx_put = (slot + 1) % E100_TX_SLOTS;
	memmove(e100_card.tx[slot].buff, buf, size);
	
	if(is_empty) 
		e100_start_cu();
	
	cprintf("E100 - Done sending packet!\n");
	return 0;
	
}

int
e100_receive_packet(void* buf) {
	int actual_size;
	
	if( (e100_card.rx[e100_card.rx_get].status & E100_RFA_STATUS_C) &&
	    (e100_card.rx[e100_card.rx_get].status & E100_RFA_STATUS_OK)) {
	    
	    actual_size = e100_card.rx[e100_card.rx_get].actual_size & E100_SIZE_MASK;
    	    memmove(buf, e100_card.rx[e100_card.rx_get].buf, actual_size);
	    
	    cprintf("E100- received packet\n");
	    
	    // Mark this slot as free. (reinitialize)
	    e100_card.rx[e100_card.rx_get].status = 0;
	    e100_card.rx[e100_card.rx_get].cmd = 0;
	    e100_card.rx[e100_card.rx_get].actual_size = 0;
	    e100_card.rx[e100_card.rx_get].size = MAX_ETH_FRAME_SIZE;
	    
	    e100_card.rx_get = (e100_card.rx_get +1) % E100_RX_SLOTS;
	    
	    //Check if the RU was suspended, and restart it
	    
	    if(e100_card.rx_suspended)
	    	e100_resume_ru();
	    return actual_size;
	    
    	}
    	return -E_TRY_AGAIN;
	
}

void
e100_init() {
	int i;
	
	// Initializing the CBLs
	for(i=0; i <  E100_TX_SLOTS; i++) {
		unsigned int next = (i + 1) % E100_TX_SLOTS;
		memset(&e100_card.tx[i], 0 , sizeof(struct cb_tx));
		e100_card.tx[i].header.link = PADDR(& ( e100_card.tx[next].header));
		e100_card.tx[i].header.tbd_array_addr = E100_NULL;
		e100_card.tx[i].header.thrs = E100_THRS;
		e100_card.tx[i].header.cmd = E100_CB_COMMAND_TX  | E100_CB_COMMAND_S;
	}
	
	cprintf("E100 - CBL initialization is completed.\n");

	//Initializing the RFAs
	for(i=0; i <  E100_RX_SLOTS; i++) {
		unsigned int next = (i + 1) % E100_RX_SLOTS;
		memset(&e100_card.rx[i], 0 , sizeof(struct cb_rx));
		e100_card.rx[i].link = PADDR(& ( e100_card.rx[next]));
		e100_card.rx[i].rbd_array_addr = E100_NULL;
		e100_card.rx[i].size = MAX_ETH_FRAME_SIZE;
		if(i==E100_RX_SLOTS-1)
			e100_card.rx[i].cmd = E100_RU_COMMAND_S;
	}
	cprintf("E100 - RFAs initialization is completed.\n");
	
	e100_card.tx_get = 0;
	e100_card.tx_put = 0;
	e100_card.rx_get = 0;
	e100_card.rx_last = 0;
	//Start the RU
	e100_start_ru();
	
	cprintf("E100 - RU started\n");
	
}

int 
e100_attach(struct pci_func *pcif) {
	// Enabling the card.
	
	pci_func_enable(pcif);

	// Saving information
	if(pcif->reg_base[1] > 0xffff)
		panic("initialize_e100: error IO base: %x",pcif->reg_base[1]);
	
	e100_card.io_base = pcif->reg_base[1];
	e100_card.irq_line = pcif->irq_line;
	e100_irq = pcif->irq_line;
	
	cprintf("E100 - I/O Base: 0x%08x, IRQ: %d\n",e100_card.io_base,e100_card.irq_line);
	
	// Reseting the card - software reset
	cprintf("E100 - Performing software reset\n");
	outl(e100_card.io_base + E100_CSR_PORT, E100_PORT_SOFTWARE_RESET);
	
	// Delay 10us after resetting.
	delay(10);
	
	e100_init();
	
	// Enabling interrupts
	irq_setmask_8259A(irq_mask_8259A & ~(1 << e100_irq));
	
	return 0;
}

void
e100_eeprom_clock(uint8_t cmd) {
	cmd |= E100_EEPROM_EESK;
	outb(e100_card.io_base + E100_EEPROM_CONTROL, cmd);
	delay(5);
	cmd &= ~ E100_EEPROM_EESK;
	outb(e100_card.io_base + E100_EEPROM_CONTROL, cmd);
	delay(5);
}

int
e100_eeprom_clock_n_read(uint8_t cmd) {
	uint8_t r =0;
	cmd |= E100_EEPROM_EESK;
	outb(e100_card.io_base + E100_EEPROM_CONTROL, cmd);
	delay(5);
	
	// Read the bit
	r = inb(e100_card.io_base + E100_EEPROM_CONTROL);
	
	cmd &= ~ E100_EEPROM_EESK;
	outb(e100_card.io_base + E100_EEPROM_CONTROL, cmd);
	delay(5);
	if( r & E100_EEPROM_EEDO)
		return 1;
	return 0;
}
	
int
e100_eeprom_read(uint8_t addr) {
	uint8_t cmd = 0;
	uint8_t r;
	uint16_t data = 0;
	int i;
	// need a delay of at least 4 us
	delay(5);
	
	// Enable the EEPROM by turning on the EECS
	cmd |= E100_EEPROM_EECS;
	outb(e100_card.io_base + E100_EEPROM_CONTROL, cmd);
	delay(5);
	
	// Give EEPROM a clock ...
	e100_eeprom_clock(cmd);
	
	// Tell EEPROM that we are doing a write operation
	cmd |= E100_EEPROM_EEDI;
	outb(e100_card.io_base + E100_EEPROM_CONTROL, cmd);
	delay(5);
	e100_eeprom_clock(cmd);
	e100_eeprom_clock(cmd);
	
	// Turn off EEDI
	cmd &= ~E100_EEPROM_EEDI;
	outb(e100_card.io_base + E100_EEPROM_CONTROL, cmd);
	delay(5);
	e100_eeprom_clock(cmd);
	
	// Now we write the address
	for(i=5; i>=0; i--) {
		if(addr & (1 << i))
			cmd |= E100_EEPROM_EEDI;
		else
			cmd &= ~E100_EEPROM_EEDI;
		outb(e100_card.io_base + E100_EEPROM_CONTROL, cmd);
		delay(5);
		e100_eeprom_clock(cmd);
	}
	// Reading the dummy "0" bit - EEDO
	r = inb(e100_card.io_base + E100_EEPROM_CONTROL);
	if( r & E100_EEPROM_EEDO) // error?
		panic("E100 - read eeprom, EEDO should be 0");
	
	// Reading the data from eeprom
	
	for(i=15; i>=0 ; i--) {
		r = e100_eeprom_clock_n_read(cmd);
		if( r != 0) // bit not 0
			data |= (1 << i);
	}
	
	// Finally, trun the EECS off
	cmd &= ~ E100_EEPROM_EECS;
	outb(e100_card.io_base + E100_EEPROM_CONTROL, cmd);
	delay(5);
	return data;
}

int
e100_get_mac_address(uint8_t hwaddr[]) {
	cprintf("E100 - Reading MAC Address\n");
	uint16_t addr0, addr1, addr2;
	addr0 = e100_eeprom_read(0);
	addr1 = e100_eeprom_read(1);
	addr2 = e100_eeprom_read(2);
	
	hwaddr[0] = addr0 & 0XFF;
        hwaddr[1] = addr0 >> 8;
        hwaddr[2] = addr1 & 0XFF;
        hwaddr[3] = addr1 >> 8;
        hwaddr[4] = addr2 & 0XFF;
        hwaddr[5] = addr2 >> 8;
        cprintf("E100 - Got Mac Address: %02x-%02x-%02x-%02x-%02x-%02x\n",
        	hwaddr[0],hwaddr[1],hwaddr[2],hwaddr[3],hwaddr[4],hwaddr[5]);
	return 0;
	
}


