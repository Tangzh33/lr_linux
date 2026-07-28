// SPDX-License-Identifier: GPL-2.0

#include <asm/host_ops.h>

int arch_fd_map(int fd)
{
	return lkl_ops->fd_map(fd);
}
