#include <drm_fourcc.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/config.h"

struct cmd_results *output_cmd_render_bit_depth(int argc, char **argv) {
	if (!config->handler_context.output_config) {
		return cmd_results_new(CMD_FAILURE, "Missing output config");
	}
	if (!argc) {
		return cmd_results_new(CMD_INVALID, "Missing bit depth argument.");
	}

	if (strcmp(*argv, "8") == 0) {
		config->handler_context.output_config->render_bit_depth =
			RENDER_BIT_DEPTH_8;
	} else if (strcmp(*argv, "8packed") == 0) {
		config->handler_context.output_config->render_bit_depth =
			RENDER_BIT_DEPTH_8_PACKED;
	} else if (strcmp(*argv, "10") == 0) {
		config->handler_context.output_config->render_bit_depth =
			RENDER_BIT_DEPTH_10;
	} else if (strcmp(*argv, "5") == 0) {
		config->handler_context.output_config->render_bit_depth =
			RENDER_BIT_DEPTH_5;
	} else if (strcasecmp(*argv, "16f") == 0) {
		config->handler_context.output_config->render_bit_depth =
			RENDER_BIT_DEPTH_16F;
	} else if (strcmp(*argv, "16") == 0) {
		config->handler_context.output_config->render_bit_depth =
			RENDER_BIT_DEPTH_16;
	} else {
		return cmd_results_new(CMD_INVALID,
			"Invalid bit depth. Must be a value in (5|8|8packed|10|16|16f).");
	}

	config->handler_context.leftovers.argc = argc - 1;
	config->handler_context.leftovers.argv = argv + 1;
	return NULL;
}

