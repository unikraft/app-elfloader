#include <uk/config.h>
#include <uk/essentials.h>
#include <errno.h>

#include "img_loader.h"

#if CONFIG_LIBVFSCORE_ROOTFS_INITRD
#include <uk/alloc.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#else /* !CONFIG_LIBVFSCORE_ROOTFS_INITRD */
#include <uk/plat/memory.h>
#endif /* !CONFIG_LIBVFSCORE_ROOTFS_INITRD */

#if CONFIG_LIBVFSCORE_ROOTFS_INITRD
void *img_load(const char *path_name, size_t *img_len)
{
	void *img = NULL;
	int fd, rc;
	struct stat st;
	ssize_t bytes_read;

	uk_pr_debug("Loading ELF image from file %s...\n", path_name);

	fd = open(path_name, O_RDONLY, 0);
	if (fd < 0) {
		uk_pr_err("ELF image %s not found\n", path_name);
		goto out;
	}

	rc = fstat(fd, &st);
	if (rc) {
		uk_pr_err("Failed to retrieve ELF image size\n");
		goto out_file;
	}

	img = uk_malloc(uk_alloc_get_default(), st.st_size);
	if (!img) {
		uk_pr_err("Failed to allocate memory for the ELF image\n");
		errno = ENOMEM;
		goto out_file;
	}

	bytes_read = read(fd, img, st.st_size);
	if (bytes_read < 0 || bytes_read != st.st_size) {
		uk_pr_err("Failed to read the ELF image\n");
		goto out_img;
	}

	*img_len = st.st_size;
	goto out_file;

out_img:
	uk_free(uk_alloc_get_default(), img);
	img = NULL;

out_file:
	close(fd);

out:
	return img;
}

void img_free(void *img)
{
	uk_free(uk_alloc_get_default(), img);
}
#else /* !CONFIG_LIBVFSCORE_ROOTFS_INITRD */
void *img_load(const char *path_name, size_t *img_size)
{
	(void)path_name;

	void *img = NULL;
	struct ukplat_memregion_desc *initrd;
	int rc;

	uk_pr_debug("Loading ELF image from initrd...\n");

	rc = ukplat_memregion_find_initrd0(&initrd);
	if (rc < 0 || !initrd->vbase || !initrd->len) {
		uk_pr_err("No image found (initrd parameter missing?)\n");
		errno = (rc < 0) ? -rc : ENOENT;
		goto out;
	}

	img = (void *) initrd->vbase;
	*img_size = initrd->len;

out:
	return img;
}

void img_free(void *img)
{
	(void)img;
}
#endif /* !CONFIG_LIBVFSCORE_ROOTFS_INITRD */
