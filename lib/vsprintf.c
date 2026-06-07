/*
 * lib/vsprintf.c - 格式化输出（printk 底层）
 *
 * 功能：
 *   实现内核中的格式化字符串核心引擎。提供 vsprintf/vsnprintf 函数，
 *   支持 printf 风格格式串解析。printk 的输出最终依赖此模块完成
 *   数值到字符串的转换。所有格式转换均在栈上完成，不依赖动态内存分配。
 *
 * 支持的格式符：
 *   %d       - 有符号十进制整数
 *   %u       - 无符号十进制整数
 *   %x       - 无符号十六进制整数（小写）
 *   %X       - 无符号十六进制整数（大写）
 *   %s       - 字符串
 *   %c       - 字符
 *   %p       - 指针（带 0x 前缀，补零到指针宽度）
 *   %%       - 转义百分号
 *   %ld      - 有符号 long 十进制
 *   %lu      - 无符号 long 十进制
 *   %llx     - 无符号 long long 十六进制
 *   %#x      - 带前缀的十六进制（0x，仅非零值）
 *   %-Ns     - 左对齐字符串（N 为宽度）
 *   %0Nd     - 零填充十进制（N 为宽度）
 *
 * 主要函数：
 *   vsnprintf(buf, size, fmt, ap) - 带缓冲区大小限制的格式化
 *   vsprintf(buf, fmt, ap)        - 将格式化结果写入缓冲区
 */

#include <kernel/types.h>
#include <kernel/printk.h>

/* vsprintf 不依赖完整 string.h，仅声明所需函数 */
extern size_t strlen(const char *s);

/* 向缓冲区写入一个字符（不越界），返回新的写入位置 */
static char *emit(char *buf, char *end, char c)
{
        if (buf < end)
                *buf = c;
        return buf + 1;
}

/*
 * emit_string - 向缓冲区写入字符串，支持宽度与对齐
 * @buf:   缓冲区当前位置
 * @end:   缓冲区末尾（不包含）
 * @s:     待写入的字符串
 * @width: 最小字段宽度
 * @left:  是否左对齐
 *
 * 返回写入后的缓冲区位置。
 */
static char *emit_string(char *buf, char *end, const char *s, int width,
                         int left)
{
        size_t len = strlen(s);
        int pad = (width > (int)len) ? width - (int)len : 0;

        if (!left)
                while (pad-- > 0)
                        buf = emit(buf, end, ' ');
        while (*s)
                buf = emit(buf, end, *s++);
        if (left)
                while (pad-- > 0)
                        buf = emit(buf, end, ' ');

        return buf;
}

/*
 * emit_num - 向缓冲区写入整数值
 * @buf:    缓冲区当前位置
 * @end:    缓冲区末尾
 * @num:    无符号数值
 * @base:   进制（10 或 16）
 * @upper:  十六进制是否使用大写字母
 * @width:  最小字段宽度
 * @zero:   零填充（否则空格填充）
 * @neg:    是否为负数（前置 '-'）
 *
 * 返回写入后的缓冲区位置。
 */
static char *emit_num(char *buf, char *end, uint64_t num, int base, int upper,
                      int width, int zero, int neg)
{
        static const char digits_lower[] = "0123456789abcdef";
        static const char digits_upper[] = "0123456789ABCDEF";
        const char *digits = upper ? digits_upper : digits_lower;

        char tmp[65];
        int nd = 0;

        /* 将数值转换为字符（逆序） */
        if (num == 0) {
                tmp[nd++] = '0';
        } else {
                while (num) {
                        tmp[nd++] = digits[num % base];
                        num /= base;
                }
        }

        /* 计算填充 */
        int total = nd + (neg ? 1 : 0);
        int pad = (width > total) ? width - total : 0;

        /* 空格填充（在符号之前） */
        if (!zero && !neg)
                while (pad-- > 0)
                        buf = emit(buf, end, ' ');

        /* 负号 */
        if (neg)
                buf = emit(buf, end, '-');

        /* 零填充（在符号之后、数字之前） */
        if (zero)
                while (pad-- > 0)
                        buf = emit(buf, end, '0');

        /* 空格填充（在符号之后，用于负数非零填充） */
        if (!zero && neg)
                while (pad-- > 0)
                        buf = emit(buf, end, ' ');

        /* 写入数字（从高位到低位） */
        while (--nd >= 0)
                buf = emit(buf, end, tmp[nd]);

        return buf;
}

/*
 * 长度修饰符枚举
 *
 * 对应 %d 中的 'l' 前缀：无前缀、%ld、%lld
 */
enum len_mod { LEN_NONE, LEN_L, LEN_LL };

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
        if (size == 0)
                return 0;

        char *p = buf;
        char *end = buf + size - 1; /* 保留一个字节给 '\0' */

        while (*fmt) {
                /* 普通字符直接输出 */
                if (*fmt != '%') {
                        p = emit(p, end, *fmt++);
                        continue;
                }
                fmt++; /* 跳过 '%' */

                /* ---- 解析 flags ---- */
                int flag_zero = 0;
                int flag_left = 0;
                int flag_alt = 0;

                for (;;) {
                        if (*fmt == '0')
                                flag_zero = 1;
                        else if (*fmt == '-')
                                flag_left = 1;
                        else if (*fmt == '#')
                                flag_alt = 1;
                        else
                                break;
                        fmt++;
                }

                /* 左对齐时禁用零填充 */
                if (flag_left)
                        flag_zero = 0;

                /* ---- 解析 width ---- */
                int width = 0;
                while (*fmt >= '0' && *fmt <= '9')
                        width = width * 10 + (*fmt++ - '0');

                /* ---- 解析长度修饰符 ---- */
                enum len_mod len = LEN_NONE;
                if (*fmt == 'l') {
                        len = LEN_L;
                        fmt++;
                        if (*fmt == 'l') {
                                len = LEN_LL;
                                fmt++;
                        }
                }

                /* ---- 解析转换说明符 ---- */
                char conv = *fmt++;

                switch (conv) {
                case '%':
                        p = emit(p, end, '%');
                        break;

                case 'c': {
                        char c = (char)va_arg(ap, int);
                        p = emit(p, end, c);
                        break;
                }

                case 's': {
                        const char *s = va_arg(ap, const char *);
                        if (!s)
                                s = "(null)";
                        p = emit_string(p, end, s, width, flag_left);
                        break;
                }

                case 'd': {
                        int64_t num;
                        if (len == LEN_LL)
                                num = va_arg(ap, int64_t);
                        else if (len == LEN_L)
                                num = va_arg(ap, long);
                        else
                                num = va_arg(ap, int);

                        int neg = (num < 0);
                        uint64_t unum = neg ? -(uint64_t)num : (uint64_t)num;
                        p = emit_num(p, end, unum, 10, 0, width, flag_zero,
                                     neg);
                        break;
                }

                case 'u': {
                        uint64_t unum;
                        if (len == LEN_LL)
                                unum = va_arg(ap, uint64_t);
                        else if (len == LEN_L)
                                unum = va_arg(ap, unsigned long);
                        else
                                unum = va_arg(ap, unsigned int);

                        p = emit_num(p, end, unum, 10, 0, width, flag_zero, 0);
                        break;
                }

                case 'x': {
                        uint64_t unum;
                        if (len == LEN_LL)
                                unum = va_arg(ap, uint64_t);
                        else if (len == LEN_L)
                                unum = va_arg(ap, unsigned long);
                        else
                                unum = va_arg(ap, unsigned int);

                        if (flag_alt && unum != 0) {
                                p = emit(p, end, '0');
                                p = emit(p, end, 'x');
                        }
                        p = emit_num(p, end, unum, 16, 0, width, flag_zero, 0);
                        break;
                }

                case 'X': {
                        uint64_t unum;
                        if (len == LEN_LL)
                                unum = va_arg(ap, uint64_t);
                        else if (len == LEN_L)
                                unum = va_arg(ap, unsigned long);
                        else
                                unum = va_arg(ap, unsigned int);

                        if (flag_alt && unum != 0) {
                                p = emit(p, end, '0');
                                p = emit(p, end, 'X');
                        }
                        p = emit_num(p, end, unum, 16, 1, width, flag_zero, 0);
                        break;
                }

                case 'p': {
                        void *ptr = va_arg(ap, void *);
                        uint64_t unum = (uintptr_t)ptr;

                        p = emit(p, end, '0');
                        p = emit(p, end, 'x');
                        /* 指针补零到固定宽度 (rv64: 16 hex digits) */
                        p = emit_num(p, end, unum, 16, 0, sizeof(void *) * 2, 1,
                                     0);
                        break;
                }

                default:
                        /* 未知转换符，原样输出 */
                        p = emit(p, end, '%');
                        p = emit(p, end, conv);
                        break;
                }
        }

        /* 确保以 '\0' 结尾 */
        if (p <= end)
                *p = '\0';
        else
                *end = '\0';

        return (int)(p - buf);
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
        /* 用一个很大的 size 模拟无限制 */
        return vsnprintf(buf, (size_t)-1, fmt, ap);
}
