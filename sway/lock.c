#define _POSIX_C_SOURCE 200809L
#include <drm_fourcc.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cairo_util.h"
#include "ext-unlocker-v1-protocol.h"
#include "log.h"
#include "pango.h"
#include "sway/input/seat.h"
#include "sway/config.h"
#include "sway/commands.h"
#include "sway/lock.h"
#include "sway/output.h"
#include "sway/server.h"
#include "util.h"

static void handle_unlocker_unlock(struct wl_client *client,
		struct wl_resource *resource) {
	if (server.lock_screen.client != client) {
		sway_log(SWAY_ERROR, "INVALID UNLOCK, IGNORING");
		return;
	}

	// The lockscreen may now shut down on its own schedule
	server.lock_screen.client = NULL;
	wl_list_remove(&server.lock_screen.client_destroy.link);
	wl_list_init(&server.lock_screen.client_destroy.link);
	sway_log(SWAY_ERROR, "RECEIVED UNLOCK");

	struct sway_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		seat_set_exclusive_client(seat, NULL);
		// copied from input_manager -- deduplicate?
		struct sway_node *previous = seat_get_focus(seat);
		if (previous) {
			// Hack to get seat to re-focus the return value of get_focus
			seat_set_focus(seat, NULL);
			seat_set_focus(seat, previous);
		}
	}

	// redraw everything
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		output_damage_whole(output);
	}
}

static const struct ext_unlocker_v1_interface unlock_impl = {
	.unlock = handle_unlocker_unlock,
};

static void screenlock_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	if (client != server.lock_screen.client) {
		sway_log(SWAY_ERROR, "WRONG LOCKSCREEN CLIENT");
		wl_client_post_implementation_error(client, "wrong client, todo filter globals");
		return;
	}
	struct wl_resource *resource = wl_resource_create(client,
		&ext_unlocker_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &unlock_impl, NULL, NULL);
}


static void handle_lockscreen_client_destroy(struct wl_listener *listener, void *data) {
	struct wl_client *client = data;
	if (server.lock_screen.client != client) {
		// this was an earlier lock screen client that had already
		// unlocked; ignore it. No ABA risk since lock_screen.client
		// must be live.
		sway_log(SWAY_ERROR, "AN OLD LOCKSCREEN CLIENT DIED");
		return;
	}

	wl_list_remove(&server.lock_screen.client_destroy.link);
	wl_list_init(&server.lock_screen.client_destroy.link);

	/* client closed, but did not unlock and reset the server.lock_screen_client */
	if (server.lock_screen.fail_locked) {
		// TODO: implement 'unlock_screen' command which which to recover
		// from permalocking; or make a repeated 'lock_screen' work?
		// or force people to swaymsg -- lock_screen --fail-unlocked /bin/false ?
		sway_log(SWAY_ERROR, "THE LOCKSCREEN CLIENT DIED, PERMALOCKING");
		server.lock_screen.client = PERMALOCK_CLIENT;
		struct sway_seat *seat;
		wl_list_for_each(seat, &server.input->seats, link) {
			seat_set_exclusive_client(seat, PERMALOCK_CLIENT);
		}
	} else {
		server.lock_screen.client = NULL;

		sway_log(SWAY_ERROR, "THE LOCKSCREEN CLIENT DIED, UNLOCKING");
		struct sway_seat *seat;
		wl_list_for_each(seat, &server.input->seats, link) {
			seat_set_exclusive_client(seat, NULL);
			// copied from input_manager -- deduplicate?
			struct sway_node *previous = seat_get_focus(seat);
			if (previous) {
				// Hack to get seat to re-focus the return value of get_focus
				seat_set_focus(seat, NULL);
				seat_set_focus(seat, previous);
			}
		}
	}

	// redraw everything
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		output_damage_whole(output);
	}
}

void sway_lock_state_create(struct sway_lock_state *state,
		struct wl_display *display) {
	state->ext_unlocker_v1_global =
		wl_global_create(display, &ext_unlocker_v1_interface,
			1, NULL, screenlock_bind);
}

struct wlr_texture *draw_permalock_message(struct sway_output *output) {
	sway_log(SWAY_ERROR, "CREATING PERMALOCK MESSAGE");

	int width = 0;
	int height = 0;

	const char* permalock_msg = "Lock screen crashed. Can only unlock by running lock_screen again.";

	// We must use a non-nil cairo_t for cairo_set_font_options to work.
	// Therefore, we cannot use cairo_create(NULL).
	cairo_surface_t *dummy_surface = cairo_image_surface_create(
			CAIRO_FORMAT_ARGB32, 0, 0);
	cairo_t *c = cairo_create(dummy_surface);
	cairo_set_antialias(c, CAIRO_ANTIALIAS_BEST);
	cairo_font_options_t *fo = cairo_font_options_create();
	cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_NONE);
	cairo_set_font_options(c, fo);
	get_text_size(c, config->font, &width, &height, NULL, output->wlr_output->scale,
			config->pango_markup, "%s", permalock_msg);
	cairo_surface_destroy(dummy_surface);
	cairo_destroy(c);

	cairo_surface_t *surface = cairo_image_surface_create(
			CAIRO_FORMAT_ARGB32, width, height);
	cairo_t *cairo = cairo_create(surface);
	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);
	cairo_set_font_options(cairo, fo);
	cairo_font_options_destroy(fo);
	cairo_set_source_rgba(cairo, 1.0,1.0,1.0,0.0);
	cairo_paint(cairo);
	PangoContext *pango = pango_cairo_create_context(cairo);
	cairo_set_source_rgba(cairo, 0.,0.,0.,1.0);
	cairo_move_to(cairo, 0, 0);

	pango_printf(cairo, config->font, output->wlr_output->scale, config->pango_markup,
			"%s", permalock_msg);

	cairo_surface_flush(surface);
	unsigned char *data = cairo_image_surface_get_data(surface);
	int stride = cairo_image_surface_get_stride(surface);
	struct wlr_renderer *renderer = wlr_backend_get_renderer(
			output->wlr_output->backend);
	struct wlr_texture *tex = wlr_texture_from_pixels(
			renderer, DRM_FORMAT_ARGB8888, stride, width, height, data);
	cairo_surface_destroy(surface);
	g_object_unref(pango);
	cairo_destroy(cairo);
	return tex;
}


struct cmd_results *run_lockscreen_cmd(const char *cmd, bool fail_locked) {
	int sockets[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
		sway_log_errno(SWAY_ERROR, "socketpair failed");
		return cmd_results_new(CMD_FAILURE, "socketpair failed");
	}
	if (!sway_set_cloexec(sockets[0], true) || !sway_set_cloexec(sockets[1], true)) {
		return cmd_results_new(CMD_FAILURE, "cloexec failed");
	}

	/* Replace any existing lock screen with the new one */
	if (server.lock_screen.client) {
		if (server.lock_screen.client != PERMALOCK_CLIENT) {
			wl_list_remove(&server.lock_screen.client_destroy.link);
			wl_list_init(&server.lock_screen.client_destroy.link);
			wl_client_destroy(server.lock_screen.client);
		}
		server.lock_screen.client = NULL;
	}

	server.lock_screen.fail_locked = fail_locked;

	server.lock_screen.client = wl_client_create(server.wl_display, sockets[0]);
	if (!server.lock_screen.client) {
		sway_log_errno(SWAY_ERROR, "wl_client_create failed");
		if (!fail_locked) {
			return cmd_results_new(CMD_FAILURE, "wl_client_create failed");
		}
		server.lock_screen.client = PERMALOCK_CLIENT;
	} else {
		server.lock_screen.client_destroy.notify = handle_lockscreen_client_destroy;
		wl_client_add_destroy_listener(server.lock_screen.client,
			&server.lock_screen.client_destroy);
	}

	// only lock screen gets input; this applies immediately,
	// before the lock screen program is set up
	struct sway_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		seat_set_exclusive_client(seat,  server.lock_screen.client);
	}

	pid_t pid = fork();
	if (pid < 0) {
		sway_log(SWAY_ERROR, "Failed to create fork for swaybar");
		return cmd_results_new(CMD_FAILURE, "fork failed");
	} else if (pid == 0) {
		// Remove the SIGUSR1 handler that wlroots adds for xwayland
		sigset_t set;
		sigemptyset(&set);
		sigprocmask(SIG_SETMASK, &set, NULL);
		signal(SIGPIPE, SIG_DFL);

		pid = fork();
		if (pid < 0) {
			sway_log_errno(SWAY_ERROR, "fork failed");
			_exit(EXIT_FAILURE);
		} else if (pid == 0) {
			if (!sway_set_cloexec(sockets[1], false)) {
				_exit(EXIT_FAILURE);
			}

			char wayland_socket_str[16];
			snprintf(wayland_socket_str, sizeof(wayland_socket_str),
				"%d", sockets[1]);
			setenv("WAYLAND_SOCKET", wayland_socket_str, true);

			execlp("sh", "sh", "-c", cmd, (void *)NULL);
			_exit(EXIT_FAILURE);
		}
		_exit(EXIT_SUCCESS);
	}

	if (close(sockets[1]) != 0) {
		sway_log_errno(SWAY_ERROR, "close failed");
		return cmd_results_new(CMD_FAILURE, "close failed");
	}

	if (waitpid(pid, NULL, 0) < 0) {
		sway_log_errno(SWAY_ERROR, "waitpid failed");
		return cmd_results_new(CMD_FAILURE, "waitpid failed");
	}
	sway_log(SWAY_ERROR, "LOCKSCREEN CLIENT STARTED");
	return cmd_results_new(CMD_SUCCESS, NULL);
}
