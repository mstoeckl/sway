#include "jswrt.h"

#include <stdio.h>

static inline int jswrt_num_length(long v) {
    int j = v <= 0 ? 1 : 0;
    while (v) {
	j++;
	v /= 10;
    }
    return j;
}
static inline void jswrt_fmt_long(struct jswrt_state *state, long v) {
    long d0 = -1;
    long w = v;
    if (!v) {
	state->msg[state->length++] = '0';
	return;
    }
    long d = -1;
    while (w) {
	d = d0;
	w /= 10;
	d0 *= 10;
    }
    if (v < 0) {
	state->msg[state->length++] = '-';
    } else {
	v = -v;
    }
    while (d) {
	long c = v / d;
	state->msg[state->length++] = '0' + c;
	v -= c * d;
	d /= 10;
    }
}
static void jswrt_optional_comma(struct jswrt_state *state) {
	if (state->comma_follows) {
		if (state->msg) {
			state->msg[state->length++] = ',';
			state->msg[state->length++] = ' ';
		} else {
			state->length += 2;
		}
	}
}
void jswrt_object_open(struct jswrt_state *state) {
	jswrt_optional_comma(state);
	if (state->msg) {
		state->msg[state->length++] = '{';
		state->msg[state->length++] = ' ';
	} else {
		state->length += 2;
	}
	state->comma_follows = false;
}
void jswrt_object_close(struct jswrt_state *state) {
	if (state->msg) {
		state->msg[state->length++] = ' ';
		state->msg[state->length++] = '}';
	} else {
		state->length += 2;
	}
	state->comma_follows = true;
}
void jswrt_array_open(struct jswrt_state *state) {
	jswrt_optional_comma(state);
	if (state->msg) {
		state->msg[state->length++] = '[';
		state->msg[state->length++] = ' ';
	} else {
		state->length += 2;
	}
	state->comma_follows = false;
}
void jswrt_array_close(struct jswrt_state *state) {
	if (state->msg) {
		state->msg[state->length++] = ' ';
		state->msg[state->length++] = ']';
	} else {
		state->length += 2;
	}
	state->comma_follows = true;
}
void jswrt_bool(struct jswrt_state *state, bool value) {
	jswrt_optional_comma(state);
	if (state->msg) {
		if (value) {
			state->msg[state->length++] = 't';
			state->msg[state->length++] = 'r';
			state->msg[state->length++] = 'u';
			state->msg[state->length++] = 'e';
		} else{
			state->msg[state->length++] = 'f';
			state->msg[state->length++] = 'a';
			state->msg[state->length++] = 'l';
			state->msg[state->length++] = 's';
			state->msg[state->length++] = 'e';
		}
	} else {
		state->length += value ? 4 : 5;
	}
	state->comma_follows = true;
}
void jswrt_integer(struct jswrt_state *state, long value) {
	jswrt_optional_comma(state);
	if (state->msg) {
		jswrt_fmt_long(state, value);
	} else {
		state->length += jswrt_num_length(value);
	}
	state->comma_follows = true;
}
void jswrt_double(struct jswrt_state *state, double value) {
	jswrt_optional_comma(state);
	if (state->msg) {
		int l = sprintf(&state->msg[state->length], "%g", value);
		state->length += l;
	} else {
		state->length += snprintf(NULL, 0, "%g", value);
	}
	state->comma_follows = true;
}
static void jswrt_null(struct jswrt_state *state) {
	jswrt_optional_comma(state);
	if (state->msg) {
		state->msg[state->length++] = 'n';
		state->msg[state->length++] = 'u';
		state->msg[state->length++] = 'l';
		state->msg[state->length++] = 'l';
	} else {
		state->length += 4;
	}
	state->comma_follows = true;
}
void jswrt_string(struct jswrt_state *state, const char *value) {
	if (value == NULL) {
		jswrt_null(state);
		return;
	}

	jswrt_optional_comma(state);
	if (state->msg) {
		state->msg[state->length++] = '\"';
		for (const char *t = value; *t; t++) {
			// Only escape the bare minimum; esp. not unicode
			if (*t == '\n' || *t == '"' || *t == '\\') {
				state->msg[state->length++] = '\\';
			}
			if (*t == '\n') {
				state->msg[state->length++] = 'n';
			} else {
				state->msg[state->length++] = *t;
			}
		}
		state->msg[state->length++] = '\"';
	} else {
		state->length++;
		for (const char *t = value; *t; t++) {
			if (*t == '\n' || *t == '"' || *t == '\\') {
				state->length++;
			}
			state->length++;
		}
		state->length++;
	}
	state->comma_follows = true;
}

void jswrt_key(struct jswrt_state *state, const char *key) {
	jswrt_optional_comma(state);
	if (state->msg) {
		state->msg[state->length++] = '\"';
		while (*key) {
			state->msg[state->length++] = *key++;
		}
		state->msg[state->length++] = '\"';
		state->msg[state->length++] = ':';
		state->msg[state->length++] = ' ';
	} else {
		state->length += 4;
		while (*key) {
			state->length++;
			key++;
		}
	}
	state->comma_follows = false;
}
void jswrt_kv_bool(struct jswrt_state *state, const char *key, bool value) {
	jswrt_key(state, key);
	jswrt_bool(state, value);
}
void jswrt_kv_integer(struct jswrt_state *state, const char *key, long value) {
	jswrt_key(state, key);
	jswrt_integer(state, value);
}
void jswrt_kv_double(struct jswrt_state *state, const char *key, double value) {
	jswrt_key(state, key);
	jswrt_double(state, value);
}
void jswrt_kv_string(struct jswrt_state *state, const char *key, const char *value) {
	jswrt_key(state, key);
	jswrt_string(state, value);
}
void jswrt_kv_object_open(struct jswrt_state *state, const char *key) {
	jswrt_key(state, key);
	jswrt_object_open(state);
}
void jswrt_kv_array_open(struct jswrt_state *state, const char *key) {
	jswrt_key(state, key);
	jswrt_array_open(state);
}

void jswrt_null_terminate(struct jswrt_state *state) {
	if (state->msg) {
		state->msg[state->length] = '\0';
	}
	state->length++;
}

