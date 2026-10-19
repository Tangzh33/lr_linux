#ifndef _ASM_UAPI_LKL_CPU_HOST_H
#define _ASM_UAPI_LKL_CPU_HOST_H

enum {
	LKL_CPU_FLAG_IRQ	= 1,
	LKL_CPU_FLAG_JMP	= 2,

	// Events
	LKL_CPU_EVENT_NETWORK	= 4,
	LKL_CPU_EVENT_BLOCK	= 8,
};

#define LKL_CPU_ALL_EVENTS (LKL_CPU_EVENT_NETWORK | LKL_CPU_EVENT_BLOCK)

int lkl_cpu_check(void);
void lkl_cpu_jump(void);

#endif
