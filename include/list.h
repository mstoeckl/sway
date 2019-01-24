#ifndef _SWAY_LIST_H
#define _SWAY_LIST_H

#include <stdbool.h>

typedef struct {
	void **items;
	int capacity;
	int length;
} list_t;

list_t *create_list(void);
void list_free(list_t *list);

// Append item to list.
// On allocation failure, return false, and leave list unchanged
bool list_add(list_t *list, void *item);

// Insert an item. Afterwards, `item` will be at index `index`, shifting
// any displaced item to the right.
// On allocation failure, return false, and leave list unchanged
bool list_insert(list_t *list, int index, void *item);

void list_del(list_t *list, int index);

// Append list `source` after list `list`.
// On allocation failure, return false, and leave list unchanged
bool list_cat(list_t *list, const list_t *source);

// See qsort. Remember to use *_qsort functions as compare functions,
// because they dereference the left and right arguments first!
void list_qsort(list_t *list, int compare(const void *left, const void *right));

// Return index for first item in list that returns 0 for given compare
// function or -1 if none matches.
int list_seq_find(list_t *list, int compare(const void *item, const void *cmp_to), const void *cmp_to);
int list_find(list_t *list, const void *item);

// stable sort since qsort is not guaranteed to be stable
void list_stable_sort(list_t *list, int compare(const void *a, const void *b));

// swap two elements in a list
void list_swap(list_t *list, int src, int dest);

// move item to end of list
void list_move_to_end(list_t *list, void *item);

/* Calls `free` for each item in the list, then frees the list.
 * Do not use this to free lists of primitives or items that require more
 * complicated deallocation code.
 */
void list_free_items_and_destroy(list_t *list);
#endif
