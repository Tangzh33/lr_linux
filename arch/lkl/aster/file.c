// SPDX-License-Identifier: GPL-2.0

#include <asm/host_ops.h>
#include <asm/poll.h>
#include <linux/fs.h>
#include <linux/namei.h>

int arch_poll_wake(wait_queue_entry_t *entry, unsigned mode, int flags,
		   void *key)
{
	struct file *filp = container_of(entry, struct file, f_wq_entry);
	__poll_t events = key_to_poll(key);

	BUG_ON(events & POLLFREE);

	lkl_ops->poll_wake((unsigned long)filp, events);

	return 0;
}

struct file *arch_fd_map(int fd)
{
	struct file *file;

	file = (struct file *)lkl_ops->fd_map(fd);
	if (file)
		get_file(file);

	return file;
}

void arch_cdev_register(unsigned int major, const char *name)
{
	lkl_ops->cdev_register(major, name);
}

void arch_cdev_add(struct cdev *p, dev_t dev, unsigned count)
{
	struct path parent, path;
	struct dentry *dentry;
	int err = 0;

	dentry = kern_path_create(AT_FDCWD, "/cdev", &parent, 0);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto out_warn;
	}

	/* Call security_path_mknod() */

	err = vfs_mknod(mnt_idmap(parent.mnt), d_inode(parent.dentry), dentry,
			S_IFCHR | 0600, dev);
	if (err)
		goto out_path;

	path.mnt = parent.mnt;
	path.dentry = dentry;

	err = lkl_ops->devtmpfs_create((unsigned long)path.mnt,
				       (unsigned long)path.dentry, dev);
	if (!err)
		path_get(&path);
	err = 0;

	vfs_unlink(mnt_idmap(parent.mnt), d_inode(parent.dentry), dentry, NULL);

out_path:
	done_path_create(&parent, dentry);

out_warn:
	if (err)
		pr_warn("cdev %d\n", err);
}
