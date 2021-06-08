#include "sway/security.h"
#include "sway/server.h"

bool security_global_filter(const struct wl_client *client,
		const struct wl_global *global, void *data) {
	struct sway_server *server = data;
	if (global == server->lock_screen.ext_unlocker_v1_global
		&& client != server->lock_screen.client) {
		// do not provide the unlocker global to non-lockscreen
		// clients. Note: a client that _used_ to be the lock screen
		// will not be sent this global again, but may still have
		// access to it from before.
		return false;
	}
	return true;
}
