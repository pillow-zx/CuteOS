#include <ulib.h>

const char *strerror(long err)
{
	switch (-err) {
	case ENOENT:
		return "No such file or directory";
	case EACCES:
		return "Permission denied";
	case EEXIST:
		return "File exists";
	case ENOTDIR:
		return "Not a directory";
	case EISDIR:
		return "Is a directory";
	case EINVAL:
		return "Invalid argument";
	case ELOOP:
		return "Too many symbolic links";
	default:
		return "Unknown error";
	}
}

int path_join(char *buf, size_t size, const char *dir, const char *name)
{
	int len;

	if (streq(dir, "/"))
		len = snprintf(buf, size, "/%s", name);
	else
		len = snprintf(buf, size, "%s/%s", dir, name);

	return len >= 0 && (size_t)len < size ? 0 : -1;
}

int is_dot_or_dotdot(const char *name)
{
	return streq(name, ".") || streq(name, "..");
}
