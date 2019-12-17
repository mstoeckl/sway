#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/noop.h>
#include <wlr/backend/session.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_gtk_primary_selection.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include "config.h"
#include "list.h"
#include "log.h"
#include "sway/config.h"
#include "sway/desktop/idle_inhibit_v1.h"
#include "sway/input/input-manager.h"
#include "sway/output.h"
#include "sway/server.h"
#include "sway/tree/root.h"
#if HAVE_XWAYLAND
#include "sway/xwayland.h"
#endif

static bool time_lt(struct timespec ta, struct timespec tb) {
	if (ta.tv_sec != tb.tv_sec) {
		return ta.tv_sec < tb.tv_sec;
	}
	return ta.tv_nsec < tb.tv_nsec;
}
static int event_sched_func(void *data) {
	struct delay_scheduler *sched = (struct delay_scheduler *)data;
	struct timespec now;
	clock_gettime(sched->presentation_clock, &now);
	struct timespec expiry = now;
	expiry.tv_nsec += 1000000L; // 1 msec extra absorption time
	if (expiry.tv_nsec > 1000000000L) {
		expiry.tv_nsec -= 1000000000L;
		expiry.tv_sec += 1;
	}

	while (sched->active_count > 0) {
		struct delayed_event *root = sched->entries[0];
		if (time_lt(root->deadline, expiry)) {
			// Disarm _before_ calling handler, in case the
			// handler reschedules the event
			delayed_event_disarm(root);
			(*root->event)(root, now);
		} else {
			break;
		}
	}

	if (sched->active_count > 0) {
		struct delayed_event *root = sched->entries[0];
		long delta = (root->deadline.tv_sec - now.tv_sec) * 1000000000L + (root->deadline.tv_nsec - now.tv_nsec);
		long msecs_until_evt = delta / 1000000L;
		if (msecs_until_evt <= 0) {
			msecs_until_evt = 1;
		}
		wl_event_source_timer_update(sched->timer, msecs_until_evt);
	} else {
		wl_event_source_timer_update(sched->timer, 0);
	}

	return 0;
}
int delayed_event_init(struct delayed_event *evt, struct delay_scheduler *sched,
		       void (*event) (struct delayed_event *evt, struct timespec a_very_recent_time)) {
	if (sched->entry_count + 1 > sched->space) {
		int nspace = sched->space >= 8 ? sched->space * 2 : 8;
		struct delayed_event **n = realloc(sched->entries, nspace * sizeof(struct delay_scheduler*));
		if (!n) {
			sway_log(SWAY_ERROR, "Allocation failure when expanding scheduler");
			return -1;
		}
		sched->entries = n;
		sched->space = nspace;
	}

	evt->scheduler = sched;
	evt->event = event;
	evt->deadline.tv_sec = 0;
	evt->deadline.tv_nsec = 0;
	evt->heap_idx = sched->entry_count;
	sched->entries[sched->entry_count] = evt;
	sched->entry_count++;
	return 0;
}
void delayed_event_destroy(struct delayed_event *evt) {
	if (!evt->scheduler) {
		// Event is zeroed and has already been cleaned up
		return;
	}

	delayed_event_disarm(evt);

	struct delay_scheduler *sched = evt->scheduler;

	// Replace `evt` with `last_evt` in the list of all events, and remove
	// `last_evt`'s old spot
	struct delayed_event *last_evt = sched->entries[sched->entry_count - 1];
	last_evt->heap_idx = evt->heap_idx;
	sched->entries[last_evt->heap_idx] = last_evt;
	sched->entries[sched->entry_count - 1] = NULL;
	sched->entry_count--;

	// Clear the event
	memset(evt, 0, sizeof(*evt));

	if (sched->space >= 16 && sched->space >= 4 * sched->entry_count) {
		struct delayed_event **n = realloc(sched->entries, sched->space / 2 * sizeof(struct delay_scheduler*));
		if (!n) {
			sway_log(SWAY_ERROR, "Allocation failure when shrinking scheduler");
			return;
		}
		sched->entries = n;
		sched->space = sched->space / 2;
	}
}

int delayed_event_schedule_from_now(struct delayed_event *evt, long nsec) {
	sway_assert(nsec >= 0, "Invalid event delay");

	struct timespec now;
	clock_gettime(evt->scheduler->presentation_clock, &now);
	long unit = nsec / 1000000000L;
	long fract = nsec % 1000000000L;
	now.tv_nsec += fract;
	if (now.tv_nsec >= 1000000000L) {
		now.tv_nsec -= 1000000000L;
		now.tv_sec += 1;
	}
	now.tv_sec += unit;
	return delayed_event_schedule(evt, now);
}

/* Move element down in the heap tree as far as possible, always replacing with
 * a live element */
static void heap_sift_down(struct delay_scheduler *sched, struct delayed_event *evt) {
	while (1) {
		int lchild_idx = evt->heap_idx*2 + 1;
		int rchild_idx = evt->heap_idx*2 + 2;
		if (rchild_idx > sched->active_count) {
			return;
		} else if (rchild_idx == sched->active_count) {
			// Only one active child, swap if greater
			struct delayed_event *lchild = sched->entries[lchild_idx];
			if (time_lt(lchild->deadline, evt->deadline)) {
				lchild->heap_idx = evt->heap_idx;
				evt->heap_idx = lchild_idx;

				sched->entries[lchild->heap_idx] = lchild;
				sched->entries[evt->heap_idx] = evt;
			} else {
				return;
			}
		} else {
			// Both children active, swap only with lesser one
			struct delayed_event *lchild = sched->entries[lchild_idx];
			struct delayed_event *rchild = sched->entries[rchild_idx];
			struct delayed_event *schild =
				time_lt(lchild->deadline, rchild->deadline)
				? lchild : rchild;
			if (time_lt(schild->deadline, evt->deadline)) {
				int sci = schild->heap_idx;
				schild->heap_idx = evt->heap_idx;
				evt->heap_idx = sci;

				sched->entries[schild->heap_idx] = schild;
				sched->entries[evt->heap_idx] = evt;
			} else {
				return;
			}
		}

	}

}
/* Move element up in the heap tree as far as possible */
static void heap_sift_up(struct delay_scheduler *sched, struct delayed_event *evt) {
	while (evt->heap_idx > 0) {
		int parent_idx = (evt->heap_idx - 1) / 2;
		struct delayed_event *parent = sched->entries[parent_idx];
		if (time_lt(evt->deadline, parent->deadline)) {
			parent->heap_idx = evt->heap_idx;
			evt->heap_idx = parent_idx;

			sched->entries[parent->heap_idx] = parent;
			sched->entries[evt->heap_idx] = evt;
		} else {
			return;
		}
	}
}
int delayed_event_schedule(struct delayed_event *evt, struct timespec the_deadline) {
	if (evt->deadline.tv_sec != 0 || evt->deadline.tv_sec != 0) {
		delayed_event_disarm(evt);
	}
	evt->deadline = the_deadline;
	struct delay_scheduler *sched = evt->scheduler;
	// Move this element to the end.
	int old_spot = evt->heap_idx;
	struct delayed_event *last_end_evt = sched->entries[sched->active_count];
	sched->entries[sched->active_count] = evt;
	sched->entries[old_spot] = last_end_evt;
	evt->heap_idx = sched->active_count;
	last_end_evt->heap_idx = old_spot;
	sched->active_count++;
	// sift toward root
	heap_sift_up(sched, evt);

	if (evt->heap_idx == 0) {
		// We are now the root element, starting _strictly_ earlier
		// than anything else, so update the timer.
		struct timespec now;
		clock_gettime(sched->presentation_clock, &now);
		long delta = (evt->deadline.tv_sec - now.tv_sec) * 1000000000L + (evt->deadline.tv_nsec - now.tv_nsec);
		// rounded _down_, if >0
		long msecs_until_evt = delta / 1000000L;
		if (msecs_until_evt <= 0) {
			msecs_until_evt = 1;
		}
		if (wl_event_source_timer_update(sched->timer, msecs_until_evt) < 0) {
			return -1;
		}
	}
	return 0;
}
int delayed_event_disarm(struct delayed_event *evt) {
	if (evt->deadline.tv_sec == 0 && evt->deadline.tv_sec == 0) {
		return 0;
	}
	evt->deadline.tv_sec = 0;
	evt->deadline.tv_nsec = 0;
	struct delay_scheduler *sched = evt->scheduler;
	if (sched->active_count <= 1) {
		sched->active_count = 0;

		// This was the last event, turn off the timer
		wl_event_source_timer_update(sched->timer, 0);
		return 0;
	}

	int old_spot = evt->heap_idx;
	if (old_spot == sched->active_count - 1) {
		sched->active_count--;
	} else {
		struct delayed_event *last_end_evt = sched->entries[sched->active_count - 1];
		sched->entries[sched->active_count - 1] = evt;
		sched->entries[old_spot] = last_end_evt;
		evt->heap_idx = sched->active_count - 1;
		last_end_evt->heap_idx = old_spot;
		sched->active_count--;

		// Move the displaced (active) element to its proper place.
		// Only one of sift-down and sift-up will have any effect
		heap_sift_down(sched, last_end_evt);
		heap_sift_up(sched, last_end_evt);
	}
	return 0;
}

bool server_privileged_prepare(struct sway_server *server) {
	sway_log(SWAY_DEBUG, "Preparing Wayland server initialization");
	server->wl_display = wl_display_create();
	server->wl_event_loop = wl_display_get_event_loop(server->wl_display);
	server->backend = wlr_backend_autocreate(server->wl_display, NULL);
	server->noop_backend = wlr_noop_backend_create(server->wl_display);

	if (!server->backend) {
		sway_log(SWAY_ERROR, "Unable to create backend");
		return false;
	}
	return true;
}

bool server_init(struct sway_server *server) {
	sway_log(SWAY_DEBUG, "Initializing Wayland server");

	struct wlr_renderer *renderer = wlr_backend_get_renderer(server->backend);
	assert(renderer);

	wlr_renderer_init_wl_display(renderer, server->wl_display);

	server->compositor = wlr_compositor_create(server->wl_display, renderer);
	server->compositor_new_surface.notify = handle_compositor_new_surface;
	wl_signal_add(&server->compositor->events.new_surface,
		&server->compositor_new_surface);

	server->data_device_manager =
		wlr_data_device_manager_create(server->wl_display);

	wlr_gamma_control_manager_v1_create(server->wl_display);
	wlr_gtk_primary_selection_device_manager_create(server->wl_display);

	server->new_output.notify = handle_new_output;
	wl_signal_add(&server->backend->events.new_output, &server->new_output);
	server->output_layout_change.notify = handle_output_layout_change;
	wl_signal_add(&root->output_layout->events.change,
		&server->output_layout_change);

	wlr_xdg_output_manager_v1_create(server->wl_display, root->output_layout);

	server->idle = wlr_idle_create(server->wl_display);
	server->idle_inhibit_manager_v1 =
		sway_idle_inhibit_manager_v1_create(server->wl_display, server->idle);

	server->layer_shell = wlr_layer_shell_v1_create(server->wl_display);
	wl_signal_add(&server->layer_shell->events.new_surface,
		&server->layer_shell_surface);
	server->layer_shell_surface.notify = handle_layer_shell_surface;

	server->xdg_shell = wlr_xdg_shell_create(server->wl_display);
	wl_signal_add(&server->xdg_shell->events.new_surface,
		&server->xdg_shell_surface);
	server->xdg_shell_surface.notify = handle_xdg_shell_surface;

	server->tablet_v2 = wlr_tablet_v2_create(server->wl_display);

	server->server_decoration_manager =
		wlr_server_decoration_manager_create(server->wl_display);
	wlr_server_decoration_manager_set_default_mode(
		server->server_decoration_manager,
		WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
	wl_signal_add(&server->server_decoration_manager->events.new_decoration,
		&server->server_decoration);
	server->server_decoration.notify = handle_server_decoration;
	wl_list_init(&server->decorations);

	server->xdg_decoration_manager =
		wlr_xdg_decoration_manager_v1_create(server->wl_display);
	wl_signal_add(
			&server->xdg_decoration_manager->events.new_toplevel_decoration,
			&server->xdg_decoration);
	server->xdg_decoration.notify = handle_xdg_decoration;
	wl_list_init(&server->xdg_decorations);

	server->relative_pointer_manager =
		wlr_relative_pointer_manager_v1_create(server->wl_display);

	server->pointer_constraints =
		wlr_pointer_constraints_v1_create(server->wl_display);
	server->pointer_constraint.notify = handle_pointer_constraint;
	wl_signal_add(&server->pointer_constraints->events.new_constraint,
		&server->pointer_constraint);

	server->presentation =
		wlr_presentation_create(server->wl_display, server->backend);

	server->output_manager_v1 =
		wlr_output_manager_v1_create(server->wl_display);
	server->output_manager_apply.notify = handle_output_manager_apply;
	wl_signal_add(&server->output_manager_v1->events.apply,
		&server->output_manager_apply);
	server->output_manager_test.notify = handle_output_manager_test;
	wl_signal_add(&server->output_manager_v1->events.test,
		&server->output_manager_test);

	wlr_export_dmabuf_manager_v1_create(server->wl_display);
	wlr_screencopy_manager_v1_create(server->wl_display);
	wlr_data_control_manager_v1_create(server->wl_display);
	wlr_primary_selection_v1_device_manager_create(server->wl_display);

	server->socket = wl_display_add_socket_auto(server->wl_display);
	if (!server->socket) {
		sway_log(SWAY_ERROR, "Unable to open wayland socket");
		wlr_backend_destroy(server->backend);
		return false;
	}

	struct wlr_output *wlr_output = wlr_noop_add_output(server->noop_backend);
	root->noop_output = output_create(wlr_output);

	// This may have been set already via -Dtxn-timeout
	if (!server->txn_timeout_ms) {
		server->txn_timeout_ms = 200;
	}

	server->dirty_nodes = create_list();
	server->transactions = create_list();

	server->input = input_manager_create(server);
	input_manager_get_default_seat(); // create seat0

	server->event_scheduler.presentation_clock =
			wlr_backend_get_presentation_clock(server->backend);
	server->event_scheduler.timer = wl_event_loop_add_timer(server->wl_event_loop,
		event_sched_func, &server->event_scheduler);

	return true;
}

void server_fini(struct sway_server *server) {
	// TODO: free sway-specific resources

#if HAVE_XWAYLAND
	wlr_xwayland_destroy(server->xwayland.wlr_xwayland);
#endif
	wl_display_destroy_clients(server->wl_display);
	wl_display_destroy(server->wl_display);
	list_free(server->dirty_nodes);
	list_free(server->transactions);

	// cleanup scheduler
	wl_event_source_remove(server->event_scheduler.timer);
	for (int i = 0; i < server->event_scheduler.entry_count; i++) {
		memset(server->event_scheduler.entries[i], 0,
			sizeof(struct delayed_event));
	}
	free(server->event_scheduler.entries);
	server->event_scheduler.entries = NULL;
}

bool server_start(struct sway_server *server) {
#if HAVE_XWAYLAND
	if (config->xwayland != XWAYLAND_MODE_DISABLED) {
		sway_log(SWAY_DEBUG, "Initializing Xwayland (lazy=%d)",
				config->xwayland == XWAYLAND_MODE_LAZY);
		server->xwayland.wlr_xwayland =
			wlr_xwayland_create(server->wl_display, server->compositor,
					config->xwayland == XWAYLAND_MODE_LAZY);
		wl_signal_add(&server->xwayland.wlr_xwayland->events.new_surface,
			&server->xwayland_surface);
		server->xwayland_surface.notify = handle_xwayland_surface;
		wl_signal_add(&server->xwayland.wlr_xwayland->events.ready,
			&server->xwayland_ready);
		server->xwayland_ready.notify = handle_xwayland_ready;

		setenv("DISPLAY", server->xwayland.wlr_xwayland->display_name, true);

		/* xcursor configured by the default seat */
	}
#endif

	sway_log(SWAY_INFO, "Starting backend on wayland display '%s'",
			server->socket);
	if (!wlr_backend_start(server->backend)) {
		sway_log(SWAY_ERROR, "Failed to start backend");
		wlr_backend_destroy(server->backend);
		return false;
	}
	return true;
}

void server_run(struct sway_server *server) {
	sway_log(SWAY_INFO, "Running compositor on wayland display '%s'",
			server->socket);
	wl_display_run(server->wl_display);
}
