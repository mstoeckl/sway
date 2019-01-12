#include "jswrt.h"

#include <stdio.h>

static inline void jswrt_char(struct jswrt_state *state, char c) {
	if (state->msg) {
		state->msg[state->length++] = c;
	} else {
		state->length++;
	}
}
static void jswrt_optional_comma(struct jswrt_state *state) {
	if (state->comma_follows) {
		jswrt_char(state, ',');
		jswrt_char(state, ' ');
	}
}
static void jswrt_copy(struct jswrt_state *state, const char *str) {
	while (*str) {
		jswrt_char(state, *str++);
	}
}

void jswrt_object_open(struct jswrt_state *state) {
	jswrt_optional_comma(state);
	jswrt_char(state, '{');
	jswrt_char(state, ' ');
	state->comma_follows = false;
}
void jswrt_object_close(struct jswrt_state *state) {
	jswrt_char(state, ' ');
	jswrt_char(state, '}');
	state->comma_follows = true;
}
void jswrt_array_open(struct jswrt_state *state) {
	jswrt_optional_comma(state);
	jswrt_char(state, '[');
	jswrt_char(state, ' ');
	state->comma_follows = false;
}
void jswrt_array_close(struct jswrt_state *state) {
	jswrt_char(state, ' ');
	jswrt_char(state, ']');
	state->comma_follows = true;
}
void jswrt_bool(struct jswrt_state *state, bool value) {
	jswrt_optional_comma(state);
	if (value) {
		jswrt_copy(state, "true");
	} else {
		jswrt_copy(state, "false");
	}
	state->comma_follows = true;
}
void jswrt_integer(struct jswrt_state *state, long value) {
	jswrt_optional_comma(state);
	char buf[21];
	sprintf(buf, "%ld", value);
	jswrt_copy(state, buf);
	state->comma_follows = true;
}
void jswrt_string(struct jswrt_state *state, const char *value) {
	jswrt_optional_comma(state);
	if (value == NULL) {
		jswrt_copy(state, "null");
		state->comma_follows = true;
		return;
	}

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
	jswrt_char(state, '\"');
	jswrt_copy(state, key);
	jswrt_char(state, '\"');
	jswrt_char(state, ':');
	jswrt_char(state, ' ');
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
	jswrt_char(state, '\0');
}

