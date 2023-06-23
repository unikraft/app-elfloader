typedef long time_t;
typedef int clockid_t;

#define	EFAULT 14
#define EINVAL 22

struct timespec {
	time_t tv_sec;
	long   tv_nsec;
};

typedef signed   int     __s64;
typedef unsigned int     __u64;

typedef __u64 __nsec;
typedef __s64 __snsec;

#define UKARCH_NSEC_PER_SEC 1000000000ULL
#define ukarch_time_nsec_to_sec(ns)      ((ns) / UKARCH_NSEC_PER_SEC)
#define ukarch_time_subsec(ns)           ((ns) % 1000000000ULL)
#define ukarch_time_sec_to_nsec(sec)     ((sec)  * UKARCH_NSEC_PER_SEC)

#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_MONOTONIC_COARSE   6

extern __nsec (*ukplat_monotonic_clock)(void);
extern __nsec (*ukplat_wall_clock)(void);

int __vdso_clock_gettime(clockid_t clk_id, struct timespec* tp)
{
	__nsec now;
	int error;

	if (!tp) {
		error = EFAULT;
		goto out_error;
	}

	switch (clk_id) {
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_COARSE:
		now = ukplat_monotonic_clock();
		break;
	case CLOCK_REALTIME:
		now = ukplat_wall_clock();
		break;
	default:
		error = EINVAL;
		goto out_error;
	}

	tp->tv_sec = ukarch_time_nsec_to_sec(now);
	tp->tv_nsec = ukarch_time_subsec(now);
	return 0;

out_error:
	return -error;
}

int clock_gettime(clockid_t, struct timespec*)
	__attribute__((weak, alias("__vdso_clock_gettime")));
