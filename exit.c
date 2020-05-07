#include <uk/print.h>
#include <uk/syscall.h>
#include <uk/plat/bootstrap.h>
#include <uk/essentials.h>

UK_SYSCALL_R_DEFINE(int, exit_group, int, status)
{

	if (status >= 0)
		ukplat_halt();

	uk_pr_warn("Application returned error code %d\n", status);
	ukplat_crash();

	/*
	 * We should never reach this point
	 */
	return -EFAULT;
}

UK_SYSCALL_R_DEFINE(int, exit, int, status)
{
	return uk_syscall_r_exit_group(status);
}
