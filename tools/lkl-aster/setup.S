# SPDX-License-Identifier: GPL-2.0

.section ".header", "a", @progbits

magic:      .ascii "LKLASTER"
sz_text:    .quad _stext
sz_rodata:  .quad _srodata
sz_data:    .quad _sdata
sz_bss:     .quad _sbss
p_irqstat:  .quad irq_status
p_irqstats: .quad irq_index_status
p_irqen:    .quad irqs_enabled
p_dmamap:   .quad dma_mapped_region
p_tscfreq:  .quad tsc_frequency
p_syscall:  .quad syscall_argument_page
p_netdev:   .quad asterinas_netdev_descriptor
p_blkdev:   .quad asterinas_block_descriptor
p_wlandev:  .quad asterinas_wlan_descriptor

.section ".setup", "ax", @progbits
.code64

.global _start
_start:
	jmp lkl_entry

.section ".bss", "aw", @nobits

.align 8
.global dma_mapped_region
dma_mapped_region:
	.quad 0  # start
	.quad 0  # end
	.quad 0  # offset

.align 8
.global tsc_frequency
tsc_frequency:
	.quad 0

.align 4096
syscall_argument_page:
	.skip 4096
	.skip 4096
	.skip 4096

.align 8
.global asterinas_netdev_descriptor
asterinas_netdev_descriptor:
	.long 0  # is_ready
	.skip 6  # mac_addr
	.skip 2  # _pad
	.long 0  # doorbell_irq
	.quad 0  # rxq
	.quad 0  # rxq_done
	.quad 0  # txq
	.quad 0  # txq_prep

.align 8
.global asterinas_wlan_descriptor
asterinas_wlan_descriptor:
	.long 0  # is_ready
	.long 0  # lkl_ifindex
	.skip 16 # lkl_ifname
	.long 0  # lkl_wiphy
	.skip 4  # _pad
	.quad 0  # lkl_wdev
	.skip 6  # mac
	.skip 2  # _pad
	.long 0  # mtu
	.skip 4  # _pad

.align 8
.global asterinas_block_descriptor
asterinas_block_descriptor:
	.long 0 # is_ready
	.long 0 # doorbell_irq
	.quad 0 # in_q
	.quad 0 # out_q
.global _asterinas_block_dma_start
_asterinas_block_dma_start:
	.quad 0 # dma_start
.global _asterinas_block_dma_end
_asterinas_block_dma_end:
	.quad 0 # dma_end
