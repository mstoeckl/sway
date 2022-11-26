#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/server.h"
#include "sway/desktop/launcher.h"
#include "sway/server.h"
#include "sway/tree/container.h"
#include "sway/tree/root.h"
#include "sway/tree/workspace.h"
#include "log.h"
#include "stringop.h"

struct cmd_results *cmd_exec_validate(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, argv[-1], EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	if (!config->active || config->validating) {
		return cmd_results_new(CMD_DEFER, NULL);
	}
	return error;
}

static void export_xdga_token(struct launcher_ctx *ctx) {
	const char *token = launcher_ctx_get_token_name(ctx);
	setenv("XDG_ACTIVATION_TOKEN", token, 1);
}

static void export_startup_id(struct launcher_ctx *ctx) {
	const char *token = launcher_ctx_get_token_name(ctx);
	setenv("DESKTOP_STARTUP_ID", token, 1);
}

extern int exec_helper_socket;
extern char **environ;

#define VERSION 100
struct request_header {
	uint32_t version;
	uint32_t env_length;
	uint32_t arg_length;
};

struct cmd_results *cmd_exec_process(int argc, char **argv) {
	struct cmd_results *error = NULL;
	char *cmd = NULL;
	bool no_startup_id = false;
	if (strcmp(argv[0], "--no-startup-id") == 0) {
		no_startup_id = true;
		--argc; ++argv;
		if ((error = checkarg(argc, argv[-1], EXPECTED_AT_LEAST, 1))) {
			return error;
		}
	}

	if (argc == 1 && (argv[0][0] == '\'' || argv[0][0] == '"')) {
		cmd = strdup(argv[0]);
		strip_quotes(cmd);
	} else {
		cmd = join_args(argv, argc);
	}

	struct launcher_ctx *ctx = launcher_ctx_create();
	sway_log(SWAY_DEBUG, "Executing %s", cmd);

//	struct timespec t1;
//	clock_gettime(CLOCK_MONOTONIC, &t1);

	if (1) {
		if (exec_helper_socket == -1) {
			launcher_ctx_destroy(ctx);
			return cmd_results_new(CMD_FAILURE, "exec helper is dead :-((");
		}

		char *argv[] = {"sh", "-c", cmd};

		// compute message length message
		uint32_t msg_length = sizeof(struct request_header);
		struct request_header req = {
			.version = VERSION,
			.env_length = 0,
			.arg_length = 0,
		};

		// modify environment.
		// todo: send this change to child, without modifying own
		// environment
		if (ctx) {
			export_xdga_token(ctx);
		}
		if (ctx && !no_startup_id) {
			export_startup_id(ctx);
		}

		// using current process environ, no modifications
		char **env = environ;
		while (*env) {
			req.env_length += 1;
			msg_length += strlen(*env) + 1;
			env++;
		}

		req.arg_length = sizeof(argv) / sizeof(argv[0]);
		for (uint32_t i = 0; i < req.arg_length; i++) {
			msg_length += strlen(argv[i]) + 1;
		}

		// setup message
		char *message = malloc(msg_length + sizeof(uint32_t));
		memcpy(message, &msg_length, sizeof(uint32_t));
		char *cursor = message + sizeof(uint32_t);
		memcpy(cursor, &req, sizeof(req));
		cursor += sizeof(req);

		env = environ;
		while (*env) {
			size_t len = strlen(*env) + 1;
			memcpy(cursor, *env, len);
			cursor = cursor + len;
			env++;
		}
		for (uint32_t i = 0; i < req.arg_length; i++) {
			size_t len = strlen(argv[i]) + 1;
			memcpy(cursor, argv[i], len);
			cursor = cursor + len;
		}

		// write message to exec helper
		ssize_t bytes_left = (ssize_t)msg_length + sizeof(uint32_t);
		char *write_cursor = message;
		while (bytes_left > 0) {
			ssize_t delta = write(exec_helper_socket, write_cursor, bytes_left);
			if (delta == 0) {
				exec_helper_socket = -1;
				free(message);
				launcher_ctx_destroy(ctx);
				return cmd_results_new(CMD_FAILURE, "exec helper has died");
			} else if (delta == -1 && errno == EINTR) {
				continue;
			} else if (delta == -1) {
				sway_log(SWAY_DEBUG, "Error while writing to exec helper: %s", strerror(errno));
				close(exec_helper_socket);
				exec_helper_socket = -1;
				free(message);
				launcher_ctx_destroy(ctx);
				return cmd_results_new(CMD_FAILURE, "exec helper error");
			}
			bytes_left -= delta;
			write_cursor += delta;
		}
		free(message);

		// read response from exec helper
		int64_t ret = 0;
		char *ret_cursor = (char *)&ret;
		bytes_left = 8;
		while (bytes_left > 0) {
			ssize_t delta = read(exec_helper_socket, ret_cursor, bytes_left);
			if (delta == 0) {
				exec_helper_socket = -1;
				launcher_ctx_destroy(ctx);
				return cmd_results_new(CMD_FAILURE, "exec helper has died");
			} else if (delta == -1 && errno == EINTR) {
				continue;
			} else if (delta == -1) {
				sway_log(SWAY_DEBUG, "Error while reading from exec helper: %s", strerror(errno));
				close(exec_helper_socket);
				exec_helper_socket = -1;
				launcher_ctx_destroy(ctx);
				return cmd_results_new(CMD_FAILURE, "exec helper error");
			}
			bytes_left -= delta;
			ret_cursor += delta;
		}

		if (ret > 0) {
			pid_t child = ret;
			sway_log(SWAY_DEBUG, "Child process created with pid %d", child);
			if (ctx != NULL) {
				sway_log(SWAY_DEBUG, "Recording workspace for process %d", child);
				ctx->pid = child;
			}
		} else {
			launcher_ctx_destroy(ctx);
			return cmd_results_new(CMD_FAILURE, "Launching process via exec-helper failed");
		}
	} else {
		// Original path
		int fd[2];
		if (pipe(fd) != 0) {
			sway_log(SWAY_ERROR, "Unable to create pipe for fork");
		}

		pid_t pid, child;
		// Fork process
		if ((pid = fork()) == 0) {
			// Fork child process again
			restore_nofile_limit();
			setsid();
			sigset_t set;
			sigemptyset(&set);
			sigprocmask(SIG_SETMASK, &set, NULL);
			signal(SIGPIPE, SIG_DFL);
			close(fd[0]);
			if ((child = fork()) == 0) {
				close(fd[1]);
				execlp("sh", "sh", "-c", cmd, (void *)NULL);
				sway_log_errno(SWAY_ERROR, "execlp failed");
				_exit(1);
			}
			ssize_t s = 0;
			while ((size_t)s < sizeof(pid_t)) {
				s += write(fd[1], ((uint8_t *)&child) + s, sizeof(pid_t) - s);
			}
			close(fd[1]);
			_exit(0); // Close child process
		} else if (pid < 0) {
			free(cmd);
			close(fd[0]);
			close(fd[1]);
			launcher_ctx_destroy(ctx);
			return cmd_results_new(CMD_FAILURE, "fork() failed");
		}
		free(cmd);
		close(fd[1]); // close write
		ssize_t s = 0;
		while ((size_t)s < sizeof(pid_t)) {
			s += read(fd[0], ((uint8_t *)&child) + s, sizeof(pid_t) - s);
		}
		close(fd[0]);

		// cleanup child process
		waitpid(pid, NULL, 0);
		if (child > 0) {
			sway_log(SWAY_DEBUG, "Child process created with pid %d", child);
			if (ctx != NULL) {
				sway_log(SWAY_DEBUG, "Recording workspace for process %d", child);
				ctx->pid = child;
			}
		} else {
			launcher_ctx_destroy(ctx);
			return cmd_results_new(CMD_FAILURE, "Second fork() failed");
		}
	}

	/* Timing chart, from a medium aged laptop:
	 *
	 * DoubleFork, on battery, with ASAN: requires 25-30 msec typically
	 * DoubleFork, plugged in, sans ASAN: requires 8-9 msec
	 * ExecHelper, on battery, with ASAN: requires 2.2msec; 1.7 are posix_spawn
	 */
//	struct timespec t2;
//	clock_gettime(CLOCK_MONOTONIC, &t2);
//	sway_log(SWAY_ERROR, "Executing process took %f msec", 1e3*(t2.tv_sec - t1.tv_sec) + 1e-6 * (t2.tv_nsec - t1.tv_nsec));

	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_exec_always(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = cmd_exec_validate(argc, argv))) {
		return error;
	}
	return cmd_exec_process(argc, argv);
}
