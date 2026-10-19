// SPDX-License-Identifier: GPL-2.0-only
#include <asm/cpu_host.h>
#include <asm/host_ops.h>
#include <asm/sched.h>
#include <asm/syscalls.h>
#include <linux/sched/stat.h>

static struct lkl_sem *shutdown_sem;
static bool has_shutdown;

void lkl_cpu_change_owner(lkl_thread_t owner)
{
	lkl_ops->cpu_change_owner(owner);
}

int lkl_cpu_get(void)
{
	return lkl_ops->cpu_get();
}

static int pending_events;

void lkl_cpu_notify(int event)
{
	BUG_ON(event & ~LKL_CPU_ALL_EVENTS);

	pending_events |= event;
}

int lkl_cpu_check(void)
{
	int flags;

	/* no smp for now.. */
	flags = pending_events;
	pending_events = 0;

	/* can we jump to another task? */
	if (test_ti_thread_flag(current_thread_info(), TIF_HOST_THREAD) &&
	    !single_task_running())
		flags |= LKL_CPU_FLAG_JMP;

	return flags;
}

void lkl_cpu_jump(void)
{
	if (in_interrupt())
		lkl_bug("%s: in interrupt\n", __func__);
	thread_sched_jb();
}

void lkl_cpu_put(void)
{
	int flags;
	int ret;

	for (;;) {
		flags = lkl_cpu_check();

		/* can we handle irqs? */
		if (!irqs_disabled())
			flags |= LKL_CPU_FLAG_IRQ;

		ret = lkl_ops->cpu_put(flags);

		if (ret == LKL_CPU_FLAG_IRQ)
			run_irqs();
		else
			break;
	}

	if (ret == LKL_CPU_FLAG_JMP)
		lkl_cpu_jump();
	else if (ret)
		lkl_bug("%s: bad ret %d\n", __func__, ret);
}

int lkl_cpu_try_run_irq(int irq)
{
	BUG();
}

void lkl_cpu_shutdown(void)
{
	has_shutdown = true;
	lkl_ops->cpu_shutdown();
}

void lkl_cpu_wait_shutdown(void)
{
	lkl_ops->sem_down(shutdown_sem);
	lkl_ops->sem_free(shutdown_sem);
}

void arch_cpu_idle(void)
{
	if (has_shutdown)
		lkl_ops->thread_exit();

	/* enable irqs now to allow direct irqs to run */
	local_irq_enable();

	/* switch to idle_host_task */
	wakeup_idle_host_task();
}

int lkl_cpu_init(void)
{
	shutdown_sem = lkl_ops->sem_alloc(0);
	if (!shutdown_sem)
		return -ENOMEM;

	return 0;
}
