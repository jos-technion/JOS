// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display back trace information", mon_backtrace},
	{ "alloc_page", "Allocate a new page", mon_alloc_page},
	{ "free_page", "Frees the pages with given addresses", mon_free_page},
	{ "page_status", "Display if the page is free or allocated", mon_page_status},
	{ "page_ref", "Display number of references to a page", mon_page_ref},
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start %08x (virt)  %08x (phys)\n", _start, _start - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		(end-_start+1023)/1024);
	return 0;
}
int
mon_backtrace2(int argc, char **argv, struct Trapframe *tf)
{
 // Your code here.
 unsigned int ebp;
   unsigned int eip;
   unsigned int args[5];
   unsigned int i;

   ebp = read_ebp();
   cprintf("Stack backtrace:\n");
   do {
     eip = *((unsigned int*)(ebp + 4));
      for(i=0; i<5; i++)
         args[i] = *((unsigned int*) (ebp + 8 + 4*i));

      cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n",
       ebp, eip, args[0], args[1], args[2], args[3], args[4]);


     ebp = *((unsigned int *)ebp);
   } while(ebp != 0);


 return 0;
}


int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	int* ebp;
	struct Eipdebuginfo info;
	unsigned int eip;
	unsigned int i;
	ebp=(int*)read_ebp();
	cprintf("Stack backtrace:\n");
	while(1) {
		eip = (unsigned int)*(ebp+1);
		debuginfo_eip(eip,&info);
		cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n",ebp,eip,*(ebp+2),*(ebp+3),*(ebp+4),*(ebp+5));
		cprintf("\t%s:%d: %.*s+%d\n", info.eip_file,
						info.eip_line,
						info.eip_fn_namelen,
						info.eip_fn_name,
						eip-info.eip_fn_addr);
		ebp=(int*)*ebp;
		if(ebp==0) break;
	}
	return 0;
}





/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
int 
mon_alloc_page (int argc, char **argv, struct Trapframe *tf) 
{
	struct Page* page;
	if(page_alloc(&page) != 0) {
		cprintf("\tAllocating a new page failed. no more memory.\n");
		return 0;
	}
	cprintf("\tnew page allocated: %p\n",page2pa(page));
	return 0;
}
int
mon_free_page (int argc, char **argv, struct Trapframe *tf)
{
	int i;
	for(i=1;i<argc;i++) {
		uint32_t a = strtol(argv[i],NULL,16);
		if(a==0) {
			cprintf("\%s, isn't a valid page number\n",argv[i]);
			continue;
		}
		if(!is_page_allocated(pa2page(a))) {
			cprintf("\tPage: %p, is already free!\n",a);
			continue;
		}
		page_free(pa2page(a));	
		cprintf("\tPage: %p, freed!\n",a);
	}
	return 0;
}
int
mon_page_status (int argc, char **argv, struct Trapframe *tf)
{
	int i;
	for(i=1;i<argc;i++) {
		uint32_t a = strtol(argv[i],NULL,16);
		if(a==0) {
			cprintf("\%s, isn't a valid page number\n",argv[i]);
			continue;
		}
		if(!is_page_allocated(pa2page(a))) {
			cprintf("\tPage: %p, is free!\n",a);
			continue;
		}
		cprintf("\tPage: %p, is allocated!\n",a);
	}
	return 0;
}	
int mon_page_ref (int argc, char **argv, struct Trapframe *tf)
{
	int i;
	for(i=1;i<argc;i++) {
		uint32_t a = strtol(argv[i],NULL,16);
		if(a==0) {
			cprintf("\%s, isn't a valid page number\n",argv[i]);
			continue;
		}
		if(!is_page_allocated(pa2page(a))) {
			cprintf("\tPage: %p, number of references: 0\n",a);
			continue;
		}
		cprintf("\tPage: %p, number of references: %d\n",a,pa2page(a)->pp_ref);
	}
	return 0;
}
// return EIP of caller.
// does not work if inlined.
// putting at the end of the file seems to prevent inlining.
unsigned
read_eip()
{
	uint32_t callerpc;
	__asm __volatile("movl 4(%%ebp), %0" : "=r" (callerpc));
	return callerpc;
}
