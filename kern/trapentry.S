/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* The TRAPHANDLER macro defines a globally-visible function for handling
 * a trap.  It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 */ 
#define TRAPHANDLER(name, num)						\
.text;									\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps;							\
.data;									\
	.long num;							\
	.long name

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
.text;									\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps;							\
.data;									\
	.long num;							\
	.long name


.data
.globl idttbl
idttbl:	.long	0xDEADBEEF


/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */

TRAPHANDLER_NOEC(t_divide_hndl,T_DIVIDE)
TRAPHANDLER_NOEC(t_debug_hndl,T_DEBUG)
TRAPHANDLER_NOEC(t_nmi_hndl,T_NMI)
TRAPHANDLER_NOEC(t_brkpt_hndl,T_BRKPT)
TRAPHANDLER_NOEC(t_oflow_hndl,T_OFLOW)
TRAPHANDLER_NOEC(t_bound_hndl,T_BOUND)
TRAPHANDLER_NOEC(t_illop_hndl,T_ILLOP)
TRAPHANDLER_NOEC(t_device_hndl,T_DEVICE)
TRAPHANDLER(t_dblflt_hndl,T_DBLFLT)
TRAPHANDLER(t_tss_hndl,T_TSS)
TRAPHANDLER(t_segnp_hndl,T_SEGNP)
TRAPHANDLER(t_stack_hndl,T_STACK)
TRAPHANDLER(t_gpflt_hndl,T_GPFLT)
TRAPHANDLER(t_pgflt_hndl,T_PGFLT)
TRAPHANDLER_NOEC(t_fperr_hndl,T_FPERR)
TRAPHANDLER(t_align_hndl,T_ALIGN)
TRAPHANDLER_NOEC(t_mchk_hndl,T_MCHK)
TRAPHANDLER_NOEC(t_simderr_hndl,T_SIMDERR)
TRAPHANDLER_NOEC(t_syscall_hndl,T_SYSCALL)
// handle IRQ
TRAPHANDLER_NOEC(t_irq_timer_hndl,IRQ_TIMER+IRQ_OFFSET)
TRAPHANDLER_NOEC(t_irq_kbd_hndl,IRQ_KBD+IRQ_OFFSET)
TRAPHANDLER_NOEC(t_irq_serial_hndl,IRQ_SERIAL+IRQ_OFFSET)
TRAPHANDLER_NOEC(t_irq_spurious_hndl,IRQ_SPURIOUS+IRQ_OFFSET)
TRAPHANDLER_NOEC(t_irq_ide_hndl,IRQ_IDE+IRQ_OFFSET)
TRAPHANDLER_NOEC(t_irq_error_hndl,IRQ_ERROR+IRQ_OFFSET)
TRAPHANDLER_NOEC(t_irq_11,11+IRQ_OFFSET)

.data
	.long 0x0
	.long 0x0

/*
 * Lab 3: Your code here for _alltraps
 */
	
.text
	.globl _alltraps;
	.align 2
 //TODO: what about disabling interrupts?
 _alltraps:
	pushl %ds
	pushl %es
	pushal
	movl $GD_KD, %eax
	movw %ax, %ds
	movw %ax, %es
	pushl %esp
	call trap
	pop %esp
	popal
	popl %es
	popl %ds
	iret

