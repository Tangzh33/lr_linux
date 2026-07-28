// SPDX-License-Identifier: GPL-2.0

#include <asm/cpu_host.h>
#include <asm/host_ops.h>
#include <asm/irq.h>
#include <asm/syscalls.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

#define BUG(msg)              \
	({                    \
		lkl_bug(msg); \
		for (;;)      \
			;     \
	})

enum {
	ASTER_RESERVED,
	/* UI */
	ASTER_PRINT,
	/* SEM */
	ASTER_SEM_ALLOC,
	ASTER_SEM_UP,
	ASTER_SEM_DOWN,
	/* CPU */
	ASTER_CPU_SET_OWNER,
	ASTER_CPU_GET,
	ASTER_CPU_PUT,
	ASTER_CPU_SHUTDOWN,
	/* THD */
	ASTER_THREAD_CREATE,
	ASTER_THREAD_SELF,
	/* TIME */
	ASTER_TIME,
	ASTER_TIMER_ONESHOT,
	/* VM */
	ASTER_MMIO_MAP,
	ASTER_VM_MAP,
	ASTER_VM_UNMAP,
	ASTER_PIN_PAGES,
	ASTER_VMA_INSERT,
	ASTER_VMA_UNMAP,
	/* FILE */
	ASTER_FW_READ,
	ASTER_FD_MAP,
	/* PCI */
	ASTER_PCI_CONF_READ,
	ASTER_PCI_CONF_WRITE,
	ASTER_PCI_BAR_ALLOC,
	ASTER_PCI_IRQ_INIT,
	ASTER_PCI_GET_MSI,
	ASTER_PCI_PUT_MSI,
	ASTER_NLMSG_READY,
};

extern long aster_call(long arg0, long arg1, long arg2, long type);

static void aster_print(const char *s, int len)
{
	aster_call((long)s, (long)len, 0, ASTER_PRINT);
}

static void aster_panic(void)
{
	aster_print("lkl panicked\n", 13);

	for (;;)
		;
}

static struct lkl_sem *aster_sem_alloc(int count)
{
	return (struct lkl_sem *)aster_call((long)count, 0, 0, ASTER_SEM_ALLOC);
}

static void aster_sem_free(struct lkl_sem *sem)
{
	/* no op, not supported */
	(void)sem;
}

static void aster_sem_up(struct lkl_sem *sem)
{
	aster_call((long)sem, 0, 0, ASTER_SEM_UP);
}

static void aster_sem_down(struct lkl_sem *sem)
{
	aster_call((long)sem, 0, 0, ASTER_SEM_DOWN);
}

static void aster_cpu_set_owner(lkl_thread_t owner)
{
	aster_call(owner, 0, 0, ASTER_CPU_SET_OWNER);
}

static int aster_cpu_get(void)
{
	return aster_call(0, 0, 0, ASTER_CPU_GET);
}

static int aster_cpu_put(int flags)
{
	return aster_call(flags, 0, 0, ASTER_CPU_PUT);
}

static void aster_cpu_shutdown(void)
{
	aster_call(0, 0, 0, ASTER_CPU_SHUTDOWN);
}

static lkl_thread_t aster_thread_create(void (*f)(void *), void *arg)
{
	return (lkl_thread_t)aster_call((long)f, (long)arg, 0,
					ASTER_THREAD_CREATE);
}

static void aster_thread_detach(void)
{
	/* no op */
}

struct thread_unwind_t {
	lkl_thread_t tid;
	long jmpbuf[5];
};

#define MAX_THREADS 100
static struct thread_unwind_t unwind_array[MAX_THREADS];

static struct thread_unwind_t *unwind_alloc(lkl_thread_t tid)
{
	int i;
	lkl_thread_t zero_tid = 0;

	if (!tid)
		BUG("zero tid");

	for (i = 0; i < MAX_THREADS; ++i) {
		lkl_thread_t *target_tid = &unwind_array[i].tid;

		if (__atomic_load_n(target_tid, __ATOMIC_RELAXED) != 0)
			continue;

		if (!__atomic_compare_exchange_n(target_tid, &zero_tid, tid, 0,
						 __ATOMIC_RELAXED,
						 __ATOMIC_RELAXED))
			continue;

		return &unwind_array[i];
	}

	BUG("too many threads");
}

static void unwind_free(struct thread_unwind_t *unwind)
{
	__atomic_store_n(&unwind->tid, 0, __ATOMIC_RELAXED);
}

static struct thread_unwind_t *unwind_find(lkl_thread_t tid)
{
	int i;

	if (!tid)
		BUG("zero tid");

	for (i = 0; i < MAX_THREADS; ++i) {
		if (__atomic_load_n(&unwind_array[i].tid, __ATOMIC_RELAXED) !=
		    tid)
			continue;

		return &unwind_array[i];
	}

	BUG("no such thread");
}

static lkl_thread_t aster_thread_self(void);

static void aster_thread_exit(void)
{
	lkl_thread_t self = aster_thread_self();
	struct thread_unwind_t *unwind = unwind_find(self);

	__builtin_longjmp(unwind->jmpbuf, 1);
}

static int aster_thread_join(lkl_thread_t tid)
{
	(void)tid;

	return 0;
}

static lkl_thread_t aster_thread_self(void)
{
	return (lkl_thread_t)aster_call(0, 0, 0, ASTER_THREAD_SELF);
}

static int aster_thread_equal(lkl_thread_t a, lkl_thread_t b)
{
	return a == b;
}

static char *__mem_start;
static char *__mem_end;

static char *mem_head;

static void *aster_mem_alloc(unsigned long size)
{
	char *slot;

	if (size > (unsigned long)(__mem_end - __mem_start))
		return NULL;

	if (__atomic_load_n(&mem_head, __ATOMIC_RELAXED) >= __mem_end)
		return NULL;

	slot = __atomic_fetch_add(&mem_head, size, __ATOMIC_RELAXED);
	if (slot >= __mem_end)
		return NULL;

	return slot;
}

static void aster_mem_free(void *addr)
{
	/* no op, not supported */
	(void)addr;
}

static unsigned long long aster_time(void)
{
#ifdef __x86_64__
	extern unsigned long long tsc_frequency;

	unsigned int edx, eax;
	unsigned long long tsc, sec, nsec;

	asm volatile("rdtsc" : "=d"(edx), "=a"(eax));

	tsc = ((unsigned long long)edx << 32) + eax;

	sec = tsc / tsc_frequency;
	nsec = (tsc % tsc_frequency) * 1000000000 / tsc_frequency;

	return sec * 1000000000 + nsec;
#else
	return (unsigned long long)aster_call(0, 0, 0, ASTER_TIME);
#endif
}

static void *aster_timer_alloc(void (*fn)(void))
{
	static int count = 0;

	if (__atomic_fetch_add(&count, 1, __ATOMIC_RELAXED) != 0)
		BUG("too many timers");

	/* FIXME: check fn */
	(void)fn;

	return (void *)1;
}

static int aster_timer_set_oneshot(void *timer, unsigned long delta)
{
	(void)timer;

	aster_call(delta, 0, 0, ASTER_TIMER_ONESHOT);

	return 0;
}

static void aster_timer_free(void *timer)
{
	/* no op */
	(void)timer;
}

static void *aster_mmio_map(long addr, int size)
{
	return (void *)aster_call(addr, size, 0, ASTER_MMIO_MAP);
}

static int aster_mmio_access(const volatile void *addr, void *val, int size,
			     int write)
{
	switch (size) {
	case 1:
		if (write)
			*(volatile char *)addr = *(char *)val;
		else
			*(char *)val = *(volatile char *)addr;
		break;
	case 2:
		if (write)
			*(volatile short *)addr = *(short *)val;
		else
			*(short *)val = *(volatile short *)addr;
		break;
	case 4:
		if (write)
			*(volatile int *)addr = *(int *)val;
		else
			*(int *)val = *(volatile int *)addr;
		break;
	case 8:
		if (write)
			*(volatile long *)addr = *(long *)val;
		else
			*(long *)val = *(volatile long *)addr;
		break;
	default:
		BUG("invalid mmio");
	}

	return 0;
}

static void *aster_vm_map(const unsigned long *paddrs, unsigned int count,
			  unsigned long pgprot)
{
	return (void *)(*aster_call)((long)paddrs, count, pgprot, ASTER_VM_MAP);
}

static void aster_vm_unmap(const void *vaddr)
{
	(*aster_call)((long)vaddr, 0, 0, ASTER_VM_UNMAP);
}

static int aster_pin_pages(unsigned long vaddr, const unsigned long *paddrs,
			   unsigned int count)
{
	return (*aster_call)(vaddr, (long)paddrs, count, ASTER_PIN_PAGES);
}

static int aster_vma_insert(unsigned long vaddr, unsigned long paddr_prot,
			    unsigned long size)
{
	return (*aster_call)(vaddr, paddr_prot, size, ASTER_VMA_INSERT);
}

static void aster_vma_unmap(const void *address_space, unsigned long offset,
			    unsigned long size)
{
	(*aster_call)((long)address_space, offset, size, ASTER_VMA_UNMAP);
}

static long aster_fw_read(const char *path, unsigned long path_size, void *buf,
			  unsigned long buf_size)
{
	unsigned long long path_and_buf_size;

	if (path_size > ~(unsigned int)0)
		BUG("path too long");
	if (buf_size > ~(unsigned int)0)
		BUG("buf too long");

	path_and_buf_size = path_size | (((unsigned long long)buf_size) << 32);
	return (*aster_call)((long)path, (long)buf, path_and_buf_size,
			     ASTER_FW_READ);
}

static int aster_fd_map(int fd)
{
	return (*aster_call)(fd, 0, 0, ASTER_FD_MAP);
}

static void aster_jmpbuf_set(struct lkl_jmp_buf *jmpb, void (*f)(void))
{
	if (__builtin_setjmp((long *)jmpb) == 0)
		(*f)();
}

static void aster_jmpbuf_jump(struct lkl_jmp_buf *jmpb, int val)
{
	if (val != 1)
		BUG("illegal longjmp");

	__builtin_longjmp((long *)jmpb, val);
}

static struct lkl_pci_dev *aster_pci_add(const char *name, void *kernel_ram,
					 unsigned long ram_size)
{
	(void)name;
	(void)kernel_ram;
	(void)ram_size;

	return (struct lkl_pci_dev *)1;
}

static void aster_pci_remove(struct lkl_pci_dev *dev)
{
	(void)dev;
}

static int aster_pci_irq_init(struct lkl_pci_dev *dev, int irq)
{
	(void)dev;

	return aster_call(irq, 0, 0, ASTER_PCI_IRQ_INIT);
}

static int aster_pci_get_msi(struct lkl_pci_dev *dev, int irq)
{
	(void)dev;

	return aster_call(irq, 0, 0, ASTER_PCI_GET_MSI);
}

static void aster_pci_put_msi(struct lkl_pci_dev *dev, int irq)
{
	(void)dev;

	aster_call(irq, 0, 0, ASTER_PCI_PUT_MSI);
}

void aster_nlmsg_ready(unsigned int slot)
{
	aster_call((long)slot, 0, 0, ASTER_NLMSG_READY);
}

static int aster_pci_conf_read(struct lkl_pci_dev *dev, int where, int size,
			       void *val)
{
	(void)dev;

	switch (size) {
	case 1:
		*(unsigned char *)val = (unsigned char)aster_call(
			where, size, 0, ASTER_PCI_CONF_READ);
		break;
	case 2:
		*(unsigned short *)val = (unsigned short)aster_call(
			where, size, 0, ASTER_PCI_CONF_READ);
		break;
	case 4:
		*(unsigned int *)val = (unsigned int)aster_call(
			where, size, 0, ASTER_PCI_CONF_READ);
		break;
	default:
		return 0;
	}

	return size;
}

static int aster_pci_conf_write(struct lkl_pci_dev *dev, int where, int size,
				void *val)
{
	(void)dev;

	switch (size) {
	case 1:
		aster_call(where, size, (unsigned long)*(unsigned char *)val,
			   ASTER_PCI_CONF_WRITE);
		break;
	case 2:
		aster_call(where, size, (unsigned long)*(unsigned short *)val,
			   ASTER_PCI_CONF_WRITE);
		break;
	case 4:
		aster_call(where, size, (unsigned long)*(unsigned int *)val,
			   ASTER_PCI_CONF_WRITE);
		break;
	default:
		return 0;
	}

	return size;
}

static void *aster_pci_bar_alloc(struct lkl_pci_dev *dev, unsigned long size,
				 int index)
{
	(void)dev;
	(void)size;

	return (void *)aster_call(index, 0, 0, ASTER_PCI_BAR_ALLOC);
}

static unsigned long long aster_dma_map(struct lkl_pci_dev *dev, void *vaddr,
					unsigned long size)
{
	extern struct {
		unsigned long long start;
		unsigned long long end;
		unsigned long long base;
	} dma_mapped_region;

	(void)dev;

#define BUG_ON(cond) \
	if (cond)    \
		BUG(#cond);
	BUG_ON((unsigned long long)vaddr < dma_mapped_region.start);
	BUG_ON((unsigned long long)vaddr > dma_mapped_region.end);
	BUG_ON(dma_mapped_region.end - (unsigned long long)vaddr < size);
#undef BUG_ON

	return (unsigned long long)vaddr - dma_mapped_region.start +
	       dma_mapped_region.base;
}

static void aster_dma_unmap(struct lkl_pci_dev *dev,
			    unsigned long long dma_handle, unsigned long size)
{
	(void)dev;
	(void)dma_handle;
	(void)size;
}

static struct lkl_dev_pci_ops pci_ops = {
	.add = &aster_pci_add,
	.remove = &aster_pci_remove,
	.irq_init = &aster_pci_irq_init,
	.get_msi = &aster_pci_get_msi,
	.put_msi = &aster_pci_put_msi,
	.read = &aster_pci_conf_read,
	.write = &aster_pci_conf_write,
	.resource_alloc = &aster_pci_bar_alloc,
	.map_page = &aster_dma_map,
	.unmap_page = &aster_dma_unmap,
};

static struct lkl_host_operations host_ops = {
	.print = &aster_print,
	.panic = &aster_panic,
	.sem_alloc = &aster_sem_alloc,
	.sem_free = &aster_sem_free,
	.sem_up = &aster_sem_up,
	.sem_down = &aster_sem_down,
	.cpu_change_owner = &aster_cpu_set_owner,
	.cpu_get = &aster_cpu_get,
	.cpu_put = &aster_cpu_put,
	.cpu_shutdown = &aster_cpu_shutdown,
	.thread_create = &aster_thread_create,
	.thread_detach = &aster_thread_detach,
	.thread_exit = &aster_thread_exit,
	.thread_join = &aster_thread_join,
	.thread_self = &aster_thread_self,
	.thread_equal = &aster_thread_equal,
	.mem_alloc = &aster_mem_alloc,
	.mem_free = &aster_mem_free,
	.time = &aster_time,
	.timer_alloc = &aster_timer_alloc,
	.timer_set_oneshot = &aster_timer_set_oneshot,
	.timer_free = &aster_timer_free,
	.ioremap = &aster_mmio_map,
	.iomem_access = &aster_mmio_access,
	.vm_map = &aster_vm_map,
	.vm_unmap = &aster_vm_unmap,
	.pin_pages = &aster_pin_pages,
	.vma_insert = &aster_vma_insert,
	.vma_unmap = &aster_vma_unmap,
	.firmware_read = &aster_fw_read,
	.fd_map = &aster_fd_map,
	.jmp_buf_set = &aster_jmpbuf_set,
	.jmp_buf_longjmp = &aster_jmpbuf_jump,
	.pci_ops = &pci_ops,
};

enum {
	LKL_MAIN = 0x8a3562fe,
	LKL_THREAD = 0x34dc3b98,
	LKL_JMP = 0xc29db440,
	LKL_IRQ = 0x36aec67b,
	LKL_SYSCALL = 0x1e7ac8ed,
};

static void lkl_main_entry(void *mem, unsigned long mem_size)
{
	static char cmdline[128] =
		"mem=M     console=lkl_console lkl_pci=aster loglevel=8"
#ifdef __x86_64__
		" msi_addr=0xFEE00000"
#endif
		/* Too many TX interrupts are generated without this */
		" virtio_net.napi_tx=0";
#define MEM_POS_IN_CMDLINE 4

	unsigned long mem_size_mib;
	int i;
	char ch, tmp;

	__mem_start = mem_head = mem;
	__mem_end = mem + mem_size;

	mem_size_mib = mem_size / 1024 / 1024;
	if (mem_size_mib < 10)
		BUG("mem underflow");
	mem_size_mib -= 5;

	if (cmdline[MEM_POS_IN_CMDLINE] != 'M')
		BUG("bad cmdline");

	while (mem_size_mib) {
		i = MEM_POS_IN_CMDLINE;
		ch = cmdline[i];
		cmdline[i] = mem_size_mib % 10 + '0';

		while (cmdline[++i] != ' ') {
			tmp = cmdline[i];
			cmdline[i] = ch;
			ch = tmp;
		}

		if (cmdline[i + 1] != ' ')
			BUG("mem overflow");
		cmdline[i] = ch;

		mem_size_mib /= 10;
	}

	lkl_init(&host_ops);
	lkl_start_kernel(cmdline);
}

static void lkl_thread_entry(void (*f)(void *), void *arg)
{
	lkl_thread_t self = aster_thread_self();
	struct thread_unwind_t *unwind = unwind_alloc(self);

	if (__builtin_setjmp(unwind->jmpbuf) == 0)
		(*f)(arg);

	unwind_free(unwind);
}

extern void deliver_irq_and_run(int irq);

int lkl_entry(long type, long arg0, long arg1)
{
	switch (type) {
	case LKL_MAIN:
		lkl_main_entry((void *)arg0, arg1);
		return 0;

	case LKL_THREAD:
		lkl_thread_entry((void (*)(void *))arg0, (void *)arg1);
		return 0;

	case LKL_JMP:
		lkl_cpu_jump();
		return 0;

	case LKL_IRQ:
		deliver_irq_and_run(arg0);
		return lkl_cpu_check();

	case LKL_SYSCALL:
		return lkl_syscall(arg0, (long *)arg1);

	default:
		return -1;
	}
}
