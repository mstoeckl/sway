#ifndef _SWAY_SECURITY_H
#define _SWAY_SECURITY_H

#include <stdbool.h>
#include <wayland-server-core.h>

bool security_global_filter(const struct wl_client *client,
	const struct wl_global *global, void *data);

#endif /* _SWAY_SECURITY_H */
