/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_ATOMIC_H
#define _ASM_LKL_ATOMIC_H

#ifdef CONFIG_SMP
#define LOCK_PREFIX "lock "
#else
#define LOCK_PREFIX ""
#endif

#include <asm/asm_x86.h>
#include <asm/atomic_x86.h>

#endif
