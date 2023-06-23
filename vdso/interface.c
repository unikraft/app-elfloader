typedef unsigned int __nsec;

__nsec (*ukplat_monotonic_clock)(void) = (__nsec (*)(void))KADDR_ukplat_monotonic_clock;
__nsec (*ukplat_wall_clock)(void) = (__nsec (*)(void))KADDR_ukplat_wall_clock;
