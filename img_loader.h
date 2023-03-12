#ifndef IMG_LOADER_H
#define IMG_LOADER_H

#include <stddef.h>

/**
 * Load the ELF image from the file system or initrd into memory.
 *
 * @param path_name:
 *   Path to the ELF image
 * @param img_len:
 *   Length of ELF image in memory
 * @return:
 *   On success, a pointer to the ELF image in memory is returned.
 *   On errors, NULL is returned and `errno` is set accordingly.
 */
void *img_load(const char *path_name, size_t *img_len);

/**
 * Free the loaded ELF image from memory.
 *
 * @param img:
 *   ELF image returned by img_load()
 */
void img_free(void *img);

#endif /* IMG_LOADER_H */
