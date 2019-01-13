#ifndef _SWAY_JSWRT_H
#define _SWAY_JSWRT_H

#include <stdbool.h>

/**
  * Mini-library for efficient JSON serialization.
  * (NOT for deserialization, as that's significantly more complicated)
  *
  * Helper functions can be used in a two pass algorithm. The first pass
  * estimates the length of the string needed; the second pass actually
  * serializes a given structure.
  *
  * The output is not pretty printed, although spaces are inserted to improve
  * legibility.
  *
  * Recommended use:
  *
  * 	struct jswrt_state state1 = { .msg = NULL, .length = 0, .comma_follows = false };
  * 	struct jswrt_state state2 = { .msg = NULL, .length = 0, .comma_follows = false };
  * 	example_unterminated_func(&state1, arguments);
  * 	state2.msg = calloc(state1.length + 1, 1);
  * 	if (state2.msg) {
  * 		example_unterminated_func(&state2, arguments);
  * 		jswrt_null_terminate(&state2);
  * 		...
  * 	}
  * 	free(state2.msg);
  *
  * ^ wherein `example_unterminated_func` uses the jswrt_* functions. Ex:
  *
  * 	jswrt_array_open(s);
  * 	jswrt_object_open(s);
  * 	jswrt_kv_string(s, "id", "example\nstring: \"escapes\"");
  * 	jswrt_key(s, "thingA");
  * 	custom_object_deserialize(s);
  * 	jswrt_kv_object_open(s, "sub_object");
  * 	jswrt_key(s, "subthingC");
  * 	custom_object_deserialize(s);
  * 	jswrt_key(s, "subthingD");
  * 	custom_object_deserialize(s);
  * 	jswrt_object_close(s);
  * 	jswrt_object_close(s);
  * 	jswrt_array_close(s);
  *
  * The provided state->msg buffer should always have room for a null
  * terminator, as e.g. jswrt_double may overwrite by one character.
  */
struct jswrt_state {
	char* msg;
	int length;
	bool comma_follows;
};

/** These functions should only be called at top level,
 *  or inside an array, or following `jswrt_key`. */
void jswrt_object_open(struct jswrt_state *state);
void jswrt_object_close(struct jswrt_state *state);
void jswrt_array_open(struct jswrt_state *state);
void jswrt_array_close(struct jswrt_state *state);
void jswrt_bool(struct jswrt_state *state, bool value);
void jswrt_integer(struct jswrt_state *state, long value);
void jswrt_double(struct jswrt_state *state, double value);
void jswrt_string(struct jswrt_state *state, const char *value);

/** These functions should only be called inside an object. */
void jswrt_key(struct jswrt_state *state, const char *key);
/**  Eqvt. to calling `jswrt_key` followed by `jswrt_X`  */
void jswrt_kv_bool(struct jswrt_state *state, const char *key, bool value);
void jswrt_kv_integer(struct jswrt_state *state, const char *key, long value);
void jswrt_kv_double(struct jswrt_state *state, const char *key, double value);
void jswrt_kv_string(struct jswrt_state *state, const char *key, const char *value);
void jswrt_kv_object_open(struct jswrt_state *state, const char *key);
void jswrt_kv_array_open(struct jswrt_state *state, const char *key);

/** Call to end the string. */
void jswrt_null_terminate(struct jswrt_state *state);

#endif
