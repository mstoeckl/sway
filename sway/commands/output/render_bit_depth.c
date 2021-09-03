#include <drm_fourcc.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/config.h"

static const uint32_t format_order_8bpc[] = {
	DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB8888, 0
};

static const uint32_t format_order_10bpc[] = {
	DRM_FORMAT_ABGR2101010, DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_XBGR2101010, DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_ARGB8888, DRM_FORMAT_XRGB8888,
	0,
};

struct cmd_results *output_cmd_render_bit_depth(int argc, char **argv) {
	if (!config->handler_context.output_config) {
		return cmd_results_new(CMD_FAILURE, "Missing output config");
	}
	if (!argc) {
		return cmd_results_new(CMD_INVALID, "Missing bit depth argument.");
	}

	if (strcmp(*argv, "8") == 0) {
		config->handler_context.output_config->render_format_order =
			format_order_8bpc;
	} else if (strcmp(*argv, "10") == 0) {
		config->handler_context.output_config->render_format_order =
			format_order_10bpc;
	} else {
		return cmd_results_new(CMD_INVALID,
			"Invalid bit depth. Must be either 8 or 10 .");
	}

	config->handler_context.leftovers.argc = argc - 1;
	config->handler_context.leftovers.argv = argv + 1;
	return NULL;
}

