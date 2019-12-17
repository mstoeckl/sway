#ifndef _SWAY_SERVER_H
#define _SWAY_SERVER_H
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "config.h"
#include "list.h"
#if HAVE_XWAYLAND
#include "sway/xwayland.h"
#endif

struct delay_scheduler;
struct delayed_event {
	struct timespec deadline;
	// TODO: should there be a return code? what would a fatal error be?
	void (*event) (struct delayed_event *evt, struct timespec time);
	struct delay_scheduler *scheduler;
	int heap_idx;
};
struct delay_scheduler {
	// An index of all connected events, with active events in heap-order
	struct delayed_event **entries;
	int entry_count, active_count, space;

	// TODO: replace this with something that provides lower level access to
	// the timerfd, to get a) absolute timeouts b) acceptable resolution
	struct wl_event_source *timer;

	// TODO: actually enforce that the underlying timer uses this clock
	clockid_t presentation_clock;
};

/**
 * Connect a `struct delayed_event` to the matching `struct delay_scheduler`.
 * If this fails (and returns negative), the event is zeroed out.
 *
 * When the callback `event` is invoked, the first argument will equal `evt`,
 * and the second argument will be a time measurement from shortly before
 * the callback.
 */
int delayed_event_init(struct delayed_event *evt, struct delay_scheduler *sched,
	void (*event) (struct delayed_event *evt, struct timespec time));
/**
 * Detach `evt` from the connected delay_scheduler and zero it. If
 * `evt->scheduler` is NULL, this does nothing.
 */
void delayed_event_destroy(struct delayed_event *evt);
/**
 * Schedule `evt` to occurs roughly at `time`. If `time` is earlier than
 * the current time, the event will be invoked as soon as the scheduler
 * can do so. If the event was already scheduled for a time, the new time
 * replaces the old.
 */
int delayed_event_schedule(struct delayed_event *evt, struct timespec time);
/**
 * Schedule event `nsec` nanoseconds in the future.
 */
int delayed_event_schedule_from_now(struct delayed_event *evt, long nsec);
/**
 * Cancel the event (if it had been scheduled.)
 */
int delayed_event_disarm(struct delayed_event *evt);

struct sway_server {
	struct wl_display *wl_display;
	struct wl_event_loop *wl_event_loop;
	const char *socket;

	struct wlr_backend *backend;
	struct wlr_backend *noop_backend;

	struct wlr_compositor *compositor;
	struct wl_listener compositor_new_surface;

	struct wlr_data_device_manager *data_device_manager;

	struct sway_input_manager *input;

	struct wl_listener new_output;
	struct wl_listener output_layout_change;

	struct wlr_idle *idle;
	struct sway_idle_inhibit_manager_v1 *idle_inhibit_manager_v1;

	struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener layer_shell_surface;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener xdg_shell_surface;

	struct wlr_tablet_manager_v2 *tablet_v2;

#if HAVE_XWAYLAND
	struct sway_xwayland xwayland;
	struct wl_listener xwayland_surface;
	struct wl_listener xwayland_ready;
#endif

	struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;

	struct wlr_server_decoration_manager *server_decoration_manager;
	struct wl_listener server_decoration;
	struct wl_list decorations; // sway_server_decoration::link

	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
	struct wl_listener xdg_decoration;
	struct wl_list xdg_decorations; // sway_xdg_decoration::link

	struct wlr_presentation *presentation;

	struct wlr_pointer_constraints_v1 *pointer_constraints;
	struct wl_listener pointer_constraint;

	struct wlr_output_manager_v1 *output_manager_v1;
	struct wl_listener output_manager_apply;
	struct wl_listener output_manager_test;

	size_t txn_timeout_ms;
	list_t *transactions;
	list_t *dirty_nodes;

	struct delay_scheduler event_scheduler;
};

struct sway_server server;

struct sway_debug {
	bool noatomic;         // Ignore atomic layout updates
	bool txn_timings;      // Log verbose messages about transactions
	bool txn_wait;         // Always wait for the timeout before applying

	enum {
		DAMAGE_DEFAULT,    // Default behaviour
		DAMAGE_HIGHLIGHT,  // Highlight regions of the screen being damaged
		DAMAGE_RERENDER,   // Render the full output when any damage occurs
	} damage;
};

struct sway_debug debug;

/* Prepares an unprivileged server_init by performing all privileged operations in advance */
bool server_privileged_prepare(struct sway_server *server);
bool server_init(struct sway_server *server);
void server_fini(struct sway_server *server);
bool server_start(struct sway_server *server);
void server_run(struct sway_server *server);

void handle_compositor_new_surface(struct wl_listener *listener, void *data);
void handle_new_output(struct wl_listener *listener, void *data);

void handle_idle_inhibitor_v1(struct wl_listener *listener, void *data);
void handle_layer_shell_surface(struct wl_listener *listener, void *data);
void handle_xdg_shell_surface(struct wl_listener *listener, void *data);
#if HAVE_XWAYLAND
void handle_xwayland_surface(struct wl_listener *listener, void *data);
#endif
void handle_server_decoration(struct wl_listener *listener, void *data);
void handle_xdg_decoration(struct wl_listener *listener, void *data);
void handle_pointer_constraint(struct wl_listener *listener, void *data);

#endif
