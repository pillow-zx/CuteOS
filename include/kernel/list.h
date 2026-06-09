#ifndef _CUTEOS_KERNEL_LIST_H
#define _CUTEOS_KERNEL_LIST_H

/*
 * include/kernel/list.h - 双向循环链表与遍历宏
 *
 * 定义 struct list_head 及全套链表操作。节点嵌入在数据结构内部
 * （不单独分配）；container_of 宏从 list_head 指针恢复包含它的对象。
 * 所有操作均为 O(1)。
 *
 * 核心结构体：
 *   struct list_head - { prev, next }
 *
 * 初始化：
 *   LIST_HEAD_INIT(name) - 静态初始化器
 *   LIST_HEAD(name)      - 声明并初始化
 *
 * 操作：
 *   list_add(new, head)          - 在 head 之后插入（前插）
 *   list_add_tail(new, head)     - 在 head 之前插入（后插）
 *   list_del(entry)              - 从链表中移除 entry
 *   list_empty(head)             - 测试链表是否为空
 *
 * 遍历：
 *   list_first_entry(ptr, type, member) - 获取第一个包含它的结构体
 *   list_for_each(pos, head)            - 遍历节点
 *   list_for_each_safe(pos, n, head)    - 安全遍历（可删除）
 *   list_for_each_entry(pos, head, member) - 遍历包含它的结构体
 */

#include <kernel/types.h>
#include <kernel/compiler.h>

struct list_head {
	struct list_head *prev;
	struct list_head *next;
};

#define LIST_HEAD_INIT(name) {&(name), &(name)}

#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

static __always_inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->prev = list;
	list->next = list;
}

static __always_inline void __list_add(struct list_head *new_node,
				       struct list_head *prev,
				       struct list_head *next)
{
	next->prev = new_node;
	new_node->next = next;
	new_node->prev = prev;
	prev->next = new_node;
}

static __always_inline void list_add(struct list_head *new_node,
				     struct list_head *head)
{
	__list_add(new_node, head, head->next);
}

static __always_inline void list_add_tail(struct list_head *new_node,
					  struct list_head *head)
{
	__list_add(new_node, head->prev, head);
}

static __always_inline void __list_del(struct list_head *prev,
				       struct list_head *next)
{
	next->prev = prev;
	prev->next = next;
}

static __always_inline void list_del(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	entry->prev = nullptr;
	entry->next = nullptr;
}

static __always_inline void list_del_init(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	INIT_LIST_HEAD(entry);
}

static __always_inline void list_move(struct list_head *list,
				      struct list_head *head)
{
	__list_del(list->prev, list->next);
	list_add(list, head);
}

static __always_inline void list_move_tail(struct list_head *list,
					   struct list_head *head)
{
	__list_del(list->prev, list->next);
	list_add_tail(list, head);
}

static __always_inline __must_check __pure bool
list_empty(const struct list_head *head)
{
	return head->next == head;
}

#define list_for_each(pos, head)                                               \
	for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_safe(pos, n, head)                                       \
	for (pos = (head)->next, n = pos->next; pos != (head);                 \
	     pos = n, n = pos->next)

#define list_entry(ptr, type, member)                                          \
	((type *)((char *)(ptr) - offsetof(type, member)))

#define list_first_entry(ptr, type, member)                                    \
	list_entry((ptr)->next, type, member)

#define list_for_each_entry(pos, head, member)                                 \
	for (pos = list_entry((head)->next, typeof(*pos), member);             \
	     &pos->member != (head);                                           \
	     pos = list_entry(pos->member.next, typeof(*pos), member))

#endif
