#include <ctype.h>
#include <string.h>
#include "sway/commands.h"

// Must be in alphabetical order for bsearch
static struct cmd_handler bar_colors_handlers[] = {
	{ "active_workspace", bar_colors_cmd_active_workspace },
	{ "background", bar_colors_cmd_background },
	{ "binding_mode", bar_colors_cmd_binding_mode },
	{ "focused_background", bar_colors_cmd_focused_background },
	{ "focused_separator", bar_colors_cmd_focused_separator },
	{ "focused_statusline", bar_colors_cmd_focused_statusline },
	{ "focused_workspace", bar_colors_cmd_focused_workspace },
	{ "inactive_workspace", bar_colors_cmd_inactive_workspace },
	{ "separator", bar_colors_cmd_separator },
	{ "statusline", bar_colors_cmd_statusline },
	{ "urgent_workspace", bar_colors_cmd_urgent_workspace },
};

/**
 * Parse `color`, expecting a format like '#80ff03' or '#80ff03a0'.
 * Iff successful, sets `value` and returns true.
 */
static bool try_parse_color(const char *color, uint32_t *value) {
	if (color[0] != '#') {
		return false;
	}
	++color;
	int len = strlen(color);
	if (len != 6 && len != 8) {
		return false;
	}
	for (int i = 0; i < len; ++i) {
		if (!isxdigit(color[i])) {
			return false;
		}
	}
	uint32_t res = (uint32_t)strtoul(color, NULL, 16);
	if (len == 6) {
		res = (res << 8) | 0xFF;
	}
	*value = res;
	return true;
}

static struct cmd_results *parse_single_color(struct maybe_color *color,
		const char *cmd_name, int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, cmd_name, EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	bool parsed = try_parse_color(argv[0], &color->value);
	if (!parsed) {
		return cmd_results_new(CMD_INVALID, "Invalid color definition %s",
			color);
	}
	color->is_set = true;
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *parse_three_colors(struct maybe_color **colors,
		const char *cmd_name, int argc, char **argv) {
	if (argc != 3) {
		return cmd_results_new(CMD_INVALID,
				"Command '%s' requires exactly three color values", cmd_name);
	}
	// Do not immediately abort on error; attempt all three colors
	int err_idx = -1;
	for (int i = 0; i < 3; i++) {
		bool parsed = try_parse_color(argv[i], &colors[i]->value);
		if (parsed) {
			colors[i]->is_set = true;
		} else {
			err_idx = i;
		}
	}
	// Mention the last color which failed to parse
	if (err_idx >= 0) {
		return cmd_results_new(CMD_INVALID, "Invalid color definition %s",
			argv[err_idx]);
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *bar_cmd_colors(int argc, char **argv) {
	return config_subcommand(argv, argc, bar_colors_handlers,
			sizeof(bar_colors_handlers));
}

struct cmd_results *bar_colors_cmd_active_workspace(int argc, char **argv) {
	struct maybe_color *colors[3] = {
		&(config->current_bar->colors.active_workspace_border),
		&(config->current_bar->colors.active_workspace_bg),
		&(config->current_bar->colors.active_workspace_text)
	};
	return parse_three_colors(colors, "active_workspace", argc, argv);
}

struct cmd_results *bar_colors_cmd_background(int argc, char **argv) {
	return parse_single_color(&(config->current_bar->colors.background),
			"background", argc, argv);
}

struct cmd_results *bar_colors_cmd_focused_background(int argc, char **argv) {
	return parse_single_color(&(config->current_bar->colors.focused_background),
			"focused_background", argc, argv);
}

struct cmd_results *bar_colors_cmd_binding_mode(int argc, char **argv) {
	struct maybe_color *colors[3] = {
		&(config->current_bar->colors.binding_mode_border),
		&(config->current_bar->colors.binding_mode_bg),
		&(config->current_bar->colors.binding_mode_text)
	};
	return parse_three_colors(colors, "binding_mode", argc, argv);
}

struct cmd_results *bar_colors_cmd_focused_workspace(int argc, char **argv) {
	struct maybe_color *colors[3] = {
		&(config->current_bar->colors.focused_workspace_border),
		&(config->current_bar->colors.focused_workspace_bg),
		&(config->current_bar->colors.focused_workspace_text)
	};
	return parse_three_colors(colors, "focused_workspace", argc, argv);
}

struct cmd_results *bar_colors_cmd_inactive_workspace(int argc, char **argv) {
	struct maybe_color *colors[3] = {
		&(config->current_bar->colors.inactive_workspace_border),
		&(config->current_bar->colors.inactive_workspace_bg),
		&(config->current_bar->colors.inactive_workspace_text)
	};
	return parse_three_colors(colors, "inactive_workspace", argc, argv);
}

struct cmd_results *bar_colors_cmd_separator(int argc, char **argv) {
	return parse_single_color(&(config->current_bar->colors.separator),
			"separator", argc, argv);
}

struct cmd_results *bar_colors_cmd_focused_separator(int argc, char **argv) {
	return parse_single_color(&(config->current_bar->colors.focused_separator),
			"focused_separator", argc, argv);
}

struct cmd_results *bar_colors_cmd_statusline(int argc, char **argv) {
	return parse_single_color(&(config->current_bar->colors.statusline),
			"statusline", argc, argv);
}

struct cmd_results *bar_colors_cmd_focused_statusline(int argc, char **argv) {
	return parse_single_color(&(config->current_bar->colors.focused_statusline),
			"focused_statusline", argc, argv);
}

struct cmd_results *bar_colors_cmd_urgent_workspace(int argc, char **argv) {
	struct maybe_color *colors[3] = {
		&(config->current_bar->colors.urgent_workspace_border),
		&(config->current_bar->colors.urgent_workspace_bg),
		&(config->current_bar->colors.urgent_workspace_text)
	};
	return parse_three_colors(colors, "urgent_workspace", argc, argv);
}
