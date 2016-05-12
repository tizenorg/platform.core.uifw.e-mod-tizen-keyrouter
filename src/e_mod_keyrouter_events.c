#define E_COMP_WL
#include "e_mod_main_wl.h"
#include <string.h>

static Eina_Bool _e_keyrouter_send_key_events(int type, Ecore_Event_Key *ev);
static Eina_Bool _e_keyrouter_send_key_events_press(int type, Ecore_Event_Key *ev);
static Eina_Bool _e_keyrouter_send_key_events_release(int type, Ecore_Event_Key *ev);
static Eina_Bool _e_keyrouter_send_key_event(int type, struct wl_resource *surface, struct wl_client *wc, Ecore_Event_Key *ev, Eina_Bool focused, unsigned int mode);

static Eina_Bool _e_keyrouter_send_key_events_register(int type, Ecore_Event_Key *ev, struct wl_resource **wl_surface);
static Eina_Bool _e_keyrouter_send_key_events_focus(int type, Ecore_Event_Key *ev, struct wl_resource *focus);

static Eina_Bool _e_keyrouter_is_key_grabbed(int key);
static Eina_Bool _e_keyrouter_check_top_visible_window(E_Client *ec_focus, int arr_idx);

static Eina_Bool
_e_keyrouter_is_key_grabbed(int key)
{
   if (!krt->HardKeys[key].keycode)
     {
        return EINA_FALSE;
     }
   if (krt->HardKeys[key].excl_ptr ||
        krt->HardKeys[key].or_excl_ptr ||
        krt->HardKeys[key].top_ptr ||
        krt->HardKeys[key].shared_ptr ||
        krt->HardKeys[key].registered_ptr)
     {
        return EINA_TRUE;
     }

   return EINA_FALSE;
}

static void
_e_keyrouter_event_generate_key(Ecore_Event_Key *ev, int type, struct wl_client *send_surface)
{
   Ecore_Event_Key *ev_cpy;
   int len;

   KLDBG("Generate new key event! wc_send: %p(%d)\n", send_surface, e_keyrouter_util_get_pid(send_surface, NULL));

   len = sizeof(Ecore_Event_Key) + strlen(ev->key) + strlen(ev->keyname) + ((ev->compose) ? strlen(ev->compose) : 0) + 3;
   ev_cpy = (Ecore_Event_Key *)calloc(1, len);
   ev_cpy = calloc(1, len);
   memcpy(ev_cpy, ev, len);
   ev_cpy->data = send_surface;

   if (ECORE_EVENT_KEY_DOWN == type)
     ecore_event_add(ECORE_EVENT_KEY_DOWN, ev_cpy, NULL, NULL);
   else
     ecore_event_add(ECORE_EVENT_KEY_UP, ev_cpy, NULL, NULL);
}

/* Function for checking the existing grab for a key and sending key event(s) */
Eina_Bool
e_keyrouter_process_key_event(void *event, int type)
{
   Eina_Bool res = EINA_TRUE;
   Ecore_Event_Key *ev = event;

   if (!ev) goto finish;

   KLDBG("[%s] keyname: %s, key: %s, keycode: %d\n", (type == ECORE_EVENT_KEY_DOWN) ? "KEY_PRESS" : "KEY_RELEASE", ev->keyname, ev->key, ev->keycode);

   if (ev->data)
     {
        KLDBG("data is exist send to compositor: %p\n", ev->data);
        goto finish;
     }

   if (krt->max_tizen_hwkeys < ev->keycode)
     {
        KLWRN("The key(%d) is too larger to process keyrouting: Invalid keycode\n", ev->keycode);
        goto finish;
     }

   if ((ECORE_EVENT_KEY_DOWN == type) && (!_e_keyrouter_is_key_grabbed(ev->keycode)))
     {
        KLDBG("The press key(%d) isn't a grabbable key or has not been grabbed yet !\n", ev->keycode);
        goto finish;
     }

   if ((ECORE_EVENT_KEY_UP == type) && (!krt->HardKeys[ev->keycode].press_ptr))
     {
        KLDBG("The release key(%d) isn't a grabbable key or has not been grabbed yet !\n", ev->keycode);
        goto finish;
     }

   //KLDBG("The key(%d) is going to be sent to the proper wl client(s) !\n", ev->keycode);

  if (_e_keyrouter_send_key_events(type, ev))
    res = EINA_FALSE;

finish:
   return res;
}

/* Function for sending key events to wl_client(s) */
static Eina_Bool
_e_keyrouter_send_key_events(int type, Ecore_Event_Key *ev)
{
   Eina_Bool res;
   Eina_List *l;
   struct wl_resource *data;

   EINA_LIST_FOREACH(krt->monitor_window_list, l, data)
     {
        KLINF("MONITOR : Key %s(%d) ===> Surface (%p)(pid: %d)\n",
              ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keycode, data, e_keyrouter_util_get_pid(NULL, data));
        _e_keyrouter_event_generate_key(ev, type, wl_resource_get_client(data));
     }

   if (ECORE_EVENT_KEY_DOWN == type)
     {
        res = _e_keyrouter_send_key_events_press(type, ev);
        e_keyrouter_modkey_check(ev);
     }
  else
     {
        res = _e_keyrouter_send_key_events_release(type, ev);
        e_keyrouter_modkey_mod_clean_up();
        e_keyrouter_stepkey_check(ev);
     }
  return res;
}

static Eina_Bool
_e_keyrouter_send_key_events_release(int type, Ecore_Event_Key *ev)
{
   E_Keyrouter_Key_List_NodePtr key_node_data;
   Eina_Bool res = EINA_TRUE, ret = EINA_TRUE;

   /* Deliver release  clean up pressed key list */
   EINA_LIST_FREE(krt->HardKeys[ev->keycode].press_ptr, key_node_data)
     {
        if (key_node_data)
          {
             res = _e_keyrouter_send_key_event(type, key_node_data->surface, key_node_data->wc, ev,
                                               key_node_data->focused, TIZEN_KEYROUTER_MODE_PRESSED);
             KLINF("Release Pair : Key %s(%s:%d)(Focus: %d) ===> E_Client (%p) WL_Client (%p) (pid: %d)\n",
                      ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode, key_node_data->focused,
                      key_node_data->surface, key_node_data->wc, e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface));
             E_FREE(key_node_data);
             if (res == EINA_FALSE) ret = EINA_FALSE;
          }
     }
   krt->HardKeys[ev->keycode].press_ptr = NULL;
   krt->isRegisterDelivery = EINA_FALSE;

   return ret;
}

static Eina_Bool
_e_keyrouter_send_key_events_press(int type, Ecore_Event_Key *ev)
{
   unsigned int keycode = ev->keycode;
   struct wl_resource *surface_focus = NULL, *delivered_surface = NULL;
   E_Client *ec_focus = NULL;
   Eina_Bool res = EINA_TRUE;

   E_Keyrouter_Key_List_NodePtr key_node_data;
   Eina_List *l = NULL;

   if (krt->pictureoff_enable == EINA_TRUE)
     {
        EINA_LIST_FOREACH(krt->HardKeys[keycode].excl_ptr, l, key_node_data)
          {
             if (key_node_data)
               {
                  res = _e_keyrouter_send_key_event(type, key_node_data->surface, key_node_data->wc, ev,
                                                    key_node_data->focused, TIZEN_KEYROUTER_MODE_PICTUREOFF);
                  KLINF("PICTUREOFF Mode : Key %s(%s:%d) ===> Surface (%p) WL_Client (%p) (pid: %d)\n",
                           ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                           key_node_data->surface, key_node_data->wc, e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface));
     
                  return res;
               }
          }
     }

   EINA_LIST_FOREACH(krt->HardKeys[keycode].excl_ptr, l, key_node_data)
     {
        if (key_node_data)
          {
             res = _e_keyrouter_send_key_event(type, key_node_data->surface, key_node_data->wc, ev,
                                               key_node_data->focused, TIZEN_KEYROUTER_MODE_EXCLUSIVE);
             KLINF("EXCLUSIVE Mode : Key %s(%s:%d) ===> Surface (%p) WL_Client (%p) (pid: %d)\n",
                      ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                      key_node_data->surface, key_node_data->wc, e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface));

             return res;
          }
     }

   EINA_LIST_FOREACH(krt->HardKeys[keycode].or_excl_ptr, l, key_node_data)
     {
        if (key_node_data)
          {
             res = _e_keyrouter_send_key_event(type, key_node_data->surface, key_node_data->wc, ev,
                                               key_node_data->focused, TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE);
             KLINF("OVERRIDABLE_EXCLUSIVE Mode : Key %s(%s:%d) ===> Surface (%p) WL_Client (%p) (pid: %d)\n",
                     ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                     key_node_data->surface, key_node_data->wc, e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface));

             return res;
          }
     }

   ec_focus = e_client_focused_get();
   surface_focus = e_keyrouter_util_get_surface_from_eclient(ec_focus);

   // Top position grab must need a focus surface.
   if (surface_focus)
     {
        EINA_LIST_FOREACH(krt->HardKeys[keycode].top_ptr, l, key_node_data)
          {
             if (key_node_data)
               {
                  if ((EINA_FALSE == krt->isWindowStackChanged) && (surface_focus == key_node_data->surface))
                    {
                       res = _e_keyrouter_send_key_event(type, key_node_data->surface, NULL, ev, key_node_data->focused,
                                                         TIZEN_KEYROUTER_MODE_TOPMOST);
                       KLINF("TOPMOST (TOP_POSITION) Mode : Key %s (%s:%d) ===> Surface (%p) (pid: %d)\n",
                                ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                                key_node_data->surface, e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface));

                       return res;
                    }
                  krt->isWindowStackChanged = EINA_FALSE;

                  if (_e_keyrouter_check_top_visible_window(ec_focus, keycode))
                    {
                       res = _e_keyrouter_send_key_event(type, key_node_data->surface, NULL, ev, key_node_data->focused,
                                                         TIZEN_KEYROUTER_MODE_TOPMOST);
                       KLINF("TOPMOST (TOP_POSITION) Mode : Key %s (%s:%d) ===> Surface (%p) (pid: %d)\n",
                             ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                             key_node_data->surface, e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface));

                       return res;
                    }
                  break;
               }
          }
     }

   if (krt->HardKeys[keycode].shared_ptr)
     {
        /* Deliver key to focus or registered surface */
        _e_keyrouter_send_key_events_register(type, ev, &delivered_surface);

        EINA_LIST_FOREACH(krt->HardKeys[keycode].shared_ptr, l, key_node_data)
          {
             if (key_node_data)
               {
                  if (key_node_data->surface)
                    {
                       if (key_node_data->surface != surface_focus)
                         {
                            _e_keyrouter_send_key_event(type, key_node_data->surface,
                                                        key_node_data->wc, ev, EINA_FALSE,
                                                        TIZEN_KEYROUTER_MODE_SHARED);
                            KLINF("SHARED Mode : Key %s(%s:%d) ===> Surface (%p) WL_Client (%p) (pid: %d)\n",
                                     ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                                     key_node_data->surface, key_node_data->wc, e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface));
                         }
                    }
                  else
                    {
                       if (((surface_focus) && (key_node_data->wc != wl_resource_get_client(surface_focus))) ||
                           (!surface_focus))
                         {
                            _e_keyrouter_send_key_event(type, key_node_data->surface,
                                                        key_node_data->wc, ev, EINA_FALSE,
                                                        TIZEN_KEYROUTER_MODE_SHARED);
                            KLINF("SHARED Mode : Key %s(%s:%d) ===> Surface (%p) WL_Client (%p) (pid: %d)\n",
                                     ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                                     key_node_data->surface, key_node_data->wc, e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface));
                         }
                    }
               }
          }

        return res;
     }

   if (_e_keyrouter_send_key_events_register(type, ev, &delivered_surface))
     {
        return EINA_TRUE;
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_keyrouter_send_key_events_focus(int type, Ecore_Event_Key *ev, struct wl_resource *focus)
{
   Eina_Bool res = EINA_TRUE;

   res = _e_keyrouter_send_key_event(type, focus, NULL, ev, EINA_TRUE, TIZEN_KEYROUTER_MODE_SHARED);
   KLINF("FOCUS : Key %s (%s:%d) ===> Surface (%p) (pid: %d)\n",
            ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up "), ev->keyname, ev->keycode,
            focus, e_keyrouter_util_get_pid(NULL, focus));

   e_keyrouter_add_surface_destroy_listener(focus);
   return res;
}

static Eina_Bool
_e_keyrouter_send_key_events_register(int type, Ecore_Event_Key *ev, struct wl_resource **delivered_surface)
{
   Eina_Bool res = EINA_TRUE;
   struct wl_resource *surface_focus, *surface;
   E_Client *ec_top = NULL;
   Eina_Bool deliver_invisible = EINA_FALSE;
   Eina_Bool registered_window = EINA_FALSE;
   Eina_Bool below_focus = EINA_FALSE;
   Eina_List *l, *ll;
   E_Keyrouter_Registered_Window_Info *data;
   int *keycode_data;

   surface_focus = e_keyrouter_util_get_surface_from_eclient(e_client_focused_get());

   /* Added this code to prevent unnecessary window stack explore.
    * However I'm not sure this code works well in TV platform.
    * For this exception work well, all of keys are registered by some clients in TV platform
    */
#if 0
   if (!krt->HardKeys[keycode].registered_ptr)
     {
        KLDBG("This keycode is not registered\n");
        res = _e_keyrouter_send_key_events_focus(type, ev, surface_focus);
        *delivered_surface = surface_focus;
        return res;
     }
#endif

   for (ec_top = e_client_top_get(); ec_top != NULL; ec_top = e_client_below_get(ec_top))
     {
        surface = e_keyrouter_util_get_surface_from_eclient(ec_top);
        if (!surface) continue;

        registered_window = EINA_FALSE;

        if (surface == surface_focus) below_focus = EINA_TRUE;

        if (deliver_invisible && e_keyrouter_is_register_grab_window(surface))
          {
              res = _e_keyrouter_send_key_event(type, surface, NULL, ev, EINA_TRUE, TIZEN_KEYROUTER_MODE_REGISTERED);
              KLINF("REGISTER [GRAB] : Key %s(%d) ===> Surface (%p)(pid: %d)\n",
                      ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keycode, surface, e_keyrouter_util_get_pid(NULL, surface));
              *delivered_surface = surface;
              break;
          }

        if (ec_top->visibility.obscured == E_VISIBILITY_FULLY_OBSCURED ||
            ec_top->visibility.obscured == E_VISIBILITY_UNKNOWN)
          {
             continue;
          }

        if (e_keyrouter_is_register_pass_window(surface)) deliver_invisible = 1;

        if (e_keyrouter_is_none_register_window(surface)) continue;

        EINA_LIST_FOREACH(krt->registered_window_list, l, data)
          {
             if (data->surface == surface)
               {
                  registered_window = EINA_TRUE;
                  EINA_LIST_FOREACH(data->keys, ll, keycode_data)
                    {
                       if (*keycode_data == ev->keycode)
                         {
                            res = _e_keyrouter_send_key_event(type, surface, NULL, ev, EINA_TRUE, TIZEN_KEYROUTER_MODE_REGISTERED);
                            KLINF("REGISTER : Key %s(%d) ===> Surface (%p)(pid: %d)\n",
                                  ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keycode, surface, e_keyrouter_util_get_pid(NULL, surface));
                            *delivered_surface = surface;
                            krt->isRegisterDelivery = EINA_TRUE;
                            break;
                         }
                    }
               }
             if (*delivered_surface) break;
          }
        if (*delivered_surface) break;

        if (registered_window == EINA_FALSE && below_focus == EINA_TRUE)
          {
             if (surface == surface_focus)
               {
                  res = _e_keyrouter_send_key_events_focus(type, ev, surface_focus);
               }
             else
               {
                  res = _e_keyrouter_send_key_event(type, surface, NULL, ev, EINA_TRUE, TIZEN_KEYROUTER_MODE_REGISTERED);
                  KLINF("NOFOCUS : Key %s(%d) ===> Surface (%p)(pid: %d)\n",
                        ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keycode, surface, e_keyrouter_util_get_pid(NULL, surface));
               }
             *delivered_surface = surface;
             break;
          }
     }

   return res;
}

static Eina_Bool
_e_keyrouter_check_top_visible_window(E_Client *ec_focus, int arr_idx)
{
   E_Client *ec_top = NULL;
   Eina_List *l = NULL, *l_next = NULL;
   E_Keyrouter_Key_List_NodePtr key_node_data = NULL;

   ec_top = e_client_top_get();

   while (ec_top)
     {
        if (!ec_top->visible && ec_top == ec_focus)
          {
             KLDBG("Top Client(%p) is invisible(%d) but focus client\n", ec_top, ec_top->visible);
             return EINA_FALSE;
          }

        /* TODO: Check this client is located inside a display boundary */

        EINA_LIST_FOREACH_SAFE(krt->HardKeys[arr_idx].top_ptr, l, l_next, key_node_data)
          {
             if (key_node_data)
               {
                  if (ec_top == wl_resource_get_user_data(key_node_data->surface))
                    {
                       krt->HardKeys[arr_idx].top_ptr = eina_list_promote_list(krt->HardKeys[arr_idx].top_ptr, l);
                       KLDBG("Move a client(%p) to first index of list(key: %d)\n",
                                ec_top, arr_idx);
                       return EINA_TRUE;
                    }
               }
          }

        if (ec_top == ec_focus)
          {
             KLDBG("The Client(%p) is a focus client\n", ec_top);
             return EINA_FALSE;
          }

        ec_top = e_client_below_get(ec_top);
     }
   return EINA_FALSE;
}

/* Function for sending key event to wl_client(s) */
static Eina_Bool
_e_keyrouter_send_key_event(int type, struct wl_resource *surface, struct wl_client *wc, Ecore_Event_Key *ev, Eina_Bool focused, unsigned int mode)
{
   struct wl_client *wc_send = NULL;

   if (surface == NULL)
     {
        wc_send = wc;
     }
   else
     {
        wc_send = wl_resource_get_client(surface);
     }

   if (!wc_send)
     {
        KLWRN("surface: %p or wc: %p returns null wayland client\n", surface, wc);
        return EINA_FALSE;
     }

   if (ECORE_EVENT_KEY_DOWN == type)
     {
        if (mode == TIZEN_KEYROUTER_MODE_EXCLUSIVE ||
            mode == TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE ||
            mode == TIZEN_KEYROUTER_MODE_TOPMOST ||
            mode == TIZEN_KEYROUTER_MODE_REGISTERED ||
            mode == TIZEN_KEYROUTER_MODE_PICTUREOFF)
          {
             focused = EINA_TRUE;
             ev->data = wc_send;
             KLDBG("Send only one key! wc_send: %p(%d)\n", wc_send, e_keyrouter_util_get_pid(wc_send, NULL));
          }
        else if (focused == EINA_TRUE)
          {
             ev->data = wc_send;
          }
        e_keyrouter_prepend_to_keylist(surface, wc, ev->keycode, TIZEN_KEYROUTER_MODE_PRESSED, focused);
     }
   else
     {
        if (focused == EINA_TRUE) ev->data = wc_send;
     }

   if (focused == EINA_TRUE) return EINA_FALSE;

   _e_keyrouter_event_generate_key(ev, type, wc_send);

   return EINA_TRUE;
}
