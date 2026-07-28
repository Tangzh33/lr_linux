// SPDX-License-Identifier: GPL-2.0

#include <asm/host_ops.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/syscalls.h>

#define phys_to_page(phys) pfn_to_page((phys) >> PAGE_SHIFT)

void *arch_vmap(struct page **pages, unsigned int count, unsigned long flags,
		pgprot_t prot)
{
	unsigned long *paddrs;
	unsigned int i;
	void *vaddr;

	paddrs = kmalloc(sizeof(*paddrs) * count, GFP_KERNEL);
	if (!paddrs)
		return NULL;

	for (i = 0; i < count; ++i)
		paddrs[i] = page_to_phys(pages[i]);

	vaddr = lkl_ops->vm_map(paddrs, count, pgprot_val(prot));

	kfree(paddrs);

	return vaddr;
}

void arch_vunmap(const void *addr)
{
	lkl_ops->vm_unmap(addr);
}

int arch_vm_insert_page(struct vm_area_struct *vma, unsigned long addr,
			struct page *page)
{
	if (lkl_ops->vma_insert(addr, page_to_phys(page), PAGE_SIZE) != 0)
		return -EINVAL;

	return 0;
}

int arch_remap_pfn_range(struct vm_area_struct *vma, unsigned long addr,
			 unsigned long pfn, unsigned long size, pgprot_t prot)
{
	unsigned long paddr_prot;

	paddr_prot = (pfn << PAGE_SHIFT) | pgprot_val(prot);

	if (lkl_ops->vma_insert(addr, paddr_prot, size) != 0)
		return -EINVAL;

	return 0;
}

void arch_unmap_mapping_range(struct address_space *mapping, loff_t holebegin,
			      loff_t holelen, int even_cows)
{
	BUILD_BUG_ON(sizeof(loff_t) != sizeof(unsigned long));

	WARN_ON_ONCE(!even_cows);

	lkl_ops->vma_unmap(mapping, (unsigned long)holebegin,
			   (unsigned long)holelen);
}

long arch_pin_user_pages(unsigned long start, unsigned long nr_pages,
			 unsigned int gup_flags, struct page **pages)
{
	unsigned long i;
	int rc;

	rc = -ENOMEM;
	for (i = 0; i < nr_pages; ++i) {
		pages[i] = alloc_page(GFP_KERNEL);
		if (!pages[i])
			goto err;

		get_page(pages[i]);
	}

	for (i = 0; i < nr_pages; ++i)
		pages[i] = (struct page *)page_to_phys(pages[i]);
	rc = lkl_ops->pin_pages(start, (unsigned long *)pages, nr_pages);
	for (i = 0; i < nr_pages; ++i)
		pages[i] = phys_to_page((unsigned long)pages[i]);

	if (rc < 0)
		goto err;

	return nr_pages;

err:
	for (--i; ~i; --i) {
		put_page(pages[i]);
		put_page(pages[i]);
	}

	return rc;
}

void arch_unpin_user_pages(struct page **pages, unsigned long npages)
{
	unsigned long i;

	for (i = 0; i < npages; ++i)
		put_page(pages[i]);
}

SYSCALL_DEFINE2(free_pinned_pages, unsigned long __user *, paddrs,
		unsigned long, npages)
{
	unsigned long i;

	for (i = 0; i < npages; ++i) {
		unsigned long paddr;
		if (get_user(paddr, &paddrs[i]))
			return -EFAULT;

		put_page(phys_to_page(paddr));
	}

	return 0;
}
