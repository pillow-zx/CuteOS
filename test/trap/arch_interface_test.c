/*
 * test/arch_interface_test.c - arch public header compile-time contracts
 */

#include <kernel/compiler.h>
#include <kernel/test.h>
#include <kernel/types.h>
#include <arch/irq.h>
#include <arch/pgtable.h>
#include <arch/trap.h>
#include <arch/uaccess.h>
#include <arch/user_map.h>

#ifndef ARCH_TRAP_REG_SEPC
#error "ARCH_TRAP_REG_* must be preprocessor constants"
#endif

static_assert(same_type(&local_irq_save, (irq_flags_t (*)(void))0),
	      "arch irq header must expose local_irq_save()");
static_assert(same_type(typeof(pgprot_user(true, true, true)), pgprot_t),
	      "arch pgtable header must expose pgprot_user()");
static_assert(same_type(&pagetable_use_buddy, (void (*)(void))0),
	      "arch pgtable header must expose pagetable_use_buddy()");
static_assert(same_type(&map_page,
			(int (*)(pte_t *, uintptr_t, uintptr_t, uint64_t))0),
	      "arch pgtable header must expose map_page()");
static_assert(same_type(typeof(trap_user_sp((const struct trap_frame *)0)),
			uintptr_t),
	      "arch trap header must expose trap_user_sp()");
static_assert(same_type(typeof(syscall_nr((const struct trap_frame *)0)),
			size_t),
	      "arch trap header must expose syscall_nr()");
static_assert(same_type(&user_access_begin, (bool (*)(void))0),
	      "arch uaccess header must expose user_access_begin()");
static_assert(same_type(&user_map_init, (void (*)(void))0),
	      "arch user_map header must expose user_map_init()");

#include "../ktest.h"

int test_arch_interface_static_contracts(void)
{
	TEST_BEGIN("arch-interface: public header static contracts");
	TEST_END("arch-interface: public header static contracts");
	return __test_ret;
}
