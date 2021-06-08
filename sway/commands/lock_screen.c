#define _POSIX_C_SOURCE 200809L
#include <string.h>

#include "log.h"
#include "util.h"
#include "stringop.h"
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/server.h"
#include "sway/input/seat.h"
#include "sway/output.h"

struct cmd_results *cmd_lock_screen(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = cmd_exec_validate(argc, argv))) {
		return error;
	}
	bool fail_locked;
	if (strcmp(argv[0], "--fail-locked") == 0) {
		fail_locked = true;
		argv++;
		argc--;
	} else if (strcmp(argv[0], "--fail-unlocked") == 0) {
		fail_locked = false;
		argv++;
		argc--;
	} else {
		return cmd_results_new(CMD_FAILURE, "must set either --fail-locked or --fail-unlocked");
	}

	char *cmd;
	if (argc == 1 && (argv[0][0] == '\'' || argv[0][0] == '"')) {
		cmd = strdup(argv[0]);
		strip_quotes(cmd);
	} else {
		cmd = join_args(argv, argc);
	}

	if (config->reloading) {
		char *args = join_args(argv, argc);
		sway_log(SWAY_DEBUG, "Ignoring 'cmd_lock_screen %s' due to reload", args);
		free(args);
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	struct cmd_results *res = run_lockscreen_cmd(cmd, fail_locked);
	free(cmd);
	return res;
}
