#include <ulib.h>

#define SYS_statfs64	43
#define SYS_fstatfs64	44
#define SYS_ppoll	73
#define SYS_nanosleep	101
#define SYS_prlimit64	261
#define SYS_getrandom	278
#define SYS_rseq	293

#define POLLIN	 0x0001
#define POLLOUT	 0x0004
#define POLLERR	 0x0008
#define POLLHUP	 0x0010
#define POLLNVAL 0x0020

#define GRND_NONBLOCK 0x0001
#define GRND_RANDOM   0x0002
#define GRND_INSECURE 0x0004

#define RLIMIT_NOFILE 7

#define RSEQ_FLAG_UNREGISTER 1

struct compat_pollfd {
	int fd;
	short events;
	short revents;
};

struct compat_statfs64 {
	long f_type;
	long f_bsize;
	unsigned long f_blocks;
	unsigned long f_bfree;
	unsigned long f_bavail;
	unsigned long f_files;
	unsigned long f_ffree;
	int f_fsid[2];
	long f_namelen;
	long f_frsize;
	long f_flags;
	long f_spare[4];
};

struct compat_rlimit64 {
	unsigned long rlim_cur;
	unsigned long rlim_max;
};

static int failures;

static void expect_eq_long(const char *name, long got, long want)
{
	if (got != want) {
		printf("syscall-compat-test: %s got %ld want %ld\n", name, got,
		       want);
		failures++;
	}
}

static void expect_true(const char *name, int ok)
{
	if (!ok) {
		printf("syscall-compat-test: %s failed\n", name);
		failures++;
	}
}

static int any_nonzero(const unsigned char *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (buf[i] != 0)
			return 1;
	}
	return 0;
}

static void test_nanosleep(void)
{
	struct timespec bad = {.tv_sec = 0, .tv_nsec = 1000000000};
	struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000};
	struct timespec before;
	struct timespec after;
	long ret;

	expect_eq_long("nanosleep invalid",
		       syscall(SYS_nanosleep, (long)&bad, 0), -EINVAL);

	expect_eq_long("clock_gettime before",
		       clock_gettime(CLOCK_MONOTONIC, &before), 0);
	ret = syscall(SYS_nanosleep, (long)&ts, 0);
	expect_eq_long("nanosleep short", ret, 0);
	expect_eq_long("clock_gettime after", clock_gettime(CLOCK_MONOTONIC,
							    &after),
		       0);
	expect_true("nanosleep elapsed",
		    after.tv_sec > before.tv_sec ||
			    (after.tv_sec == before.tv_sec &&
			     after.tv_nsec >= before.tv_nsec));
}

static void test_getrandom(void)
{
	unsigned char buf[32];
	unsigned char zero[32];

	memset(buf, 0, sizeof(buf));
	memset(zero, 0, sizeof(zero));
	expect_eq_long("getrandom length",
		       syscall(SYS_getrandom, (long)buf, sizeof(buf),
			       GRND_NONBLOCK | GRND_INSECURE),
		       (long)sizeof(buf));
	(void)zero;
	expect_true("getrandom nonzero", any_nonzero(buf, sizeof(buf)));
	expect_eq_long("getrandom bad flags",
		       syscall(SYS_getrandom, (long)buf, sizeof(buf), 0x8000),
		       -EINVAL);
}

static void test_prlimit64(void)
{
	struct compat_rlimit64 lim;
	struct compat_rlimit64 old;
	struct compat_rlimit64 set = {.rlim_cur = 16, .rlim_max = 16};
	int status = -1;
	long pid;

	expect_eq_long("prlimit get",
		       syscall(SYS_prlimit64, 0, RLIMIT_NOFILE, 0, (long)&lim),
		       0);
	expect_true("prlimit nofile sane", lim.rlim_cur >= 3);
	expect_eq_long("prlimit set",
		       syscall(SYS_prlimit64, 0, RLIMIT_NOFILE, (long)&set,
			       (long)&old),
		       0);
	expect_eq_long("prlimit get changed",
		       syscall(SYS_prlimit64, 0, RLIMIT_NOFILE, 0, (long)&lim),
		       0);
	expect_eq_long("prlimit nofile cur", lim.rlim_cur, 16);
	expect_eq_long("prlimit bad resource",
		       syscall(SYS_prlimit64, 0, 999, 0, (long)&lim), -EINVAL);

	pid = fork();
	if (pid == 0) {
		struct compat_rlimit64 child_lim;

		if (syscall(SYS_prlimit64, 0, RLIMIT_NOFILE, 0,
			    (long)&child_lim) == 0 &&
		    child_lim.rlim_cur == 16 && child_lim.rlim_max == 16)
			exit(0);
		exit(1);
	}
	expect_true("prlimit fork child created", pid > 0);
	if (pid > 0) {
		expect_eq_long("prlimit fork wait", wait4(pid, &status, 0, 0),
			       pid);
		expect_eq_long("prlimit fork inherit", status, 0);
	}
}

static void test_statfs64(void)
{
	struct compat_statfs64 st;
	int fd;

	expect_eq_long("statfs64 root",
		       syscall(SYS_statfs64, (long)"/", (long)&st), 0);
	expect_true("statfs64 blocks", st.f_bsize > 0 && st.f_blocks > 0);
	expect_true("statfs64 namelen", st.f_namelen >= 255);

	fd = open("/", O_RDONLY | O_DIRECTORY);
	expect_true("open root dir", fd >= 0);
	if (fd >= 0) {
		expect_eq_long("fstatfs64 root",
			       syscall(SYS_fstatfs64, fd, (long)&st),
			       0);
		close(fd);
	}
	expect_eq_long("fstatfs64 bad fd",
		       syscall(SYS_fstatfs64, 1234, (long)&st), -EBADF);
}

static void test_ppoll(void)
{
	int fds[2];
	char ch = 'x';
	struct timespec zero = {0, 0};
	struct compat_pollfd pfd;

	expect_eq_long("pipe", pipe(fds), 0);

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = fds[0];
	pfd.events = POLLIN;
	expect_eq_long("ppoll empty pipe",
		       syscall(SYS_ppoll, (long)&pfd, 1, (long)&zero, 0,
			       sizeof(unsigned long)),
		       0);
	expect_eq_long("write pipe byte", write(fds[1], &ch, 1), 1);
	expect_eq_long("ppoll pipe readable",
		       syscall(SYS_ppoll, (long)&pfd, 1, (long)&zero, 0,
			       sizeof(unsigned long)),
		       1);
	expect_true("ppoll revents pollin", (pfd.revents & POLLIN) != 0);

	pfd.fd = -1;
	pfd.events = POLLIN;
	pfd.revents = 0;
	expect_eq_long("ppoll ignored negative fd",
		       syscall(SYS_ppoll, (long)&pfd, 1, (long)&zero, 0,
			       sizeof(unsigned long)),
		       0);
	expect_eq_long("ppoll negative revents", pfd.revents, 0);

	pfd.fd = 1234;
	pfd.events = POLLIN;
	pfd.revents = 0;
	expect_eq_long("ppoll bad fd",
		       syscall(SYS_ppoll, (long)&pfd, 1, (long)&zero, 0,
			       sizeof(unsigned long)),
		       1);
	expect_true("ppoll pollnval", (pfd.revents & POLLNVAL) != 0);

	close(fds[0]);
	close(fds[1]);
}

static void test_rseq(void)
{
	expect_eq_long("rseq fallback",
		       syscall(SYS_rseq, 0, 0, RSEQ_FLAG_UNREGISTER, 0),
		       -ENOSYS);
}

int main(void)
{
	test_nanosleep();
	test_getrandom();
	test_prlimit64();
	test_statfs64();
	test_ppoll();
	test_rseq();

	if (failures) {
		printf("syscall-compat-test: %d failures\n", failures);
		return 1;
	}

	printf("syscall-compat-test: PASS\n");
	return 0;
}
