/*
 * Copyright 2012 Samsung Electronics co., Ltd. All Rights Reserved.
 * Contact: Sung-Jin Park (sj76.park@samsung.com),
 * Jeonghyun Kang (jhyuni.kang@samsung.com)
 *
 * Permission to use, copy, modify, distribute, and sell this
 * software and its documentation for any purpose is hereby granted
 * without fee, provided that the above copyright notice appear in
 * all copies and that both that copyright notice and this permission
 * notice appear in supporting documentation, and that the name of
 * the copyright holders not be used in advertising or publicity
 * pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
 * ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
 * THIS SOFTWARE.
 */

#ifndef KEYROUTER_SERVER_PROTOCOL_H
#define KEYROUTER_SERVER_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-server.h"

struct wl_client;
struct wl_resource;

struct wl_keyrouter;

extern const struct wl_interface wl_keyrouter_interface;

#ifndef WL_KEYROUTER_ERROR_ENUM
#define WL_KEYROUTER_ERROR_ENUM
enum wl_keyrouter_error {
	WL_KEYROUTER_ERROR_NONE = 0,
	WL_KEYROUTER_ERROR_INVALID_SURFACE = 1,
	WL_KEYROUTER_ERROR_INVALID_KEY = 2,
	WL_KEYROUTER_ERROR_INVALID_MODE = 3,
	WL_KEYROUTER_ERROR_GRABBED_ALREADY = 4,
	WL_KEYROUTER_ERROR_NO_PERMISSION = 5,
	WL_KEYROUTER_ERROR_NO_SYSTEM_RESOURCES = 6,
};
#endif /* WL_KEYROUTER_ERROR_ENUM */

#ifndef WL_KEYROUTER_MODE_ENUM
#define WL_KEYROUTER_MODE_ENUM
/**
 * wl_keyrouter_mode - mode for a key grab
 * @WL_KEYROUTER_MODE_NONE: none
 * @WL_KEYROUTER_MODE_SHARED: mode to get a key grab with the other
 *	client surfaces when the focused client surface gets the key
 * @WL_KEYROUTER_MODE_TOPMOST: mode to get a key grab when the client
 *	surface is the top most surface
 * @WL_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE: mode to get a key grab
 *	exclusively, overridably regardless of the order in the surface stack
 * @WL_KEYROUTER_MODE_EXCLUSIVE: mode to get a key grab exclusively
 *	regardless of the order in surface stack
 *
 * This value is used to set a mode for a key grab. With this mode and
 * the order of the surface between surfaces' stack, the compositor will
 * determine the destination client surface.
 */
enum wl_keyrouter_mode {
	WL_KEYROUTER_MODE_NONE = 0,
	WL_KEYROUTER_MODE_SHARED = 1,
	WL_KEYROUTER_MODE_TOPMOST = 2,
	WL_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE = 3,
	WL_KEYROUTER_MODE_EXCLUSIVE = 4,
};
#endif /* WL_KEYROUTER_MODE_ENUM */

/**
 * wl_keyrouter - an interface to set each focus for each key
 * @set_keygrab: (none)
 * @unset_keygrab: (none)
 * @get_keygrab_status: (none)
 *
 * In tradition, all the keys in a keyboard and a device on which some
 * keys are attached will be sent to focus surface by default. Currently
 * it's possible to set up each focus for each key in a keyboard and a
 * device. Therefore, by setting a key grab for a surface, the owner of the
 * surface will get the key event when it has the key grab for the key.
 */
struct wl_keyrouter_interface {
	/**
	 * set_keygrab - (none)
	 * @surface: (none)
	 * @key: (none)
	 * @mode: (none)
	 */
	void (*set_keygrab)(struct wl_client *client,
			    struct wl_resource *resource,
			    struct wl_resource *surface,
			    uint32_t key,
			    uint32_t mode);
	/**
	 * unset_keygrab - (none)
	 * @surface: (none)
	 * @key: (none)
	 */
	void (*unset_keygrab)(struct wl_client *client,
			      struct wl_resource *resource,
			      struct wl_resource *surface,
			      uint32_t key);
	/**
	 * get_keygrab_status - (none)
	 * @surface: (none)
	 * @key: (none)
	 */
	void (*get_keygrab_status)(struct wl_client *client,
				   struct wl_resource *resource,
				   struct wl_resource *surface,
				   uint32_t key);
};

#define WL_KEYROUTER_KEYGRAB_NOTIFY	0

#define WL_KEYROUTER_KEYGRAB_NOTIFY_SINCE_VERSION	1

static inline void
wl_keyrouter_send_keygrab_notify(struct wl_resource *resource_, struct wl_resource *surface, uint32_t key, uint32_t mode, uint32_t error)
{
	wl_resource_post_event(resource_, WL_KEYROUTER_KEYGRAB_NOTIFY, surface, key, mode, error);
}

#ifdef  __cplusplus
}
#endif

#endif
