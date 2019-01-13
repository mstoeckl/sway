#ifndef _SWAY_IPC_JSON_H
#define _SWAY_IPC_JSON_H
#include "sway/tree/container.h"
#include "sway/input/input-manager.h"

struct jswrt_state;

void ipc_json_describe_node_recursive_2(struct jswrt_state *s, struct sway_node *node);
void ipc_json_get_version_2(struct jswrt_state *s);
void ipc_json_get_binding_modes_2(struct jswrt_state *s);
void ipc_json_describe_input_2(struct jswrt_state *s, const struct sway_input_device *device);
void ipc_json_get_inputs_2(struct jswrt_state *s);
void ipc_json_list_bars_2(struct jswrt_state *s);
void ipc_json_describe_bar_config_2(struct jswrt_state *s, const struct bar_config *bar);
void ipc_json_get_seats_2(struct jswrt_state *s);
void ipc_json_get_marks_2(struct jswrt_state *s);
void ipc_json_get_config_2(struct jswrt_state *s);
void ipc_json_get_workspaces_2(struct jswrt_state *s);
void ipc_json_get_outputs_2(struct jswrt_state *s);
void ipc_json_event_workspace_2(struct jswrt_state *s, struct sway_workspace *old,
	struct sway_workspace *new, const char *change);
void ipc_json_event_window_2(struct jswrt_state *s, struct sway_container *window,
	const char *change);
#endif
