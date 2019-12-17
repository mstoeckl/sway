#ifndef _SWAY_SURFACE_H
#define _SWAY_SURFACE_H
#include <wlr/types/wlr_surface.h>
#include <sway/server.h>

struct sway_surface {
	struct wlr_surface *wlr_surface;

	struct wl_listener destroy;

	/**
         * This event can be used for issuing delayed frame done callbacks (for
	 * example, to improve presentation latency). Its handler is set to a
	 * function that issues a frame done callback to this surface.
	 */
        struct delayed_event frame_done_event;
};

#endif
