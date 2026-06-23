#include <ulib.h>

static int failures;

static void expect_eq_long(const char *name, long got, long want)
{
	if (got != want) {
		printf("ext2-4k-test: %s got %ld want %ld\n", name, got, want);
		failures++;
	}
}

static void expect_true(const char *name, int ok)
{
	if (!ok) {
		printf("ext2-4k-test: %s failed\n", name);
		failures++;
	}
}

static void cleanup_file(const char *path)
{
	(void)unlinkat(AT_FDCWD, path, 0);
}

static void make_entry_name(char *buf, size_t size, int index)
{
	if (size < 5)
		return;

	buf[0] = 'f';
	buf[1] = (char)('0' + (index / 100) % 10);
	buf[2] = (char)('0' + (index / 10) % 10);
	buf[3] = (char)('0' + index % 10);
	buf[4] = '\0';
}

static void fill_pattern(unsigned char *buf, size_t len, unsigned char seed)
{
	for (size_t i = 0; i < len; i++)
		buf[i] = (unsigned char)(seed + (unsigned char)(i * 13u));
}

static int buffers_equal(const unsigned char *a, const unsigned char *b,
			 size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (a[i] != b[i])
			return 0;
	}

	return 1;
}

static void test_regular_file_crosses_4k_boundary(void)
{
	enum {
		WRITE_OFF = 3072,
		WRITE_LEN = 2500,
	};
	static unsigned char wbuf[WRITE_LEN];
	static unsigned char rbuf[WRITE_LEN];
	struct stat st;
	long fd;

	cleanup_file("/ext2-4k-boundary");
	fill_pattern(wbuf, sizeof(wbuf), 0x41);
	memset(rbuf, 0, sizeof(rbuf));

	fd = openat(AT_FDCWD, "/ext2-4k-boundary", O_CREAT | O_RDWR, 0644);
	expect_true("open boundary file", fd >= 0);
	if (fd < 0)
		return;

	expect_eq_long("pwrite boundary",
		       pwrite64((int)fd, wbuf, sizeof(wbuf), WRITE_OFF),
		       (long)sizeof(wbuf));
	expect_eq_long("fstat boundary", fstat((int)fd, &st), 0);
	expect_eq_long("boundary file size", st.st_size,
		       (long)(WRITE_OFF + WRITE_LEN));
	expect_eq_long("pread boundary",
		       pread64((int)fd, rbuf, sizeof(rbuf), WRITE_OFF),
		       (long)sizeof(rbuf));
	expect_true("boundary bytes match",
		    buffers_equal(wbuf, rbuf, sizeof(wbuf)));

	close((int)fd);
}

static void test_directory_ops_cross_4k_blocks(void)
{
	enum {
		ENTRY_COUNT = 320,
	};
	static char dent_buf[512];
	struct stat st;
	char name[16];
	long dirfd;
	long nread;
	int found = 0;
	int entries = 0;

	(void)unlinkat(AT_FDCWD, "/ext2-4k-dir", AT_REMOVEDIR);

	expect_eq_long("mkdir ext2-4k dir",
		       mkdirat(AT_FDCWD, "/ext2-4k-dir", 0777), 0);
	dirfd = openat(AT_FDCWD, "/ext2-4k-dir", O_RDONLY | O_DIRECTORY, 0);
	expect_true("open ext2-4k dir", dirfd >= 0);
	if (dirfd < 0)
		return;

	for (int i = 0; i < ENTRY_COUNT; i++) {
		long fd;

		make_entry_name(name, sizeof(name), i);
		fd = openat((int)dirfd, name, O_CREAT | O_WRONLY, 0644);
		expect_true("create dense dir entry", fd >= 0);
		if (fd >= 0)
			close((int)fd);
	}

	make_entry_name(name, sizeof(name), 0);
	expect_eq_long("lookup first dense entry",
		       fstatat((int)dirfd, name, &st, 0), 0);
	make_entry_name(name, sizeof(name), 255);
	expect_eq_long("lookup boundary dense entry",
		       fstatat((int)dirfd, name, &st, 0), 0);
	make_entry_name(name, sizeof(name), ENTRY_COUNT - 1);
	expect_eq_long("lookup last dense entry",
		       fstatat((int)dirfd, name, &st, 0), 0);

	close((int)dirfd);
	dirfd = openat(AT_FDCWD, "/ext2-4k-dir", O_RDONLY | O_DIRECTORY, 0);
	expect_true("reopen ext2-4k dir", dirfd >= 0);
	if (dirfd < 0)
		return;

	while ((nread = getdents64((int)dirfd, dent_buf, sizeof(dent_buf))) > 0) {
		size_t off = 0;

		while (off < (size_t)nread) {
			struct linux_dirent64 *de =
				(struct linux_dirent64 *)(dent_buf + off);

			if (de->d_reclen == 0)
				break;
			if (!is_dot_or_dotdot(de->d_name)) {
				entries++;
				if (strncmp(de->d_name, "f000", 4) == 0 ||
				    strncmp(de->d_name, "f255", 4) == 0 ||
				    strncmp(de->d_name, "f319", 4) == 0)
					found++;
			}

			off += de->d_reclen;
		}
	}
	expect_true("getdents dense dir completed", nread == 0);
	expect_eq_long("dense dir entry count", entries, ENTRY_COUNT);
	expect_eq_long("dense dir sentinel count", found, 3);

	make_entry_name(name, sizeof(name), ENTRY_COUNT - 1);
	expect_eq_long("unlink last dense entry",
		       unlinkat((int)dirfd, name, 0), 0);
	expect_eq_long("last dense entry removed",
		       fstatat((int)dirfd, name, &st, 0), -ENOENT);

	close((int)dirfd);
	dirfd = openat(AT_FDCWD, "/ext2-4k-dir", O_RDONLY | O_DIRECTORY, 0);
	expect_true("reopen dense dir for cleanup", dirfd >= 0);
	if (dirfd < 0)
		return;

	for (int i = 0; i < ENTRY_COUNT - 1; i++) {
		make_entry_name(name, sizeof(name), i);
		expect_eq_long("cleanup dense dir entry",
			       unlinkat((int)dirfd, name, 0), 0);
	}
	close((int)dirfd);
	expect_eq_long("rmdir dense dir",
		       unlinkat(AT_FDCWD, "/ext2-4k-dir", AT_REMOVEDIR), 0);
}

static void test_truncate_preserves_prefix_and_zero_fills_holes(void)
{
	enum {
		INITIAL_LEN = 6000,
		TRUNCATED_LEN = 3000,
		EXTEND_OFF = 4500,
		TAIL_LEN = 100,
		READ_OFF = 2900,
		READ_LEN = 1700,
	};
	static unsigned char initial[INITIAL_LEN];
	static unsigned char tail[TAIL_LEN];
	static unsigned char got[READ_LEN];
	static unsigned char want[READ_LEN];
	struct stat st;
	long fd;

	cleanup_file("/ext2-4k-trunc");
	fill_pattern(initial, sizeof(initial), 0x21);
	fill_pattern(tail, sizeof(tail), 0x90);
	memset(got, 0, sizeof(got));
	memset(want, 0, sizeof(want));

	fd = openat(AT_FDCWD, "/ext2-4k-trunc", O_CREAT | O_RDWR, 0644);
	expect_true("open truncate file", fd >= 0);
	if (fd < 0)
		return;

	expect_eq_long("pwrite initial truncate payload",
		       pwrite64((int)fd, initial, sizeof(initial), 0),
		       (long)sizeof(initial));
	expect_eq_long("ftruncate shrink", ftruncate((int)fd, TRUNCATED_LEN), 0);
	expect_eq_long("pwrite after shrink",
		       pwrite64((int)fd, tail, sizeof(tail), EXTEND_OFF),
		       (long)sizeof(tail));
	expect_eq_long("fstat truncate file", fstat((int)fd, &st), 0);
	expect_eq_long("truncate file size", st.st_size,
		       (long)(EXTEND_OFF + TAIL_LEN));
	expect_eq_long("pread truncate window",
		       pread64((int)fd, got, sizeof(got), READ_OFF),
		       (long)sizeof(got));

	memcpy(want, initial + READ_OFF, TRUNCATED_LEN - READ_OFF);
	memcpy(want + (EXTEND_OFF - READ_OFF), tail, sizeof(tail));
	expect_true("truncate window bytes match",
		    buffers_equal(want, got, sizeof(want)));

	close((int)fd);
}

int main(void)
{
	test_regular_file_crosses_4k_boundary();
	test_directory_ops_cross_4k_blocks();
	test_truncate_preserves_prefix_and_zero_fills_holes();

	if (failures) {
		printf("ext2-4k-test: %d failures\n", failures);
		return 1;
	}

	printf("ext2-4k-test: PASS\n");
	return 0;
}
