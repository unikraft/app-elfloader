#ifndef LIBELF_HELPER
#define LIBELF_HELPER

#include <libelf.h>
#include <uk/print.h>

/* Note: fmt has to be given without newline at the end! */
#define elferr_k(klvl, fmtn, ...)					\
	do {								\
		int elf_err = elf_errno();				\
		if (elf_err != 0) {					\
			uk_printk((klvl), fmtn ": %s (%d)\n",		\
				  ##__VA_ARGS__,			\
				  elf_errmsg(elf_err), elf_err);	\
		} else {						\
			uk_printk((klvl), fmtn "\n",			\
				  ##__VA_ARGS__);			\
		}							\
	} while(0)

#define elferr_warn(fmtn, ...) elferr_k(KLVL_WARN, fmtn, ##__VA_ARGS__)
#define elferr_err(fmtn, ...)  elferr_k(KLVL_ERR,  fmtn, ##__VA_ARGS__)
#define elferr_crit(fmtn, ...) elferr_k(KLVL_CRIT, fmtn, ##__VA_ARGS__)

#endif /* LIBELF_HELPER */
