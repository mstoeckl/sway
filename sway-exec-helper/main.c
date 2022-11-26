#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>

static uint64_t str_to_int(char *str) {
	uint64_t i = 0;
	while (*str) {
			i = i * 10 + (*str - '0');
			str++;
	}
	return i;
}

#define VERSION 100

/* Message structure, since C doesn't have any good built-in way to handle this.
 *
 * [length, contents]
 * contents = request_header , [env strings], [argv strings]
 *
 * Process to run is argv[0].
 * Return messages: int64_t of launched pid (or -1), sent in order of requests
 */

struct request_header {
	uint32_t version;
	uint32_t env_length;
	uint32_t arg_length;
	// todo, future: allow passing in a socket for the child process
};

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "Usage: sway-exec-helper socket_fd\n");
		fprintf(stderr, "Only sway should run this program.\n");
		fprintf(stderr, "No error checking is performed.\n");
		return EXIT_FAILURE;
	}

	int sock_fd = str_to_int(argv[1]);
	if (sock_fd == -1) {
		return EXIT_FAILURE;
	}
	if (fcntl(sock_fd, F_SETFD, FD_CLOEXEC) == -1) {
		fprintf(stderr, "failed to make the socket CLOEXEC\n");
		return EXIT_FAILURE;
	}

	/* Call setsid(). Because sway-exec-helper is started by sway, it is
	 * definitely not the process group leader, so setsid will start a new
	 * session without a controlling terminal. Because sway-exec-helper is
	 * fairly simple (unlike sway) and never opens any controlling terminal,
	 * its children also do not inherit one; and unless they call setsid,
	 * they are not at risk of accidentally acquiring one. (Having a
	 * controlling terminal is bad, because the terminal can then signal/kill
	 * the process. This is not desired behavior for programs started by
	 * Sway.) */
	if (setsid() == -1) {
		fprintf(stderr, "setsid() failed: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	sigset_t set;
	sigemptyset(&set);
	sigprocmask(SIG_SETMASK, &set, NULL);
	// reset SIGPIPE, which sway had set to SIGIGN
	signal(SIGPIPE, SIG_DFL);

	bool reading_length = true;
	uint32_t length = 0;
	uint32_t bytes_left_to_read = sizeof(length);
	void *data = NULL;

	fprintf(stderr, "Started sway exec helper\n");
	while (true) {
		// Reap zombies
		(void)waitpid((pid_t)-1, NULL, WNOHANG);

		void *dst = reading_length ? (void *)&length : data;
		ssize_t ret = read(sock_fd, dst, bytes_left_to_read);
		if (ret == -1 && errno == EINTR) {
			// was interrupted; reap zombies and retry
			continue;
		} else if (ret == -1) {
			fprintf(stderr, "Unexpected error: %s\n", strerror(errno));
			return EXIT_FAILURE;
		} else if (ret == 0) {
			// end of file
			fprintf(stderr, "Connection closed, exiting sway-exec-helper\n");
			break;
		}
		bytes_left_to_read -= ret;
		if (bytes_left_to_read > 0) {
			// read more
			continue;
		}
		if (reading_length) {
			reading_length = false;
			data = calloc(length, 1);
			bytes_left_to_read = length;
			continue;
		}

		// We have received a message. Now process it
		struct request_header* header = data;
		if (header->version != VERSION) {
			fprintf(stderr, "Version mismatch, expected %d, received %d. Exiting.\n",
				VERSION, header->version);
			// exit, since input stream is unrecoverably invalid
			return EXIT_FAILURE;
		}

		char *cursor = (char *)data + sizeof(struct request_header);
		char **env = calloc(header->env_length + 1, sizeof(char *));
		char **argv = calloc(header->arg_length + 1, sizeof(char *));
		// todo: handle allocation failures by writing back -1q
		for (uint32_t i = 0; i < header->env_length; i++) {
			env[i] = cursor;
			cursor += strlen(cursor) + 1;
		}
		for (uint32_t i = 0; i < header->arg_length; i++) {
			argv[i] = cursor;
			cursor += strlen(cursor) + 1;
		}

//		struct timespec pre,post;
//		clock_gettime(CLOCK_MONOTONIC, &pre);
		pid_t pid = 0;
		if (posix_spawnp(&pid, argv[0], NULL, NULL, argv, env) == -1) {
			fprintf(stderr, "Failed to spawn program '%s'\n", argv[0]);
			pid = -1;
		}
//		clock_gettime(CLOCK_MONOTONIC, &post);
//		fprintf(stderr, "Actual spawn time: %f msec\n", 1e3*(post.tv_sec - pre.tv_sec) + 1e-6 * (post.tv_nsec - pre.tv_nsec));

		int64_t retval = pid;
		if (write(sock_fd, &retval, sizeof(retval)) != (ssize_t)sizeof(retval)) {
			fprintf(stderr, "Failed to write return value fully; desynchronized. Exiting.\n");
			return EXIT_FAILURE;
		}

		// process next command
		free(env);
		free(argv);
		free(data);
		reading_length = true;
		bytes_left_to_read = sizeof(length);
	}

	return EXIT_SUCCESS;
}
