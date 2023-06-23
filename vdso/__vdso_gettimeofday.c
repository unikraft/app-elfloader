#define EINVAL 22

typedef long time_t;
typedef long suseconds_t;

struct timeval {
	time_t      tv_sec;
	suseconds_t tv_usec;
};

typedef unsigned int __nsec;

extern __nsec (*ukplat_wall_clock)(void);

#define UKARCH_NSEC_PER_SEC 1000000000ULL
#define ukarch_time_nsec_to_sec(ns)      ((ns) / UKARCH_NSEC_PER_SEC)
#define ukarch_time_nsec_to_usec(ns)     ((ns) / 1000UL)
#define ukarch_time_subsec(ns)           ((ns) % 1000000000ULL)

int __vdso_gettimeofday(struct timeval * tv, void * tz)
{
	__nsec now = ukplat_wall_clock();

	if (!tv)
		return -EINVAL;

	tv->tv_sec = ukarch_time_nsec_to_sec(now);
	tv->tv_usec = ukarch_time_nsec_to_usec(ukarch_time_subsec(now));
	return 0;
}

int gettimeofday(struct timeval * tv, void * tz)
	__attribute__((weak, alias("__vdso_gettimeofday")));
