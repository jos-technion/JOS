// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>


// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at vpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	pte_t pte;
	
	pte = ((pte_t *)vpt)[PPN(addr)];
	if(!((err & FEC_WR) != 0 && (pte & PTE_COW) != 0)) {
		panic("page fault, not cow");
	}
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.
	sys_page_alloc(0,PFTEMP, PTE_P | PTE_U | PTE_W);
	memmove(PFTEMP,ROUNDDOWN(addr, PGSIZE), PGSIZE);
	sys_page_map(0,PFTEMP, 0, ROUNDDOWN(addr, PGSIZE), PTE_P|PTE_U|PTE_W);
	sys_page_unmap(0, PFTEMP);
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
// 
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	int ret;
	pte_t p;
	
	p = ((pte_t *)vpt)[pn];
	if((p & PTE_P) == 0)
		panic("error in duplicate, not present");
	if((p & PTE_U) == 0)
		panic("error in duplicate, not user");
	// Checking the PTE_SHARE  - Lab 7
	if( p & PTE_SHARE) {
		if ((ret = sys_page_map(0, (void *) (pn * PGSIZE), envid, (void *) (pn * PGSIZE), p & PTE_USER)) < 0)
			return ret;
			
	} else {
		if((p & PTE_W) ==0 && (p & PTE_COW) ==0) {
			if((ret = sys_page_map(0, (void *) (pn * PGSIZE), envid, (void *) (pn * PGSIZE), PTE_P|PTE_U)) < 0)
				return ret;
	
		} else {
			if((ret = sys_page_map(0, (void *) (pn * PGSIZE), envid, (void*)(pn * PGSIZE), PTE_P|PTE_U|PTE_COW)) <0)
				return ret;
			if((ret = sys_page_map(0, (void *) (pn * PGSIZE), 0, (void*)(pn * PGSIZE), PTE_P|PTE_U|PTE_COW)) <0)
				return ret;
		}
	}
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use vpd, vpt, and duppage.
//   Remember to fix "env" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	int envid=0;
	int i,j,ret;
	extern void _pgfault_upcall(void);
	
	set_pgfault_handler(pgfault);
	envid=sys_exofork();
	if(envid < 0)
		panic("sys_exofork in fork: %e",envid);
	if(envid==0) { //child code
		//fix the env
		//cprintf("Child code: %08x\n",sys_getenvid());
		env = envs+ ENVX(sys_getenvid());
		return 0;
	}
	//Parent code, do all the dup/copy
	//cprintf("Parent code: %08x\n",envid);
	for(i=0; i * PTSIZE < UTOP; i++) {
		//cprintf("Parent code 1: %08x\n",envid);
		if(vpd[i] & PTE_P ) {
		 	//cprintf("Parent code 2: %08x\n",envid);
			for(j=0; i*PTSIZE + j*PGSIZE < UTOP && j < NPTENTRIES; j++) {
                               uintptr_t addr = i*PTSIZE + j*PGSIZE;
                               if(addr == UXSTACKTOP-PGSIZE){
                                        continue;
                                }
                                     
                                if((vpt[VPN(addr)] & PTE_P) && (vpt[VPN(addr)] & PTE_U) ) { // check if the page is present and for user
                                        //cprintf("Duplicating page...\n");
                                        duppage(envid, VPN(addr)); 
                                }
                       }
                }
        }
        //allocating exception stack page
        //cprintf("fork envid: %08x\n",envid);
        if((ret = sys_page_alloc(envid, (void *)UXSTACKTOP-PGSIZE, PTE_P|PTE_U|PTE_W)) <0 )
        	panic("sys_page_alloc in fork: %e",ret);
	
	sys_env_set_pgfault_upcall(envid, _pgfault_upcall);
	if((ret = sys_env_set_status(envid, ENV_RUNNABLE)) <0 )
		panic("sys_env_set_status in fork: %e", ret);
	struct Env *e;

	
	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
