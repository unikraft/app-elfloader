#define	EFAULT 14
#define EINVAL 22

#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_MONOTONIC_COARSE   6

#define UKARCH_NSEC_PER_SEC 1000000000ULL
#define UKPLAT_TIME_TICK_NSEC  (UKARCH_NSEC_PER_SEC / CONFIG_HZ)

typedef int clockid_t;
typedef long time_t;

struct timespec {
	time_t tv_sec;
	long   tv_nsec;
};

int __vdso_clock_getres(clockid_t clk_id, struct timespec * tp)
{
	int error;

	if (!tp) {
		error = EFAULT;
		goto out_error;
	}

	switch (clk_id) {
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_COARSE:
	case CLOCK_REALTIME:
		tp->tv_sec = 0;
		tp->tv_nsec = UKPLAT_TIME_TICK_NSEC;
		break;
	default:
		error = EINVAL;
		goto out_error;
	}

	return 0;

out_error:
	return -error;
}

int clock_getres(clockid_t, struct timespec *)
	__attribute__((weak, alias("__vdso_clock_getres")));
