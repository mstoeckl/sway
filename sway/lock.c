#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include "log.h"
#include "sway/input/keyboard.h"
#include "sway/input/seat.h"
#include "sway/output.h"
#include "sway/server.h"
#include <wlr/types/wlr_subcompositor.h>

struct sway_session_lock_surface {
	struct wlr_session_lock_surface_v1 *lock_surface;
	struct sway_output *output;
	struct wlr_surface *surface;
	struct wl_listener map;
	struct wl_listener destroy;
	struct wl_listener surface_commit;
	struct wl_listener output_mode;
	struct wl_listener output_commit;
	struct wl_listener new_subsurface;
	struct wl_list subsurfaces;
};

struct sway_session_lock_subsurface {
	struct wlr_subsurface *wlr_subsurface;
	struct sway_session_lock_surface *lock_surface;
	struct wl_list link;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
};

static void lock_subsurface_destroy(struct sway_session_lock_subsurface *subsurface) {
	wl_list_remove(&subsurface->link);
	wl_list_remove(&subsurface->map.link);
	wl_list_remove(&subsurface->unmap.link);
	wl_list_remove(&subsurface->destroy.link);
	wl_list_remove(&subsurface->commit.link);
	free(subsurface);
}

static void handle_surface_map(struct wl_listener *listener, void *data) {
	struct sway_session_lock_surface *surf = wl_container_of(listener, surf, map);
	sway_force_focus(surf->surface);
	output_damage_whole(surf->output);
}

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	struct sway_session_lock_surface *surf = wl_container_of(listener, surf, surface_commit);
	output_damage_surface(surf->output, 0, 0, surf->surface, false);
}

static void handle_output_mode(struct wl_listener *listener, void *data) {
	struct sway_session_lock_surface *surf = wl_container_of(listener, surf, output_mode);
	wlr_session_lock_surface_v1_configure(surf->lock_surface,
		surf->output->width, surf->output->height);
}

static void handle_output_commit(struct wl_listener *listener, void *data) {
	struct wlr_output_event_commit *event = data;
	struct sway_session_lock_surface *surf = wl_container_of(listener, surf, output_commit);
	if (event->committed & (
			WLR_OUTPUT_STATE_MODE |
			WLR_OUTPUT_STATE_SCALE |
			WLR_OUTPUT_STATE_TRANSFORM)) {
		wlr_session_lock_surface_v1_configure(surf->lock_surface,
			surf->output->width, surf->output->height);
	}
}

static void handle_surface_destroy(struct wl_listener *listener, void *data) {
	struct sway_session_lock_surface *surf = wl_container_of(listener, surf, destroy);

	struct sway_session_lock_subsurface *subsurface, *subsurface_tmp;
	wl_list_for_each_safe(subsurface, subsurface_tmp, &surf->subsurfaces, link) {
		lock_subsurface_destroy(subsurface);
	}

	wl_list_remove(&surf->map.link);
	wl_list_remove(&surf->destroy.link);
	wl_list_remove(&surf->surface_commit.link);
	wl_list_remove(&surf->output_mode.link);
	wl_list_remove(&surf->output_commit.link);
	output_damage_whole(surf->output);
	free(surf);
}

static void subsurface_damage(struct sway_session_lock_subsurface *subsurface,
		bool whole) {
	struct sway_session_lock_surface *layer = subsurface->lock_surface;
	struct wlr_output *wlr_output = layer->output->wlr_output;
	if (!wlr_output) {
		return;
	}
	struct sway_output *output = wlr_output->data;
	int ox = subsurface->wlr_subsurface->current.x;
	int oy = subsurface->wlr_subsurface->current.y;
	output_damage_surface(
			output, ox, oy, subsurface->wlr_subsurface->surface, whole);
}

static void subsurface_handle_unmap(struct wl_listener *listener, void *data) {
	struct sway_session_lock_subsurface *subsurface =
			wl_container_of(listener, subsurface, unmap);
	subsurface_damage(subsurface, true);
}

static void subsurface_handle_map(struct wl_listener *listener, void *data) {
	struct sway_session_lock_subsurface *subsurface =
			wl_container_of(listener, subsurface, map);
	subsurface_damage(subsurface, true);
}

static void subsurface_handle_commit(struct wl_listener *listener, void *data) {
	struct sway_session_lock_subsurface *subsurface =
			wl_container_of(listener, subsurface, commit);
	subsurface_damage(subsurface, false);
}

static void subsurface_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct sway_session_lock_subsurface *subsurface =
			wl_container_of(listener, subsurface, destroy);
	lock_subsurface_destroy(subsurface);
}

static void handle_new_subsurface(struct wl_listener *listener, void *data) {
	struct sway_session_lock_surface *sway_lock_surface =
			wl_container_of(listener, sway_lock_surface, new_subsurface);
	struct wlr_subsurface *wlr_subsurface = data;

	struct sway_session_lock_subsurface *subsurface =
			calloc(1, sizeof(struct sway_session_lock_subsurface));
	if (subsurface == NULL) {
		wl_resource_post_no_memory(wlr_subsurface->resource);
		return;
	}

	subsurface->wlr_subsurface = wlr_subsurface;
	subsurface->lock_surface = sway_lock_surface;
	wl_list_insert(&sway_lock_surface->subsurfaces, &subsurface->link);

	subsurface->map.notify = subsurface_handle_map;
	wl_signal_add(&wlr_subsurface->events.map, &subsurface->map);
	subsurface->unmap.notify = subsurface_handle_unmap;
	wl_signal_add(&wlr_subsurface->events.unmap, &subsurface->unmap);
	subsurface->destroy.notify = subsurface_handle_destroy;
	wl_signal_add(&wlr_subsurface->events.destroy, &subsurface->destroy);
	subsurface->commit.notify = subsurface_handle_commit;
	wl_signal_add(&wlr_subsurface->surface->events.commit, &subsurface->commit);
}

static void handle_new_surface(struct wl_listener *listener, void *data) {
	struct wlr_session_lock_surface_v1 *lock_surface = data;
	struct sway_session_lock_surface *surf = calloc(1, sizeof(*surf));
	if (surf == NULL) {
		return;
	}

	sway_log(SWAY_DEBUG, "new lock layer surface");

	struct sway_output *output = lock_surface->output->data;
	wlr_session_lock_surface_v1_configure(lock_surface, output->width, output->height);

	surf->lock_surface = lock_surface;
	surf->surface = lock_surface->surface;
	surf->output = output;
	surf->map.notify = handle_surface_map;
	wl_signal_add(&lock_surface->events.map, &surf->map);
	surf->destroy.notify = handle_surface_destroy;
	wl_signal_add(&lock_surface->events.destroy, &surf->destroy);
	surf->surface_commit.notify = handle_surface_commit;
	wl_signal_add(&surf->surface->events.commit, &surf->surface_commit);
	surf->new_subsurface.notify = handle_new_subsurface;
	wl_signal_add(&surf->surface->events.new_subsurface, &surf->new_subsurface);
	surf->output_mode.notify = handle_output_mode;
	wl_signal_add(&output->wlr_output->events.mode, &surf->output_mode);
	surf->output_commit.notify = handle_output_commit;
	wl_signal_add(&output->wlr_output->events.commit, &surf->output_commit);

	wl_list_init(&surf->subsurfaces);
}

static void handle_unlock(struct wl_listener *listener, void *data) {
	sway_log(SWAY_DEBUG, "session unlocked");
	server.session_lock.locked = false;
	server.session_lock.lock = NULL;

	wl_list_remove(&server.session_lock.lock_new_surface.link);
	wl_list_remove(&server.session_lock.lock_unlock.link);
	wl_list_remove(&server.session_lock.lock_destroy.link);

	struct sway_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		seat_set_exclusive_client(seat, NULL);
		// copied from seat_set_focus_layer -- deduplicate?
		struct sway_node *previous = seat_get_focus_inactive(seat, &root->node);
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

static void handle_abandon(struct wl_listener *listener, void *data) {
	sway_log(SWAY_INFO, "session lock abandoned");
	server.session_lock.lock = NULL;

	wl_list_remove(&server.session_lock.lock_new_surface.link);
	wl_list_remove(&server.session_lock.lock_unlock.link);
	wl_list_remove(&server.session_lock.lock_destroy.link);

	struct sway_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		seat->exclusive_client = NULL;
	}

	// redraw everything
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		output_damage_whole(output);
	}
}

static void handle_session_lock(struct wl_listener *listener, void *data) {
	struct wlr_session_lock_v1 *lock = data;
	struct wl_client *client = wl_resource_get_client(lock->resource);

	if (server.session_lock.lock) {
		wlr_session_lock_v1_destroy(lock);
		return;
	}

	sway_log(SWAY_DEBUG, "session locked");
	server.session_lock.locked = true;
	server.session_lock.lock = lock;

	struct sway_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		seat_set_exclusive_client(seat, client);
	}

	wl_signal_add(&lock->events.new_surface, &server.session_lock.lock_new_surface);
	wl_signal_add(&lock->events.unlock, &server.session_lock.lock_unlock);
	wl_signal_add(&lock->events.destroy, &server.session_lock.lock_destroy);

	wlr_session_lock_v1_send_locked(lock);

	// redraw everything
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		output_damage_whole(output);
	}
}

static void handle_session_lock_destroy(struct wl_listener *listener, void *data) {
	assert(server.session_lock.lock == NULL);
	wl_list_remove(&server.session_lock.new_lock.link);
	wl_list_remove(&server.session_lock.manager_destroy.link);
}

void sway_session_lock_init(void) {
	server.session_lock.manager = wlr_session_lock_manager_v1_create(server.wl_display);

	server.session_lock.lock_new_surface.notify = handle_new_surface;
	server.session_lock.lock_unlock.notify = handle_unlock;
	server.session_lock.lock_destroy.notify = handle_abandon;
	server.session_lock.new_lock.notify = handle_session_lock;
	server.session_lock.manager_destroy.notify = handle_session_lock_destroy;
	wl_signal_add(&server.session_lock.manager->events.new_lock,
		&server.session_lock.new_lock);
	wl_signal_add(&server.session_lock.manager->events.destroy,
		&server.session_lock.manager_destroy);
}
