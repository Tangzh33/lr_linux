/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_IRQFLAGS_H
#define _ASM_LKL_IRQFLAGS_H

static inline unsigned long arch_local_save_flags(void)
{
	extern bool irqs_enabled;

	return irqs_enabled;
}
#define arch_local_save_flags arch_local_save_flags

static inline void arch_local_irq_restore(unsigned long flags)
{
	extern bool irqs_enabled;
	extern unsigned long irq_index_status;
	extern void on_irqs_enable(void);

	if (flags && !irqs_enabled && irq_index_status)
		on_irqs_enable();
	irqs_enabled = flags;
}
#define arch_local_irq_restore arch_local_irq_restore

#include <asm-generic/irqflags.h>

#endif
