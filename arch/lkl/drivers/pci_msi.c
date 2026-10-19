#include <linux/pci.h>
#include <linux/msi.h>
#include <asm/host_ops.h>

static u64 msi_addr __ro_after_init;

static int __init setup_msi_addr(char *str)
{
	if (kstrtou64(str, 0, &msi_addr) < 0)
		pr_warn("invalid msg_addr\n");

	return 1;
}

__setup("msi_addr=", setup_msi_addr);

static struct irq_chip lkl_msi_irq_chip = {
	.name = "PCI-MSI",
	.irq_mask = pci_msi_mask_irq,
	.irq_unmask = pci_msi_unmask_irq,
};

int arch_setup_msi_irq(struct pci_dev *dev, struct msi_desc *desc)
{
	int irq;
	struct msi_msg msg;

	if (!msi_addr)
		return -EINVAL;

	irq = lkl_get_free_irq("msi");
	if (irq < 0)
		return irq;

	msg.data = lkl_ops->pci_ops->get_msi(dev->sysdata, irq);
	if (msg.data < 0) {
		lkl_put_irq(irq, "msi");
		return -ENOSPC;
	}

	msg.address_hi = (u32)(msi_addr >> 32);
	msg.address_lo = (u32)msi_addr;

	irq_set_msi_desc(irq, desc);

	pci_write_msi_msg(irq, &msg);

	irq_set_chip_and_handler(irq, &lkl_msi_irq_chip, &handle_simple_irq);

	return 0;
}

void arch_teardown_msi_irq(unsigned int irq)
{
	struct msi_desc *desc;
	struct pci_dev *dev;

	desc = irq_get_msi_desc(irq);

	dev = msi_desc_to_pci_dev(desc);

	lkl_ops->pci_ops->put_msi(dev->sysdata, irq);

	lkl_put_irq(irq, "msi");
}
