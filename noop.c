#include <uk/syscall.h>

UK_SYSCALL_R_DEFINE(unsigned long, noop)
{
	return (unsigned long) 0xC0FFEE;
}
