/*
 * user/init/init.c - 用户态测试程序
 *
 * CuteOS 的第一个用户程序，输出 "Hello CuteOS!" 后退出。
 */

#include <user.h>

int main(void)
{
	write(1, "Hello CuteOS!\n", 14);
	return 0;
}
