/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_BARRIER_H
#define _ASM_LKL_BARRIER_H

/*
 * Force strict CPU ordering.
 * And yes, this is required on UP too when we're talking
 * to devices.
 */
#ifdef __x86_64__

#define __mb()          asm volatile("mfence" ::: "memory")
#define __rmb()         asm volatile("lfence" ::: "memory")
#define __wmb()         asm volatile("sfence" ::: "memory")

#define __smp_mb()      asm volatile("lock; addl $0, (%%rsp)" ::: "memory", "cc")
#define __smp_rmb()     barrier()
#define __smp_wmb()     barrier()

#define __dma_rmb()     barrier()
#define __dma_wmb()     barrier()

#else /* __x86_64__ */

#warning "Memory barriers are not implemented for this host architecture."
#warning "Devices/drivers may miss events due to unimplemented memory barriers."

#endif /* __x86_64__ */

#include <asm-generic/barrier.h>

#endif
