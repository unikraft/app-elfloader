typedef unsigned long __nsec;

__nsec (*ukplat_monotonic_clock)(void) = (__nsec (*)(void))KADDR_ukplat_monotonic_clock;
__nsec (*ukplat_wall_clock)(void) = (__nsec (*)(void))KADDR_ukplat_wall_clock;

unsigned long (*ukplat_tlsp_enter)(void) = (long unsigned int (*)(void))0x0000000000105360ULL;
void (*ukplat_tlsp_exit)(unsigned long tlsp) = (void (*)(unsigned long))0x00000000001053c0ULL;

int (*uk_syscall_r_getpid)(void) = (int (*)(void))0x0000000000127130ULL;
int (*uk_syscall_r_getppid)(void) = (int (*)(void))0x0000000000127550ULL;

unsigned long uk_vdso_syscall_getpid() {
    unsigned long orig_tlsp = ukplat_tlsp_enter();
    int ret = uk_syscall_r_getpid();
    ukplat_tlsp_exit(orig_tlsp);
    return ret;
}

unsigned long uk_vdso_syscall_getppid() {
    unsigned long orig_tlsp = ukplat_tlsp_enter();
    int ret = uk_syscall_r_getppid();
    ukplat_tlsp_exit(orig_tlsp);
    return ret;
}
