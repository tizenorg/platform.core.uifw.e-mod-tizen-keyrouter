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

#include <stdlib.h>
#include <stdint.h>
#include "wayland-util.h"

extern const struct wl_interface wl_surface_interface;

static const struct wl_interface *types[] = {
	&wl_surface_interface,
	NULL,
	NULL,
	&wl_surface_interface,
	NULL,
	&wl_surface_interface,
	NULL,
	&wl_surface_interface,
	NULL,
	NULL,
	NULL,
};

static const struct wl_message wl_keyrouter_requests[] = {
	{ "set_keygrab", "?ouu", types + 0 },
	{ "unset_keygrab", "?ou", types + 3 },
	{ "get_keygrab_status", "?ou", types + 5 },
};

static const struct wl_message wl_keyrouter_events[] = {
	{ "keygrab_notify", "?ouuu", types + 7 },
};

WL_EXPORT const struct wl_interface wl_keyrouter_interface = {
	"wl_keyrouter", 1,
	3, wl_keyrouter_requests,
	1, wl_keyrouter_events,
};

