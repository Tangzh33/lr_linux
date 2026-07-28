// SPDX-License-Identifier: GPL-2.0

#include <asm/host_ops.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

static ssize_t read_firmware_to_end(const char *path, void **buf_p,
				    size_t max_size)
{
	ssize_t path_size = strlen(path);
	ssize_t file_size;
	char *buf = NULL;
	ssize_t buf_size = 0;

	for (;;) {
		file_size =
			lkl_ops->firmware_read(path, path_size, buf, buf_size);
		if (file_size < 0 || file_size <= buf_size)
			break;
		if (file_size > SSIZE_MAX || file_size > max_size)
			return -EFBIG;

		vfree(buf);
		buf = vmalloc(file_size);
		if (!buf)
			return -ENOMEM;

		buf_size = file_size;
	}

	if (file_size < 0)
		vfree(buf);
	else
		*buf_p = buf;

	return file_size;
}

static ssize_t read_firmware_to_buf(const char *path, void *buf,
				    size_t buf_size)
{
	ssize_t path_size = strlen(path);
	ssize_t file_size;

	if (buf_size > SSIZE_MAX)
		return -EFBIG;

	file_size = lkl_ops->firmware_read(path, path_size, buf, buf_size);

	return file_size;
}

ssize_t arch_read_firmware(const char *path, loff_t offset, void **buf,
			   size_t buf_size, size_t *file_size)
{
	ssize_t rc;

	if (!path || !*path)
		return -EINVAL;
	if (offset)
		return -EINVAL;
	if (!lkl_ops->firmware_read)
		return -ENOENT;

	if (!*buf) {
		rc = read_firmware_to_end(path, buf, buf_size);
		if (rc < 0)
			return rc;

		if (file_size)
			*file_size = rc;

		return rc;
	} else {
		rc = read_firmware_to_buf(path, *buf, buf_size);
		if (rc < 0)
			return rc;

		if (file_size)
			*file_size = rc;

		if (rc > buf_size)
			rc = buf_size;
		return rc;
	}
}
