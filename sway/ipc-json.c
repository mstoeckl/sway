#include <json-c/json.h>
#include <stdio.h>
#include <ctype.h>
#include "config.h"
#include "log.h"
#include "jswrt.h"
#include "sway/config.h"
#include "sway/ipc-json.h"
#include "sway/tree/container.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "sway/output.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output.h>
#include <xkbcommon/xkbcommon.h>
#include "wlr-layer-shell-unstable-v1-protocol.h"

static const int i3_output_id = INT32_MAX;
static const int i3_scratch_id = INT32_MAX - 1;

static const char *ipc_json_layout_description(enum sway_container_layout l) {
	switch (l) {
	case L_VERT:
		return "splitv";
	case L_HORIZ:
		return "splith";
	case L_TABBED:
		return "tabbed";
	case L_STACKED:
		return "stacked";
	case L_NONE:
		break;
	}
	return "none";
}

static const char *ipc_json_orientation_description(enum sway_container_layout l) {
	switch (l) {
	case L_VERT:
		return "vertical";
	case L_HORIZ:
		return "horizontal";
	default:
		return "none";
	}
}

static const char *ipc_json_border_description(enum sway_container_border border) {
	switch (border) {
	case B_NONE:
		return "none";
	case B_PIXEL:
		return "pixel";
	case B_NORMAL:
		return "normal";
	case B_CSD:
		return "csd";
	}
	return "unknown";
}

static const char *ipc_json_output_transform_description(enum wl_output_transform transform) {
	switch (transform) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
		return "normal";
	case WL_OUTPUT_TRANSFORM_90:
		return "90";
	case WL_OUTPUT_TRANSFORM_180:
		return "180";
	case WL_OUTPUT_TRANSFORM_270:
		return "270";
	case WL_OUTPUT_TRANSFORM_FLIPPED:
		return "flipped";
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
		return "flipped-90";
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
		return "flipped-180";
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		return "flipped-270";
	}
	return NULL;
}

static const char *ipc_json_device_type_description(const struct sway_input_device *device) {
	switch (device->wlr_device->type) {
	case WLR_INPUT_DEVICE_POINTER:
		return "pointer";
	case WLR_INPUT_DEVICE_KEYBOARD:
		return "keyboard";
	case WLR_INPUT_DEVICE_TOUCH:
		return "touch";
	case WLR_INPUT_DEVICE_TABLET_TOOL:
		return "tablet_tool";
	case WLR_INPUT_DEVICE_TABLET_PAD:
		return "tablet_pad";
	case WLR_INPUT_DEVICE_SWITCH:
		return "switch";
	}
	return "unknown";
}

void ipc_json_get_version_2(struct jswrt_state *s) {
	int major = 0, minor = 0, patch = 0;
	sscanf(SWAY_VERSION, "%u.%u.%u", &major, &minor, &patch);

	jswrt_object_open(s);
	jswrt_kv_string(s, "human_readable", SWAY_VERSION);
	jswrt_kv_string(s, "variant", "sway");
	jswrt_kv_integer(s, "major", major);
	jswrt_kv_integer(s, "minor", minor);
	jswrt_kv_integer(s, "patch", patch);
	jswrt_kv_string(s,  "loaded_config_file_name", config->current_config_path);
	jswrt_object_close(s);
}

void ipc_json_get_binding_modes_2(struct jswrt_state* s) {
	jswrt_array_open(s);
	for (int i = 0; i < config->modes->length; i++) {
		struct sway_mode *mode = config->modes->items[i];
		jswrt_string(s, mode->name);
	}
	jswrt_array_close(s);
}

void ipc_json_get_inputs_2(struct jswrt_state* s) {
	jswrt_array_open(s);

	struct sway_input_device *device = NULL;
	wl_list_for_each(device, &server.input->devices, link) {
		ipc_json_describe_input_2(s, device);
	}

	jswrt_array_close(s);
}

static void ipc_json_create_rect_2(struct jswrt_state *s, const struct wlr_box *box) {
	jswrt_object_open(s);
	jswrt_kv_integer(s, "x", box->x);
	jswrt_kv_integer(s, "y", box->y);
	jswrt_kv_integer(s, "width", box->width);
	jswrt_kv_integer(s, "height", box->height);
	jswrt_object_close(s);

}
static void ipc_json_create_empty_rect_2(struct jswrt_state *s) {
	struct wlr_box empty = {0, 0, 0, 0};
	ipc_json_create_rect_2(s, &empty);
}
static void ipc_json_describe_root_2(struct jswrt_state *s, const struct sway_root *root) {
	jswrt_kv_string(s, "type", "root");

	// i3 filler (?)
	jswrt_key(s, "geometry");
	ipc_json_create_empty_rect_2(s);
	// todo etc.
}
static void ipc_json_describe_output_2(struct jswrt_state *s, struct sway_output *output) {
	struct wlr_output *wlr_output = output->wlr_output;
	jswrt_kv_string(s, "type", "output");
	jswrt_kv_bool(s, "active", true);
	jswrt_kv_bool(s, "primary", false);
	jswrt_kv_string(s, "layout", "output");
	jswrt_kv_string(s, "orientation", ipc_json_orientation_description(L_NONE));
	jswrt_kv_string(s, "make", wlr_output->make);
	jswrt_kv_string(s, "model", wlr_output->model);
	jswrt_kv_string(s, "serial", wlr_output->serial);
	jswrt_kv_double(s, "scale", wlr_output->scale);
	jswrt_kv_string(s, "transform", ipc_json_output_transform_description(wlr_output->transform));

	struct sway_workspace *ws = output_get_active_workspace(output);
	jswrt_kv_string(s, "current_workspace", ws->name);

	jswrt_kv_array_open(s, "modes");
	struct wlr_output_mode *mode;
	wl_list_for_each(mode, &wlr_output->modes, link) {
		jswrt_object_open(s);
		jswrt_kv_integer(s, "width", mode->width);
		jswrt_kv_integer(s, "height", mode->height);
		jswrt_kv_integer(s, "refresh", mode->refresh);
		jswrt_object_close(s);
	}
	jswrt_array_close(s);

	jswrt_kv_object_open(s, "current_mode");
	jswrt_kv_integer(s, "width", wlr_output->width);
	jswrt_kv_integer(s, "height", wlr_output->height);
	jswrt_kv_integer(s, "refresh", wlr_output->refresh);
	jswrt_object_close(s);

	struct sway_node *parent = node_get_parent(&output->node);
	struct wlr_box parent_box = {0, 0, 0, 0};

	if (parent != NULL) {
		node_get_box(parent, &parent_box);
	}

	if (parent_box.width != 0 && parent_box.height != 0) {
		double percent = ((double)output->width / parent_box.width)
				* ((double)output->height / parent_box.height);
		jswrt_kv_double(s, "percent", percent);
	} else {
		jswrt_kv_string(s, "percent", NULL);
	}
}


static void ipc_json_describe_scratchpad_output_2(struct jswrt_state *s) {
	struct wlr_box box;
	root_get_box(root, &box);

	// Create focus stack for __i3 output
	jswrt_object_open(s);
	jswrt_kv_integer(s, "id", i3_output_id);
	jswrt_kv_string(s, "name", "__i3");
	jswrt_key(s, "rect");
	ipc_json_create_rect_2(s, &box);
	jswrt_kv_bool(s, "focused", false);
	jswrt_kv_array_open(s, "focus");
	jswrt_integer(s, i3_scratch_id);
	jswrt_array_close(s);

	jswrt_kv_array_open(s, "nodes");
	jswrt_object_open(s);
	// workspace!

	jswrt_kv_integer(s, "id", i3_scratch_id);
	jswrt_kv_string(s, "name", "__i3_scratch");
	jswrt_key(s, "rect");
	ipc_json_create_rect_2(s, &box);
	jswrt_kv_bool(s, "focused", false);
	jswrt_kv_array_open(s, "focus");
	for (int i = root->scratchpad->length - 1; i >= 0; --i) {
		struct sway_container *container = root->scratchpad->items[i];
		jswrt_integer(s, container->node.id);
	}
	jswrt_array_close(s);
	jswrt_kv_string(s, "type", "workspace");

	// List all hidden scratchpad containers as floating nodes
	jswrt_kv_array_open(s, "floating_nodes");
	for (int i = 0; i < root->scratchpad->length; ++i) {
		struct sway_container *container = root->scratchpad->items[i];
		if (!container->workspace) {
			ipc_json_describe_node_recursive_2(s, &container->node);
		}
	}
	jswrt_array_close(s);
	jswrt_object_close(s);
	jswrt_array_close(s);
	jswrt_object_close(s);
}


static void ipc_json_describe_view_2(struct jswrt_state *s, struct sway_container *c) {
	jswrt_kv_integer(s, "pid", c->view->pid);

	const char *app_id = view_get_app_id(c->view);
	jswrt_kv_string(s, "app_id", app_id);

	jswrt_kv_array_open(s, "marks");
	list_t *con_marks = c->marks;
	for (int i = 0; i < con_marks->length; ++i) {
		jswrt_string(s, con_marks->items[i]);
	}
	jswrt_array_close(s);

	struct wlr_box window_box = {
		c->content_x - c->x,
		(c->current.border == B_PIXEL) ? c->current.border_thickness : 0,
		c->content_width,
		c->content_height
	};

	jswrt_key(s, "window_rect");
	ipc_json_create_rect_2(s, &window_box);

	struct wlr_box deco_box = {0, 0, 0, 0};

	if (c->current.border == B_NORMAL) {
		deco_box.width = c->width;
		deco_box.height = c->content_y - c->y;
	}

	jswrt_key(s, "deco_rect");
	ipc_json_create_rect_2(s, &deco_box);

	struct wlr_box geometry = {0, 0, c->view->natural_width, c->view->natural_height};
	jswrt_key(s, "geometry");
	ipc_json_create_rect_2(s, &geometry);

#if HAVE_XWAYLAND
	if (c->view->type == SWAY_VIEW_XWAYLAND) {
		jswrt_kv_integer(s, "window", view_get_x11_window_id(c->view));

		jswrt_kv_object_open(s, "window_properties");

		const char *class = view_get_class(c->view);
		if (class) {
			jswrt_kv_string(s, "class", class);
		}
		const char *instance = view_get_instance(c->view);
		if (instance) {
			jswrt_kv_string(s, "instance", instance);
		}
		if (c->title) {
			jswrt_kv_string(s, "title", c->title);
		}

		// the transient_for key is always present in i3's output
		uint32_t parent_id = view_get_x11_parent_id(c->view);
		if (parent_id) {
			jswrt_kv_integer(s, "transient_for", parent_id);
		} else {
			jswrt_kv_string(s, "transient_for", NULL);
		}

		const char *role = view_get_window_role(c->view);
		if (role) {
			jswrt_kv_string(s, "window_role", role);
		}

		jswrt_object_close(s);
	}
#endif
}

static void ipc_json_describe_container_2(struct jswrt_state *s, struct sway_container *c) {
	jswrt_kv_string(s, "name", c->title);
	jswrt_kv_string(s, "type", container_is_floating(c) ? "floating_con" : "con");
	jswrt_kv_string(s, "layout", ipc_json_layout_description(c->layout));
	jswrt_kv_string(s, "orientation", ipc_json_orientation_description(c->layout));

	bool urgent = c->view ?
		view_is_urgent(c->view) : container_has_urgent_child(c);
	jswrt_kv_bool(s, "urgent", urgent);
	jswrt_kv_bool(s, "sticky", c->is_sticky);
	jswrt_kv_integer(s, "fullscreen_mode", c->is_fullscreen);

	struct sway_node *parent = node_get_parent(&c->node);
	struct wlr_box parent_box = {0, 0, 0, 0};

	if (parent != NULL) {
		node_get_box(parent, &parent_box);
	}

	if (parent_box.width != 0 && parent_box.height != 0) {
		double percent = ((double)c->width / parent_box.width)
				* ((double)c->height / parent_box.height);
		jswrt_kv_double(s, "percent", percent);
	} else {
		jswrt_kv_string(s, "percent", NULL);
	}

	jswrt_kv_string(s, "border", ipc_json_border_description(c->current.border));
	jswrt_kv_integer(s, "current_border_width", c->current.border_thickness);
	jswrt_kv_array_open(s, "floating_nodes");
	jswrt_array_close(s);

	if (c->view) {
		ipc_json_describe_view_2(s, c);
	}
}

static void ipc_json_describe_workspace_2(struct jswrt_state *s, struct sway_workspace *workspace) {
	int num = isdigit(workspace->name[0]) ? atoi(workspace->name) : -1;

	jswrt_kv_integer(s, "num", num);
	jswrt_kv_string(s, "output", workspace->output ? workspace->output->wlr_output->name : NULL);
	jswrt_kv_string(s, "type", "workspace");
	jswrt_kv_bool(s, "urgent", workspace->urgent);
	jswrt_kv_string(s, "representation", workspace->representation);

	jswrt_kv_string(s, "layout", ipc_json_layout_description(workspace->layout));
	jswrt_kv_string(s, "orientation", ipc_json_orientation_description(workspace->layout));

	// Floating
	jswrt_kv_array_open(s, "floating_nodes");
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct sway_container *floater = workspace->floating->items[i];
		ipc_json_describe_node_recursive_2(s, &floater->node);
	}
	jswrt_array_close(s);
}

struct focus_inactive_test_data_2 {
	size_t id;
	bool duplicate;
	struct sway_node *node;
	int nleft_to_visit;
};

static void focus_inactive_children_test_iterator_2(struct sway_node *node,
		void *_data) {
	struct focus_inactive_test_data_2 *data = _data;
	if (data->nleft_to_visit <= 0) {
		return;
	}
	data->nleft_to_visit--;

	if (data->node == &root->node) {
		struct sway_output *output = node_get_output(node);
		if (output == NULL) {
			return;
		}
		if (output->node.id == data->id) {
			data->duplicate = true;
		}
	} else if (node_get_parent(node) != data->node) {
		return;
	} else {
		if (node->id == data->id) {
			data->duplicate = true;
		}
	}

}

struct focus_inactive_data_2 {
	struct sway_node *node;
	struct jswrt_state *state;
	int nvisited;
};

static void focus_inactive_children_iterator_2(struct sway_node *node,
		void *_data) {
	struct focus_inactive_data_2 *data = _data;
	data->nvisited++;
	// TODO: distinct iterator for this case
	if (data->node == &root->node) {
		struct sway_output *output = node_get_output(node);
		if (output == NULL) {
			return;
		}

		struct sway_seat *seat = input_manager_get_default_seat();
		size_t id = output->node.id;
		struct focus_inactive_test_data_2 tst =
			{ .id = id, .duplicate = false,
			.node = data->node, .nleft_to_visit = data->nvisited - 1 };
		seat_for_each_node(seat, focus_inactive_children_test_iterator_2, &tst);
		if (tst.duplicate) {
			return;
		}
		jswrt_integer(data->state, id);
	} else if (node_get_parent(node) != data->node) {
		return;
	} else {
		jswrt_integer(data->state, node->id);
	}
}

void ipc_json_start_describe_node_2(struct jswrt_state *s, struct sway_node *node, bool focused) {
	struct sway_seat *seat = input_manager_get_default_seat();
	char *name = node_get_name(node);

	struct wlr_box box;
	node_get_box(node, &box);

	// previously, had 'ipc_json_create_node', and edited properties after the
	// fact. In write-once world, nyet.
	jswrt_object_open(s);
	jswrt_kv_integer(s, "id", (int)node->id);
	jswrt_kv_string(s, "name", name);
	jswrt_key(s, "rect");
	ipc_json_create_rect_2(s, &box);
	jswrt_kv_bool(s, "focused", focused);
	jswrt_kv_array_open(s, "focus");

	struct focus_inactive_data_2 data = {
		.node = node,
		.state = s,
		.nvisited = 0,
	};
	seat_for_each_node(seat, focus_inactive_children_iterator_2, &data);
	jswrt_array_close(s);

	// What follows should also have default values ??

	switch (node->type) {
	case N_ROOT:
		ipc_json_describe_root_2(s, root);
		break;
	case N_OUTPUT:
		ipc_json_describe_output_2(s, node->sway_output);
		break;
	case N_CONTAINER:
		ipc_json_describe_container_2(s, node->sway_container);
		break;
	case N_WORKSPACE:
		ipc_json_describe_workspace_2(s, node->sway_workspace);
		break;
	}

	// We do *NOT* close the open object; that is caller's duty
}

void ipc_json_describe_node_recursive_2(struct jswrt_state *s, struct sway_node *node) {
	// TODO: add 'focused' and 'visible' fields / override via const bool pointer, either NULL or real
	// TODO: visible details ??
	struct sway_seat *seat = input_manager_get_default_seat();
	bool focused = seat_get_focus(seat) == node;
	ipc_json_start_describe_node_2(s, node, focused);

	jswrt_kv_array_open(s, "nodes");
	switch (node->type) {
	case N_ROOT:
		ipc_json_describe_scratchpad_output_2(s);
		for (int i = 0; i < root->outputs->length; ++i) {
			struct sway_output *output = root->outputs->items[i];
			ipc_json_describe_node_recursive_2(s, &output->node);
		}
		break;
	case N_OUTPUT:
		for (int i = 0; i < node->sway_output->workspaces->length; ++i) {
			struct sway_workspace *ws = node->sway_output->workspaces->items[i];
			ipc_json_describe_node_recursive_2(s, &ws->node);
		}
		break;
	case N_WORKSPACE:
		for (int i = 0; i < node->sway_workspace->tiling->length; ++i) {
			struct sway_container *con = node->sway_workspace->tiling->items[i];
			ipc_json_describe_node_recursive_2(s, &con->node);
		}
		break;
	case N_CONTAINER:
		if (node->sway_container->children) {
			for (int i = 0; i < node->sway_container->children->length; ++i) {
				struct sway_container *child =
					node->sway_container->children->items[i];
				ipc_json_describe_node_recursive_2(s, &child->node);
			}
		}
		break;
	}
	jswrt_array_close(s);

	jswrt_object_close(s); // corresponding to 'start_describe'
}

void ipc_json_describe_input_2(struct jswrt_state *s, const struct sway_input_device *device) {
	if (!(sway_assert(device, "Device must not be null"))) {
		return;
	}

	jswrt_object_open(s);
	jswrt_kv_string(s, "identifier", device->identifier);
	jswrt_kv_string(s, "name", device->wlr_device->name);
	jswrt_kv_integer(s, "vendor", device->wlr_device->vendor);
	jswrt_kv_integer(s, "product", device->wlr_device->product);
	jswrt_kv_string(s, "type", ipc_json_device_type_description(device));

	if (device->wlr_device->type == WLR_INPUT_DEVICE_KEYBOARD) {
		struct wlr_keyboard *keyboard = device->wlr_device->keyboard;
		struct xkb_keymap *keymap = keyboard->keymap;
		struct xkb_state *state = keyboard->xkb_state;
		xkb_layout_index_t num_layouts = xkb_keymap_num_layouts(keymap);
		xkb_layout_index_t layout_idx;
		for (layout_idx = 0; layout_idx < num_layouts; layout_idx++) {
			bool is_active =
				xkb_state_layout_index_is_active(state,
						layout_idx,
						XKB_STATE_LAYOUT_EFFECTIVE);
			if (is_active) {
				const char *layout =
					xkb_keymap_layout_get_name(keymap, layout_idx);

				jswrt_kv_string(s, "xkb_active_layout_name", layout);
				break;
			}
		}
	}

	jswrt_object_close(s);
}


void ipc_json_list_bars_2(struct jswrt_state *s) {
	jswrt_array_open(s);
	for (int i = 0; i < config->bars->length; ++i) {
		struct bar_config *bar = config->bars->items[i];
		jswrt_string(s, bar->id);
	}
	jswrt_array_close(s);
}
void ipc_json_describe_bar_config_2(struct jswrt_state *s, const struct bar_config *bar) {
	if (!sway_assert(bar, "Bar must not be NULL")) {
		return;
	}

	jswrt_object_open(s);
	jswrt_kv_string(s, "id", bar->id);
	jswrt_kv_string(s, "mode", bar->mode);
	jswrt_kv_string(s, "hidden_state", bar->hidden_state);
	jswrt_kv_string(s, "position", bar->position);
	jswrt_kv_string(s, "status_command", bar->status_command);
	jswrt_kv_string(s, "font", bar->font);

	jswrt_kv_object_open(s, "gaps");
	jswrt_kv_integer(s, "top", bar->gaps.top);
	jswrt_kv_integer(s, "right", bar->gaps.right);
	jswrt_kv_integer(s, "bottom", bar->gaps.bottom);
	jswrt_kv_integer(s, "left", bar->gaps.left);
	jswrt_object_close(s);

	if (bar->separator_symbol) {
		jswrt_kv_string(s, "separator_symbol", bar->separator_symbol);
	}
	jswrt_kv_integer(s, "bar_height", bar->height);
	jswrt_kv_bool(s, "wrap_scroll", bar->wrap_scroll);
	jswrt_kv_bool(s, "workspace_buttons", bar->workspace_buttons);
	jswrt_kv_bool(s, "strip_workspace_numbers", bar->strip_workspace_numbers);
	jswrt_kv_bool(s, "strip_workspace_name", bar->strip_workspace_name);
	jswrt_kv_bool(s, "binding_mode_indicator", bar->binding_mode_indicator);
	jswrt_kv_bool(s, "verbose", bar->verbose);
	jswrt_kv_bool(s, "pango_markup", bar->pango_markup);

	jswrt_kv_object_open(s, "colors");
	jswrt_kv_string(s, "background", bar->colors.background);
	jswrt_kv_string(s, "statusline", bar->colors.statusline);
	jswrt_kv_string(s, "separator", bar->colors.separator);
	jswrt_kv_string(s, "focused_background", bar->colors.focused_background ?
		bar->colors.focused_background : bar->colors.background);
	jswrt_kv_string(s, "focused_statusline", bar->colors.focused_statusline ?
		bar->colors.focused_statusline : bar->colors.statusline);
	jswrt_kv_string(s, "focused_separator", bar->colors.focused_separator ?
		bar->colors.focused_separator : bar->colors.separator);

	jswrt_kv_string(s, "focused_workspace_border", bar->colors.focused_workspace_border);
	jswrt_kv_string(s, "focused_workspace_bg", bar->colors.focused_workspace_bg);
	jswrt_kv_string(s, "focused_workspace_text", bar->colors.focused_workspace_text);

	jswrt_kv_string(s, "inactive_workspace_border", bar->colors.inactive_workspace_border);
	jswrt_kv_string(s, "inactive_workspace_bg", bar->colors.inactive_workspace_bg);
	jswrt_kv_string(s, "inactive_workspace_text", bar->colors.inactive_workspace_text);

	jswrt_kv_string(s, "active_workspace_border", bar->colors.active_workspace_border);
	jswrt_kv_string(s, "active_workspace_bg", bar->colors.active_workspace_bg);
	jswrt_kv_string(s, "active_workspace_text", bar->colors.active_workspace_text);

	jswrt_kv_string(s, "urgent_workspace_border", bar->colors.urgent_workspace_border);
	jswrt_kv_string(s, "urgent_workspace_bg", bar->colors.urgent_workspace_bg);
	jswrt_kv_string(s, "urgent_workspace_text", bar->colors.urgent_workspace_text);

	jswrt_kv_string(s, "binding_mode_border", bar->colors.binding_mode_border ?
		bar->colors.binding_mode_border : bar->colors.urgent_workspace_border);
	jswrt_kv_string(s, "binding_mode_bg", bar->colors.binding_mode_bg ?
		bar->colors.binding_mode_bg : bar->colors.urgent_workspace_bg);
	jswrt_kv_string(s, "binding_mode_text", bar->colors.binding_mode_text ?
		bar->colors.binding_mode_text : bar->colors.urgent_workspace_text);

	jswrt_object_close(s);

	if (bar->bindings->length > 0) {
		jswrt_kv_array_open(s, "bindings");
		for (int i = 0; i < bar->bindings->length; ++i) {
			struct bar_binding *binding = bar->bindings->items[i];
			jswrt_object_open(s);
			jswrt_kv_integer(s, "input_code", binding->button);
			jswrt_kv_string(s, "command", binding->command);
			jswrt_kv_bool(s, "release", binding->release);
			jswrt_object_close(s);
		}
		jswrt_array_close(s);
	}

	// Add outputs if defined
	if (bar->outputs && bar->outputs->length > 0) {
		jswrt_kv_array_open(s, "outputs");
		for (int i = 0; i < bar->outputs->length; ++i) {
			const char *name = bar->outputs->items[i];
			jswrt_string(s, name);
		}
		jswrt_array_close(s);
	}
#if HAVE_TRAY
	// Add tray outputs if defined
	if (bar->tray_outputs && bar->tray_outputs->length > 0) {
		jswrt_kv_array_open(s, "tray_outputs");
		for (int i = 0; i < bar->tray_outputs->length; ++i) {
			const char *name = bar->tray_outputs->items[i];
			jswrt_string(s, name);
		}
		jswrt_array_close(s);
	}

	bool has_tray_bindings = false;
	for (int i = 0; i < 10; ++i) {
		has_tray_bindings |= bar->tray_bindings[i] != NULL;
	}
	if (has_tray_bindings) {
		jswrt_kv_array_open(s, "tray_bindings");
		for (int i = 0; i < 10; ++i) {
			if (bar->tray_bindings[i]) {
				jswrt_object_open(s);
				jswrt_kv_integer(s, "input_code", i);
				jswrt_kv_string(s, "command", bar->tray_bindings[i]);
				jswrt_object_close(s);
			}
		}
		jswrt_array_close(s);
	}

	if (bar->icon_theme) {
		jswrt_kv_string(s, "icon_theme", bar->icon_theme);
	}

	jswrt_kv_integer(s, "tray_padding", bar->tray_padding);
#endif

	jswrt_object_close(s);
}


void ipc_json_describe_seat_2(struct jswrt_state *s, struct sway_seat *seat) {
	if (!(sway_assert(seat, "Seat must not be null"))) {
		return;
	}

	jswrt_object_open(s);
	struct sway_node *focus = seat_get_focus(seat);

	jswrt_kv_string(s, "name", seat->wlr_seat->name);
	jswrt_kv_integer(s, "capabilities", seat->wlr_seat->capabilities);
	jswrt_kv_integer(s, "focus", focus ? focus->id : 0);

	jswrt_kv_array_open(s, "devices");

	struct sway_seat_device *device = NULL;
	wl_list_for_each(device, &seat->devices, link) {
		ipc_json_describe_input_2(s, device->input_device);
	}

	jswrt_array_close(s);
	jswrt_object_close(s);
}

void ipc_json_get_seats_2(struct jswrt_state *s) {
	jswrt_array_open(s);
	struct sway_seat *seat = NULL;
	wl_list_for_each(seat, &server.input->seats, link) {
		ipc_json_describe_seat_2(s, seat);
	}
	jswrt_array_close(s);
}


static void ipc_get_marks_callback_2(struct sway_container *con, void *data) {
	struct jswrt_state *s = (struct jswrt_state *)data;
	for (int i = 0; i < con->marks->length; ++i) {
		const char *mark = (const char *)con->marks->items[i];
		jswrt_string(s, mark);
	}
}
void ipc_json_get_marks_2(struct jswrt_state *s) {
	jswrt_array_open(s);
	root_for_each_container(ipc_get_marks_callback_2, s);
	jswrt_array_close(s);
}

void ipc_json_get_config_2(struct jswrt_state *s) {
	jswrt_object_open(s);
	jswrt_kv_string(s, "config", config->current_config);
	jswrt_object_close(s);
}


static void ipc_get_workspaces_callback(struct sway_workspace *workspace,
		void *data) {
	struct jswrt_state *s = data;

	// override the default focused indicator because
	// it's set differently for the get_workspaces reply
	struct sway_seat *seat = input_manager_get_default_seat();
	struct sway_workspace *focused_ws = seat_get_focused_workspace(seat);
	bool focused = workspace == focused_ws;

	ipc_json_start_describe_node_2(s, &workspace->node, focused);

	focused_ws = output_get_active_workspace(workspace->output);
	bool visible = workspace == focused_ws;

	jswrt_kv_bool(s, "visible", visible);
	jswrt_object_close(s); // matching  ipc_json_start_describe_node_2
}

void ipc_json_get_workspaces_2(struct jswrt_state *s) {
	jswrt_array_open(s);
	root_for_each_workspace(ipc_get_workspaces_callback, s);
	jswrt_array_close(s);
}


static void ipc_json_describe_disabled_output_2(struct jswrt_state *s, struct sway_output *output) {
	struct wlr_output *wlr_output = output->wlr_output;

	jswrt_object_open(s);
	jswrt_kv_string(s, "type", "output");
	jswrt_kv_string(s, "name", wlr_output->name);
	jswrt_kv_bool(s, "active", false);
	jswrt_kv_bool(s, "make", wlr_output->make);
	jswrt_kv_bool(s, "model", wlr_output->model);
	jswrt_kv_bool(s, "serial", wlr_output->serial);
	jswrt_kv_array_open(s, "modes");
	jswrt_array_close(s);
	jswrt_kv_string(s, "percent", NULL);
	jswrt_object_close(s);
}

void ipc_json_get_outputs_2(struct jswrt_state *s) {
	jswrt_array_open(s);

	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];

		// override the default focused indicator because it's set
		// differently for the get_outputs reply
		struct sway_seat *seat = input_manager_get_default_seat();
		struct sway_workspace *focused_ws =
			seat_get_focused_workspace(seat);
		bool focused = focused_ws && output == focused_ws->output;

		ipc_json_start_describe_node_2(s, &output->node, focused);
		jswrt_object_close(s);
	}
	struct sway_output *output;
	wl_list_for_each(output, &root->all_outputs, link) {
		if (!output->enabled) {
			ipc_json_describe_disabled_output_2(s, output);
		}
	}

	jswrt_array_close(s);
}

void ipc_json_event_workspace_2(struct jswrt_state *s, struct sway_workspace *old,
		struct sway_workspace *new, const char *change) {
	jswrt_object_open(s);
	jswrt_kv_string(s, "change", change);
	jswrt_key(s, "old");
	if (old) {
		ipc_json_describe_node_recursive_2(s, &old->node);
	} else {
		jswrt_string(s, NULL);
	}
	jswrt_key(s, "current");
	if (new) {
		ipc_json_describe_node_recursive_2(s, &new->node);
	} else {
		jswrt_string(s, NULL);
	}
	jswrt_object_close(s);
}
void ipc_json_event_window_2(struct jswrt_state *s, struct sway_container *window,
		const char *change) {
	jswrt_object_open(s);
	jswrt_kv_string(s, "change", change);
	jswrt_key(s, "container");
	ipc_json_describe_node_recursive_2(s, &window->node);
	jswrt_object_close(s);
}
