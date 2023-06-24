typedef unsigned int __nsec;

__nsec (*ukplat_monotonic_clock)(void) = (__nsec (*)(void))KADDR_ukplat_monotonic_clock;
__nsec (*ukplat_wall_clock)(void) = (__nsec (*)(void))KADDR_ukplat_wall_clock;

unsigned long (*ukplat_tlsp_enter)(void) = (long unsigned int (*)(void))0x0000000000105380ULL;
void (*ukplat_tlsp_exit)(unsigned long tlsp) = (void (*)(unsigned long))0x00000000001053f0dULL;
unsigned long (*uk_syscall_r_getpid)(void) = (long unsigned int (*)(void))0x00000000001276e0ULL;

unsigned long uk_vdso_syscall_getpid() {
    unsigned long orig_tlsp = ukplat_tlsp_enter();
    unsigned long ret = uk_syscall_r_getpid();
    ukplat_tlsp_exit(orig_tlsp);
    return ret;
}
