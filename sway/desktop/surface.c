#define _POSIX_C_SOURCE 200112L
#include <stdlib.h>
#include <time.h>
#include <wlr/types/wlr_surface.h>
#include "sway/server.h"
#include "sway/surface.h"
#include "log.h"

void handle_destroy(struct wl_listener *listener, void *data) {
	struct sway_surface *surface = wl_container_of(listener, surface, destroy);

	surface->wlr_surface->data = NULL;
	wl_list_remove(&surface->destroy.link);

	delayed_event_destroy(&surface->frame_done_event);

	free(surface);
}

static void surface_frame_done_timer_handler(struct delayed_event* evt, struct timespec a_very_recent_time) {
	struct sway_surface *surface = wl_container_of(evt, surface, frame_done_event);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_surface_send_frame_done(surface->wlr_surface, &now);
}

void handle_compositor_new_surface(struct wl_listener *listener, void *data) {
	struct wlr_surface *wlr_surface = data;

	struct sway_surface *surface = calloc(1, sizeof(struct sway_surface));
	surface->wlr_surface = wlr_surface;
	wlr_surface->data = surface;

	surface->destroy.notify = handle_destroy;
	wl_signal_add(&wlr_surface->events.destroy, &surface->destroy);

	if (delayed_event_init(&surface->frame_done_event, &server.event_scheduler, surface_frame_done_timer_handler) < 0) {
		sway_log(SWAY_ERROR, "Failed to setup timer event");
	}
}
