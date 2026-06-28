#include <kernel/ksyms.h>
#include <kernel/compiler.h>

#if CONFIG_KSYMS
const struct ksym ksym_table[1] __weak = {
	{0, NULL},
};
const size_t ksym_count __weak;
#endif

const char *ksym_lookup(uintptr_t addr, uintptr_t *offset)
{
#if CONFIG_KSYMS
	size_t lo = 0;
	size_t hi = ksym_count;
	const struct ksym *best = NULL;

	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;

		if (ksym_table[mid].addr <= addr) {
			best = &ksym_table[mid];
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}

	if (!best || !best->name)
		return NULL;
	if (offset)
		*offset = addr - best->addr;
	return best->name;
#else
	(void)addr;
	if (offset)
		*offset = 0;
	return NULL;
#endif
}
