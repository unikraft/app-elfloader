#include <uk/syscall.h>

UK_SYSCALL_R_DEFINE(unsigned long, gettsc)
{
	unsigned cycles_low, cycles_high;
	asm volatile("RDTSC\n\t" // read clock
		     "MOV %%edx, %0\n\t"
		     "MOV %%eax, %1\n\t"
		     : "=r" (cycles_high), "=r" (cycles_low)
		     :: "%rax", "%rdx" );
	return (long) (((unsigned long) cycles_high << 32) | cycles_low);
}
