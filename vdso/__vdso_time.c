
typedef long time_t;

typedef unsigned int __nsec;

#define UKARCH_NSEC_PER_SEC 1000000000ULL
#define ukarch_time_nsec_to_sec(ns)      ((ns) / UKARCH_NSEC_PER_SEC)

extern __nsec (*ukplat_wall_clock)(void);

time_t __vdso_time(time_t * tloc)
{
	time_t secs = ukarch_time_nsec_to_sec(ukplat_wall_clock());

	if (tloc)
		*tloc = secs;

	return secs;
}

time_t time(time_t *)
	__attribute__((weak, alias("__vdso_time")));
