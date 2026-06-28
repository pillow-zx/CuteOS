/*
 * user/bin/syscall_compat_test.c - raw Linux ABI compatibility tests
 */

#include <ulib.h>

static int has_nul(const char *s, size_t max)
{
	for (size_t i = 0; i < max; i++) {
		if (s[i] == '\0')
			return 1;
	}

	return 0;
}

static int test_getcwd_return_length(void)
{
	char buf[PATH_MAX];
	long ret;

	if (chdir("/") != 0) {
		printf("FAIL: chdir / failed\n");
		return 1;
	}

	memset(buf, 0xaa, sizeof(buf));
	ret = getcwd(buf, sizeof(buf));
	if (ret != 2) {
		printf("FAIL: getcwd ret expected 2 got %ld\n", ret);
		return 1;
	}
	if (strcmp(buf, "/") != 0) {
		printf("FAIL: getcwd buf expected / got %s\n", buf);
		return 1;
	}
	if (ret != (long)strlen(buf) + 1) {
		printf("FAIL: getcwd ret does not include nul\n");
		return 1;
	}

	return 0;
}

static int test_getdents64_small_buffer(void)
{
	char tiny[1];
	long fd;
	long ret;

	fd = open("/", O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		printf("FAIL: open / for small getdents64: %ld\n", fd);
		return 1;
	}

	ret = getdents64((int)fd, tiny, 0);
	if (ret != -EINVAL) {
		printf("FAIL: getdents64 zero expected -%d got %ld\n",
		       EINVAL, ret);
		close((int)fd);
		return 1;
	}

	ret = getdents64((int)fd, tiny, sizeof(tiny));
	close((int)fd);
	if (ret != -EINVAL) {
		printf("FAIL: getdents64 tiny expected -%d got %ld\n",
		       EINVAL, ret);
		return 1;
	}

	return 0;
}

static int test_getdents64_d_off_resume(void)
{
	char buf[512];
	char next_buf[512];
	struct linux_dirent64 *first;
	struct linux_dirent64 *next;
	size_t name_off = OFFSETOF(struct linux_dirent64, d_name);
	long fd;
	long n;
	long n2;
	long off;

	fd = open("/", O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		printf("FAIL: open / for getdents64: %ld\n", fd);
		return 1;
	}

	n = getdents64((int)fd, buf, sizeof(buf));
	if (n <= 0) {
		printf("FAIL: getdents64 root ret=%ld\n", n);
		close((int)fd);
		return 1;
	}

	first = (struct linux_dirent64 *)buf;
	if (first->d_reclen < name_off + 2 ||
	    first->d_reclen > (unsigned long)n) {
		printf("FAIL: first d_reclen=%u n=%ld\n",
		       first->d_reclen, n);
		close((int)fd);
		return 1;
	}
	if (!has_nul(first->d_name, first->d_reclen - name_off)) {
		printf("FAIL: first d_name not nul terminated\n");
		close((int)fd);
		return 1;
	}
	if (first->d_off <= 0) {
		printf("FAIL: first d_off expected >0 got %ld\n",
		       first->d_off);
		close((int)fd);
		return 1;
	}

	off = lseek((int)fd, first->d_off, SEEK_SET);
	if (off != first->d_off) {
		printf("FAIL: lseek d_off expected %ld got %ld\n",
		       first->d_off, off);
		close((int)fd);
		return 1;
	}

	n2 = getdents64((int)fd, next_buf, sizeof(next_buf));
	close((int)fd);
	if (n2 <= 0) {
		printf("FAIL: getdents64 after d_off ret=%ld\n", n2);
		return 1;
	}

	next = (struct linux_dirent64 *)next_buf;
	if (next->d_reclen < name_off + 2 ||
	    next->d_reclen > (unsigned long)n2) {
		printf("FAIL: next d_reclen=%u n=%ld\n",
		       next->d_reclen, n2);
		return 1;
	}
	if (!has_nul(next->d_name, next->d_reclen - name_off)) {
		printf("FAIL: next d_name not nul terminated\n");
		return 1;
	}
	if (strcmp(first->d_name, next->d_name) == 0) {
		printf("FAIL: d_off resume repeated %s\n", first->d_name);
		return 1;
	}

	return 0;
}

int main(void)
{
	int failed = 0;

	printf("syscall_compat_test: getcwd raw return ... ");
	if (test_getcwd_return_length())
		failed++;
	else
		printf("PASS\n");

	printf("syscall_compat_test: getdents64 tiny buffer ... ");
	if (test_getdents64_small_buffer())
		failed++;
	else
		printf("PASS\n");

	printf("syscall_compat_test: getdents64 d_off resume ... ");
	if (test_getdents64_d_off_resume())
		failed++;
	else
		printf("PASS\n");

	if (failed)
		printf("syscall_compat_test: %d test(s) FAILED\n", failed);
	else
		printf("syscall_compat_test: all tests PASSED\n");

	return failed ? 1 : 0;
}
