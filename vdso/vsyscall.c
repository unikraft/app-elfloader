#include <uk/syscall.h>
#include <uk/plat/syscall.h>
#include <uk/assert.h>
#include <uk/essentials.h>

long __kernel_vsyscall(long syscall_nr, long arg0, long arg1, long arg2, long arg3, long arg4, long arg5)
{
	struct ukarch_sysregs sysregs;
	long ret;

	ukarch_sysregs_switch_uk_tls(&sysregs);

	ret = uk_syscall6_r(syscall_nr,
			    arg0, arg1, arg2,
			    arg3, arg4, arg5);

	ukarch_sysregs_switch_ul_tls(&sysregs);

	return ret;
}
