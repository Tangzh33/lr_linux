/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_ASTER_NLMSG_H
#define _ASM_LKL_ASTER_NLMSG_H

#include <linux/compiler_types.h>
#include <linux/linkage.h>

asmlinkage long sys_aster_nlmsg_create(int slot, int protocol, int flags);
asmlinkage long sys_aster_nlmsg_send(int sock_idx, char __user *ubuf, int len);
asmlinkage long sys_aster_nlmsg_recv(int sock_idx, char __user *ubuf,
				     int len, int flags);
asmlinkage long sys_aster_nlmsg_quiesce(int sock_idx);
asmlinkage long sys_aster_nlmsg_release(int sock_idx);
asmlinkage long sys_aster_nlmsg_join(int sock_idx, int group_id);

#endif /* _ASM_LKL_ASTER_NLMSG_H */
