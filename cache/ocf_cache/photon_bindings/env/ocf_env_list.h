/*
Copyright 2022 The Photon Authors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef __OCF_ENV_LIST__
#define __OCF_ENV_LIST__

#define LIST_POISON1  ((void *)0x101)
#define LIST_POISON2  ((void *)0x202)

/**
 * List entry structure mimicking linux kernel based one.
 */
struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

/**
 * start an empty list
 */
#define INIT_LIST_HEAD(l) { (l)->prev = l; (l)->next = l; }

/**
 * Add item to list head.
 * @param it list entry to be added
 * @param l1 list main node (head)
 */
static inline void list_add(struct list_head *it, struct list_head *l1)
{
	it->prev = l1;
	it->next = l1->next;

	l1->next->prev = it;
	l1->next = it;
}

/**
 * Add item it to tail.
 * @param it list entry to be added
 * @param l1 list main node (head)
 */
static inline void list_add_tail(struct list_head *it, struct list_head *l1)
{
	it->prev = l1->prev;
	it->next = l1;

	l1->prev->next = it;
	l1->prev = it;
}

/**
 * check if a list is empty (return true)
 * @param l1 list main node (head)
 */
static inline int list_empty(struct list_head *l1)
{
	return l1->next == l1;
}

/**
 * delete an entry from a list
 * @param it list entry to be deleted
 */
static inline void list_del(struct list_head *it)
{
	it->next->prev = it->prev;
	it->prev->next = it->next;
}

/**
 * Move element to list head.
 * @param it list entry to be moved
 * @param l1 list main node (head)
 */
static inline void list_move(struct list_head *it, struct list_head *l1)
{
	list_del(it);
	list_add(it, l1);
}

/**
 * Move element to list tail.
 * @param it list entry to be moved
 * @param l1 list main node (head)
 */
static inline void list_move_tail(struct list_head *it, struct list_head *l1)
{
	list_del(it);
	list_add_tail(it, l1);
}

/**
 * Extract an entry.
 * @param list_head_i list head item, from which entry is extracted
 * @param item_type type (struct) of list entry
 * @param field_name name of list_head field within item_type
 */
#define list_entry(list_head_i, item_type, field_name) \
	(item_type *)(((void*)(list_head_i)) - offsetof(item_type, field_name))

#define list_first_entry(list_head_i, item_type, field_name) \
	list_entry((list_head_i)->next, item_type, field_name)

/**
 * @param iterator uninitialized list_head pointer, to be used as iterator
 * @param plist list head (main node)
 */
#define list_for_each(iterator, plist) \
	for (iterator = (plist)->next; \
	     (iterator)->next != (plist)->next; \
	     iterator = (iterator)->next)

/**
 * Safe version of list_for_each which works even if entries are deleted during
 * loop.
 * @param iterator uninitialized list_head pointer, to be used as iterator
 * @param q another uninitialized list_head, used as helper
 * @param plist list head (main node)
 */
/*
 * Algorithm handles situation, where q is deleted.
 * consider in example 3 element list with header h:
 *
 *   h -> 1 -> 2 -> 3 ->
 *1.      i    q
 *
 *2.           i    q
 *
 *3. q              i
 */
#define list_for_each_safe(iterator, q, plist) \
	for (iterator = (q = (plist)->next->next)->prev; \
	     (q) != (plist)->next; \
	     iterator = (q = (q)->next)->prev)

#define _list_entry_helper(item, head, field_name) \
		list_entry(head, typeof(*item), field_name)

/**
 * Iterate over list entries.
 * @param list pointer to list item (iterator)
 * @param plist pointer to list_head item
 * @param field_name name of list_head field in list entry
 */
#define list_for_each_entry(item, plist, field_name) \
	for (item = _list_entry_helper(item, (plist)->next, field_name); \
	     _list_entry_helper(item, (item)->field_name.next, field_name) !=\
		     _list_entry_helper(item, (plist)->next, field_name); \
	     item = _list_entry_helper(item, (item)->field_name.next, field_name))

/**
 * Safe version of list_for_each_entry which works even if entries are deleted
 * during loop.
 * @param list pointer to list item (iterator)
 * @param q another pointer to list item, used as helper
 * @param plist pointer to list_head item
 * @param field_name name of list_head field in list entry
 */
#define list_for_each_entry_safe(item, q, plist, field_name)		\
	for (item = _list_entry_helper(item, (plist)->next, field_name), \
	     q = _list_entry_helper(item, (item)->field_name.next, field_name); \
	     _list_entry_helper(item, (item)->field_name.next, field_name) != \
		     _list_entry_helper(item, (plist)->next, field_name); \
	     item = q, q = _list_entry_helper(q, (q)->field_name.next, field_name))

#endif // __OCF_ENV_LIST__
