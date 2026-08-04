// SPDX-License-Identifier: GPL-2.0

#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/netlink.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/sockptr.h>
#include <linux/uaccess.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <asm/aster/nlmsg.h>

#define MAX_NLMSG_SOCKETS 64
#define NLMSG_MAX_LEN 10240
/* Use fixed negative port IDs away from normal task PIDs. */
#define ASTER_NLMSG_PORTID_BASE (-4097)

struct aster_nlmsg_ctx {
	unsigned int slot;
};

static DEFINE_MUTEX(aster_nlmsg_lock);
static struct socket *aster_nlmsg_sockets[MAX_NLMSG_SOCKETS];

extern void aster_nlmsg_ready(unsigned int slot);

static bool aster_nlmsg_valid_slot(int slot)
{
	return slot >= 0 && slot < MAX_NLMSG_SOCKETS;
}

static bool aster_nlmsg_valid_len(int len)
{
	return len > 0 && len <= NLMSG_MAX_LEN;
}

static u32 aster_nlmsg_portid(int slot)
{
	return (u32)(ASTER_NLMSG_PORTID_BASE - slot);
}

static void aster_nlmsg_data_ready(struct sock *sk)
{
	struct aster_nlmsg_ctx *ctx;

	rcu_read_lock();
	ctx = rcu_dereference_sk_user_data(sk);
	if (ctx)
		aster_nlmsg_ready(ctx->slot);
	rcu_read_unlock();

	sock_def_readable(sk);
}

static struct aster_nlmsg_ctx *aster_nlmsg_clear_callback(struct socket *sock)
{
	struct aster_nlmsg_ctx *ctx;
	struct sock *sk = sock->sk;

	write_lock_bh(&sk->sk_callback_lock);
	ctx = rcu_dereference_protected(__sk_user_data(sk),
					  lockdep_is_held(&sk->sk_callback_lock));
	if (sk->sk_data_ready == aster_nlmsg_data_ready) {
		rcu_assign_sk_user_data(sk, NULL);
		sk->sk_data_ready = sock_def_readable;
	} else {
		ctx = NULL;
	}
	write_unlock_bh(&sk->sk_callback_lock);

	return ctx;
}

static void aster_nlmsg_quiesce_sock(struct socket *sock)
{
	struct aster_nlmsg_ctx *ctx = aster_nlmsg_clear_callback(sock);

	if (!ctx)
		return;

	synchronize_rcu();
	kfree(ctx);
}

asmlinkage long sys_aster_nlmsg_create(int slot, int protocol, int flags)
{
	struct sockaddr_nl addr;
	struct aster_nlmsg_ctx *ctx;
	struct socket *sock;
	int ret;

	if (!aster_nlmsg_valid_slot(slot))
		return -EINVAL;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->slot = slot;

	mutex_lock(&aster_nlmsg_lock);
	if (aster_nlmsg_sockets[slot]) {
		ret = -EEXIST;
		goto out_unlock_free_ctx;
	}

	ret = sock_create_kern(&init_net, AF_NETLINK, SOCK_RAW | flags,
			       protocol, &sock);
	if (ret)
		goto out_unlock_free_ctx;

	memset(&addr, 0, sizeof(addr));
	addr.nl_family = AF_NETLINK;
	addr.nl_pid = aster_nlmsg_portid(slot);
	ret = sock->ops->bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret)
		goto out_release_sock;

	write_lock_bh(&sock->sk->sk_callback_lock);
	rcu_assign_sk_user_data(sock->sk, ctx);
	sock->sk->sk_data_ready = aster_nlmsg_data_ready;
	write_unlock_bh(&sock->sk->sk_callback_lock);

	aster_nlmsg_sockets[slot] = sock;
	mutex_unlock(&aster_nlmsg_lock);

	return 0;

out_release_sock:
	sock_release(sock);
out_unlock_free_ctx:
	mutex_unlock(&aster_nlmsg_lock);
	kfree(ctx);
	return ret;
}

asmlinkage long sys_aster_nlmsg_send(int sock_idx, char __user *ubuf, int len)
{
	struct sockaddr_nl addr = {
		.nl_family = AF_NETLINK,
	};
	struct msghdr msg = {
		.msg_name = &addr,
		.msg_namelen = sizeof(addr),
	};
	struct kvec iov;
	struct socket *sock;
	void *buf;
	long ret;

	if (!aster_nlmsg_valid_slot(sock_idx) || !aster_nlmsg_valid_len(len))
		return -EINVAL;

	buf = memdup_user(ubuf, len);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	mutex_lock(&aster_nlmsg_lock);
	sock = aster_nlmsg_sockets[sock_idx];
	if (!sock) {
		ret = -EBADF;
		goto out_unlock;
	}

	iov.iov_base = buf;
	iov.iov_len = len;
	ret = kernel_sendmsg(sock, &msg, &iov, 1, len);

out_unlock:
	mutex_unlock(&aster_nlmsg_lock);
	kfree(buf);
	return ret;
}

asmlinkage long sys_aster_nlmsg_recv(int sock_idx, char __user *ubuf,
				     int len, int flags)
{
	struct msghdr msg = { .msg_flags = flags };
	struct kvec iov;
	struct socket *sock;
	void *buf;
	long ret;

	if (!aster_nlmsg_valid_slot(sock_idx) || !aster_nlmsg_valid_len(len))
		return -EINVAL;

	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&aster_nlmsg_lock);
	sock = aster_nlmsg_sockets[sock_idx];
	if (!sock) {
		ret = -EBADF;
		goto out_unlock;
	}

	iov.iov_base = buf;
	iov.iov_len = len;
	ret = kernel_recvmsg(sock, &msg, &iov, 1, len, flags);
	if (ret > 0 && copy_to_user(ubuf, buf, ret))
		ret = -EFAULT;

out_unlock:
	mutex_unlock(&aster_nlmsg_lock);
	kfree(buf);
	return ret;
}

asmlinkage long sys_aster_nlmsg_quiesce(int sock_idx)
{
	struct socket *sock;

	if (!aster_nlmsg_valid_slot(sock_idx))
		return -EINVAL;

	mutex_lock(&aster_nlmsg_lock);
	sock = aster_nlmsg_sockets[sock_idx];
	if (!sock) {
		mutex_unlock(&aster_nlmsg_lock);
		return -EBADF;
	}
	aster_nlmsg_quiesce_sock(sock);
	mutex_unlock(&aster_nlmsg_lock);

	return 0;
}

asmlinkage long sys_aster_nlmsg_release(int sock_idx)
{
	struct socket *sock;

	if (!aster_nlmsg_valid_slot(sock_idx))
		return -EINVAL;

	mutex_lock(&aster_nlmsg_lock);
	sock = aster_nlmsg_sockets[sock_idx];
	if (!sock) {
		mutex_unlock(&aster_nlmsg_lock);
		return -EBADF;
	}
	aster_nlmsg_sockets[sock_idx] = NULL;
	aster_nlmsg_quiesce_sock(sock);
	mutex_unlock(&aster_nlmsg_lock);

	kernel_sock_shutdown(sock, SHUT_RDWR);
	sock_release(sock);
	return 0;
}

asmlinkage long sys_aster_nlmsg_join(int sock_idx, int group_id)
{
	struct socket *sock;
	long ret;

	if (!aster_nlmsg_valid_slot(sock_idx))
		return -EINVAL;

	mutex_lock(&aster_nlmsg_lock);
	sock = aster_nlmsg_sockets[sock_idx];
	if (!sock) {
		ret = -EBADF;
		goto out_unlock;
	}
	if (!sock->ops->setsockopt) {
		ret = -EOPNOTSUPP;
		goto out_unlock;
	}

	ret = sock->ops->setsockopt(sock, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
				     KERNEL_SOCKPTR(&group_id), sizeof(group_id));

out_unlock:
	mutex_unlock(&aster_nlmsg_lock);
	return ret;
}
