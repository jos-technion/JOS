/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <kern/e100.h>

#include <kern/user.h>

static int sys_clear_block_access_bit(envid_t envid, void *va) {
	struct Env *env;
	pte_t *pte;
	envid2env(envid,&env,1);
	if(env == NULL)
		return -E_BAD_ENV;
	if((uintptr_t)va >= UTOP || PGOFF(va) != 0)
		return -E_INVAL;
	if(page_lookup(env->env_pgdir, va, &pte) == NULL)
		return -E_INVAL;
	*pte &= ~ PTE_A;
	return 0;
}
// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
//TODO
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.
	
	// LAB 3: Your code here.
	user_mem_assert(curenv, (void *)s, len, 0);  
	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.

	// LAB 4: Your code here.
	struct Env * new_env;
	int ret;
	ret = env_alloc(&new_env,curenv->env_id);
	//cprintf("sys_exofork ret: %d\n",ret);
	if(ret < 0 )
		return ret;
	new_env->env_status = ENV_NOT_RUNNABLE;
	new_env->env_tf = curenv->env_tf;
	new_env->env_tf.tf_regs.reg_eax = 0;
	new_env->user= curenv->user;
	
	//cprintf("New child is born!: %08x\n",new_env->env_id);


	
	return new_env->env_id;
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.

	// LAB 4: Your code here.
	
	struct Env* env;
	envid2env(envid,&env,1);
	if(env==NULL)
		return -E_BAD_ENV;
	//cprintf("2ereeeeeeeeeeeeeeeeeee %d, %d\n",env->env_status,status);
	switch (status) {
		case ENV_RUNNABLE:
		case ENV_NOT_RUNNABLE:
			break;
		default:
			return -E_INVAL;
	}
	
	env->env_status = status;
	return 0;
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3) with interrupts enabled.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 5: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
	//panic("sys_env_set_trapframe not implemented");
	struct Env* env;
	envid2env(envid,&env,1);
	if(env == NULL)
		return -E_BAD_ENV;
	user_mem_assert(env, tf, sizeof(struct Trapframe), PTE_U);

	
	tf->tf_ds = GD_UD | 3;
	tf->tf_es = GD_UD | 3;
	tf->tf_ss = GD_UD | 3;
	tf->tf_cs = GD_UT | 3;	
	
	tf->tf_eflags |= FL_IF;
	env->env_tf=*tf;	
	return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.
	struct Env* env;
	envid2env(envid,&env,1);
	if(env==NULL)
		return -E_BAD_ENV;
	env->env_pgfault_upcall = func;
	return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_USER in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!

	// LAB 4: Your code here.
	//TODO: check if user has permission to change envid
	struct Page *new_page;
	struct Env* env;
	envid2env(envid,&env,1);
	if(env==NULL)
		return -E_BAD_ENV;
	if(va >= (void *)UTOP || PGOFF(va) != 0)
		return -E_INVAL;
	 if((perm & PTE_U) == 0 ||
                (perm & PTE_P) == 0 ||
                (perm & ~PTE_U & ~PTE_P & ~PTE_AVAIL & ~PTE_W) != 0){
                return -E_INVAL;
        }

	page_alloc(&new_page);
	if(new_page==NULL)
		return -E_NO_MEM;
	if(page_insert(env->env_pgdir, new_page, va, perm) < 0) {
		page_free(new_page);
		return -E_NO_MEM;
	}
	memset(page2kva(new_page), 0, PGSIZE);	
	return 0;
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.

	// LAB 4: Your code here.
	struct Env *source_env, *dest_env;
	struct Page* page;
	pte_t *ppte;
	int ret;
	
	envid2env(srcenvid,&source_env,1);
        envid2env(dstenvid,&dest_env,1);
       	if(source_env == NULL || dest_env == NULL)
       		return -E_BAD_ENV;
	if(srcva >= (void *)UTOP || PGOFF(srcva) != 0)
		return -E_INVAL;
	if(dstva >= (void *)UTOP || PGOFF(dstva) != 0)
		return -E_INVAL;
	if(((page =  page_lookup(source_env->env_pgdir, srcva, &ppte)) == NULL)
		|| (*ppte | PTE_P) == 0){
		return -E_INVAL;
        }  
        if((perm & PTE_U) == 0 ||
                (perm & PTE_P) == 0 ||
                (perm & ~PTE_U & ~PTE_P & ~PTE_AVAIL & ~PTE_W) != 0){
                return -E_INVAL;
        }

	if((ret = page_insert(dest_env->env_pgdir, page, dstva, perm)) < 0){
                return ret;
        } 
        return 0;

}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().

	// LAB 4: Your code here.
	struct Env *e;
        int ret;
        //cprintf("Calling unmap...\n");
        envid2env(envid,&e,1);
        if(e==NULL) {
        	//cprintf("no such env\n");
                return -E_BAD_ENV;
        }
        if( va >= (void *)UTOP || PGOFF(va) != 0) {
        	//cprintf("va isn't alligned\n");
                return -E_INVAL;
        }
        page_remove(e->env_pgdir,va);   
        return 0;
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
	//panic("sys_ipc_try_send not implemented");
	struct Env *e;
       // int ret;
        struct Page *p;
	//cprintf("%08x is Sending data to: %08x Value: %08x\n",curenv->env_id,envid,value);
        //cprintf("Calling send ipc...\n");
        envid2env(envid,&e,0);
        if(e==NULL) 
        	return -E_BAD_ENV;
	//cprintf("Calling send ipc 2...\n");
        if(e->env_ipc_recving == 0)
                return -E_IPC_NOT_RECV;
        //cprintf("Calling send ipc 3...\n");
        if(srcva < (void *)UTOP) {
        	if(PGOFF(srcva) != 0)
        		return -E_INVAL;
		//cprintf("Calling send ipc 4...\n");
		if((perm & PTE_U) == 0 ||
                (perm & PTE_P) == 0 ||
                (perm & ~PTE_U & ~PTE_P & ~PTE_AVAIL & ~PTE_W) != 0){
                
                	return -E_INVAL;
        	}
       		//cprintf("Calling send ipc 5..\n");
		if((p=page_lookup(curenv->env_pgdir, srcva, NULL)) == NULL)
			return -E_INVAL;
		//if((perm & PTE_W) && !(*p & PTE_W))
		//	return -E_INVAL;
		//cprintf("Calling send ipc 6...\n");
		e->env_ipc_perm = perm;
		if((page_insert(e->env_pgdir, p, e->env_ipc_dstva, perm)) < 0)
			return -E_NO_MEM;
                

	} else {
		e->env_ipc_perm = 0;
	}
	//cprintf("Calling send ipc 7...\n");
	e->env_ipc_recving = 0;
	e->env_ipc_from = curenv->env_id;
	e->env_ipc_value = value;
	e->env_status = ENV_RUNNABLE;
	return 0;
	
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.
	//panic("sys_ipc_recv not implemented");
	if(dstva < (void *) UTOP && PGOFF(dstva) != 0)
		return -E_INVAL;
	curenv->env_ipc_recving = 1;
	curenv->env_ipc_dstva = dstva;
	curenv->env_status = ENV_NOT_RUNNABLE;
	curenv->env_tf.tf_regs.reg_eax = 0;
	sched_yield();
	return 0;
}

// Return the current time.
static int
sys_time_msec(void) 
{
	return time_msec();
}

// Send packet
static int
sys_send_packet(void *va, unsigned int size) {
	int perm;
	int offset;
	int r;
	struct Page *p;
	
	perm = PTE_U;
	if(va + size >= (void *)UTOP) {
		cprintf("SYS_send_packet: above UTOP\n");
		return -E_INVAL;
	}
	if((r = user_mem_check(curenv, va, size, perm)) <0) {
		cprintf("SYS_send_packet: %08x\n",r);
		return r;
	}
	if(size > MAX_ETH_FRAME_SIZE)  {
		//should fix the size ...
		cprintf("SYS_send_packet: size is too big!");
		return -E_INVAL; //better return?
	}
	//cprintf("Transmitting.......\n\n\n\n");
	return e100_send_packet(va, size);	
}
static int
sys_receive_packet(void *va) {
	int perm;
	int offset;
	int r;
	struct Page *p;
	
	perm = PTE_U | PTE_W ;
	//cprintf("Checking memory...\n");
	//if((r = user_mem_check(curenv, va, 5, perm)) <0) {
	//	return r;
	//}
	//cprintf("Done checking memory...\n");
	if((r = e100_receive_packet(va)) < 0) {
		//cprintf("fee Error: %x\n",r);
		return r;
	}
	return r;
}
int
sys_get_mac_address(uint8_t hwaddr[]) {
	return e100_get_mac_address(hwaddr);
}
int
sys_get_logged_user_name(char* buf) {
	int r;
	if(curenv->user == -1)
		return -1;
	if((r = user_mem_check(curenv, buf, MAX_USER_LENGTH, PTE_U|PTE_W)) <0) {
		cprintf("SYS_get_logged_user_name: %08x\n",r);
		return r;
	}
	
	memmove(buf,users[curenv->user].user,MAX_USER_LENGTH);
	return 0;
}

int
sys_get_current_path(char* buf) {
	int r;
	if(curenv->user == -1)
		return -1;
	if((r = user_mem_check(curenv, buf, MAX_PATH_LENGTH, PTE_U|PTE_W)) <0) {
		cprintf("SYS_get_logged_user_name: %08x\n",r);
		return r;
	}
	
	memmove(buf,users[curenv->user].path,MAX_PATH_LENGTH);
	return 0;
}

int
sys_update_current_path(char* buf) {
	int r;
	if(curenv->user == -1)
		return -1;
	if((r = user_mem_check(curenv, buf, MAX_PATH_LENGTH, PTE_U)) <0) {
		cprintf("SYS_get_logged_user_name: %08x\n",r);
		return r;
	}
	
	memmove(users[curenv->user].path,buf,MAX_PATH_LENGTH);
	return 0;
}

int
sys_get_home_dir(char* buf) {
	int r;
	if(curenv->user == -1)
		return -1;
	if((r = user_mem_check(curenv, buf, MAX_PATH_LENGTH, PTE_U|PTE_W)) <0) {
		cprintf("SYS_get_logged_user_name: %08x\n",r);
		return r;
	}
	memmove(buf,"/home/stam",11);
	return 0;
}

// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.
	switch (syscallno) {
		case SYS_cputs :
			sys_cputs((const char*) a1, (size_t) a2);
			return 0;
		case SYS_env_set_trapframe:
			return sys_env_set_trapframe((envid_t)a1, (struct Trapframe*) a2);
		case SYS_cgetc :
			return sys_cgetc();
		case SYS_getenvid:
			return sys_getenvid();
		case SYS_env_destroy:
			return sys_env_destroy((envid_t) a1);
		case SYS_yield:
			sys_yield();
			return 0;
 		case SYS_exofork:
                	return sys_exofork();
		case SYS_env_set_status:
		        return sys_env_set_status((envid_t)a1,(int)a2);
		case SYS_page_alloc:
		        return sys_page_alloc((envid_t)a1, (void *)a2, (int)a3);
		case SYS_page_map:
		        return sys_page_map((envid_t)a1, (void *)a2, (envid_t)a3, (void *)a4, (int)a5);
		case SYS_page_unmap:
                	return sys_page_unmap((envid_t)a1, (void *)a2);
		case SYS_env_set_pgfault_upcall:
                	return sys_env_set_pgfault_upcall((envid_t)a1, (void *)a2);
		case SYS_ipc_try_send:
			//cprintf("Sending ipc..\n\n\n");
                	return sys_ipc_try_send((envid_t)a1, (uint32_t)a2, (void *)a3, (unsigned)a4);
        	case SYS_ipc_recv:
                	return sys_ipc_recv((void *)a1);
        	case SYS_clear_block_access_bit:
        		return sys_clear_block_access_bit((envid_t)a1, (void*) a2);
        	case SYS_time_msec:
        		return sys_time_msec();
		case SYS_send_packet:
			return sys_send_packet((void *)a1, (unsigned)a2);
		case SYS_receive_packet:
			return sys_receive_packet((void *)a1);
		case SYS_get_mac_address:
			return sys_get_mac_address((uint8_t *) a1);
		case SYS_get_logged_user_name:
			return sys_get_logged_user_name((char *)a1);
		case SYS_get_current_path:
			return sys_get_current_path((char *)a1);
		case SYS_update_current_path:
			return sys_update_current_path((char *)a1);
		case SYS_get_home_dir:
			return sys_get_home_dir((char *)a1);
		default:
			return -E_INVAL;
	}
		
	
	//panic("syscall not implemented");
}

