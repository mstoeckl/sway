#include "list.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"

list_t *create_list(void) {
	list_t *list = malloc(sizeof(list_t));
	if (!list) {
		sway_log(SWAY_ERROR, "Unable to allocate memory for list");
		return NULL;
	}
	list->capacity = 10;
	list->length = 0;
	list->items = malloc(sizeof(void*) * list->capacity);
	if (!list->items) {
		free(list);
		sway_log(SWAY_ERROR, "Unable to allocate memory for list");
		return NULL;
	}
	return list;
}

static bool list_resize(list_t *list) {
	if (list->length == list->capacity) {
		void *items = realloc(list->items, sizeof(void *) * list->capacity * 2);
		if (!items) {
			sway_log(SWAY_ERROR, "Unable to allocate memory to resize list");
			return false;
		}
		list->capacity *= 2;
		list->items = items;
	}
	return true;
}

void list_free(list_t *list) {
	if (list == NULL) {
		return;
	}
	free(list->items);
	free(list);
}

bool list_add(list_t *list, void *item) {
	bool success = list_resize(list);
	if (success) {
		list->items[list->length++] = item;
	}
	return success;
}

bool list_insert(list_t *list, int index, void *item) {
	bool success = list_resize(list);
	if (success) {
		memmove(&list->items[index + 1], &list->items[index], sizeof(void*) * (list->length - index));
		list->length++;
		list->items[index] = item;
	}
	return success;
}

void list_del(list_t *list, int index) {
	list->length--;
	memmove(&list->items[index], &list->items[index + 1], sizeof(void*) * (list->length - index));
}

bool list_cat(list_t *list, const list_t *source) {
	if (list->length + source->length > list->capacity) {
		// Try to resize to fit both lists
		int max_cap = list->capacity > source->capacity ?
			list->capacity : source->capacity;
		if (max_cap < list->length + source->length) {
			max_cap *= 2;
		}
		void *items = realloc(list->items, sizeof(void *) * max_cap);
		if (!items) {
			sway_log(SWAY_ERROR, "Unable to allocate memory to resize list");
			return false;
		}
		list->capacity = max_cap;
		list->items = items;
	}

	for (int i = 0; i < source->length; ++i) {
		list->items[list->length++] = source->items[i];
	}
	return true;
}

void list_qsort(list_t *list, int compare(const void *left, const void *right)) {
	qsort(list->items, list->length, sizeof(void *), compare);
}

int list_seq_find(list_t *list, int compare(const void *item, const void *data), const void *data) {
	for (int i = 0; i < list->length; i++) {
		void *item = list->items[i];
		if (compare(item, data) == 0) {
			return i;
		}
	}
	return -1;
}

int list_find(list_t *list, const void *item) {
	for (int i = 0; i < list->length; i++) {
		if (list->items[i] == item) {
			return i;
		}
	}
	return -1;
}

void list_swap(list_t *list, int src, int dest) {
	void *tmp = list->items[src];
	list->items[src] = list->items[dest];
	list->items[dest] = tmp;
}

void list_move_to_end(list_t *list, void *item) {
	int i;
	for (i = 0; i < list->length; ++i) {
		if (list->items[i] == item) {
			break;
		}
	}
	if (!sway_assert(i < list->length, "Item not found in list")) {
		return;
	}
	memmove(&list->items[i], &list->items[i + 1], sizeof(void*) * (list->length - i - 1));
	list->items[list->length - 1] = item;
}

static void list_rotate(list_t *list, int from, int to) {
	void *tmp = list->items[to];

	while (to > from) {
		list->items[to] = list->items[to - 1];
		to--;
	}

	list->items[from] = tmp;
}

static void list_inplace_merge(list_t *list, int left, int last, int mid, int compare(const void *a, const void *b)) {
	int right = mid + 1;

	if (compare(&list->items[mid], &list->items[right]) <= 0) {
		return;
	}

	while (left <= mid && right <= last) {
		if (compare(&list->items[left], &list->items[right]) <= 0) {
			left++;
		} else {
			list_rotate(list, left, right);
			left++;
			mid++;
			right++;
		}
	}
}

static void list_inplace_sort(list_t *list, int first, int last, int compare(const void *a, const void *b)) {
	if (first >= last) {
		return;
	} else if ((last - first) == 1) {
		if (compare(&list->items[first], &list->items[last]) > 0) {
			list_swap(list, first, last);
		}
	} else {
		int mid = (int)((last + first) / 2);
		list_inplace_sort(list, first, mid, compare);
		list_inplace_sort(list, mid + 1, last, compare);
		list_inplace_merge(list, first, last, mid, compare);
	}
}

void list_stable_sort(list_t *list, int compare(const void *a, const void *b)) {
	if (list->length > 1) {
		list_inplace_sort(list, 0, list->length - 1, compare);
	}
}

void list_free_items_and_destroy(list_t *list) {
	if (!list) {
		return;
	}

	for (int i = 0; i < list->length; ++i) {
		free(list->items[i]);
	}
	list_free(list);
}

