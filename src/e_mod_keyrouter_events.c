#define E_COMP_WL
#include "e_mod_main_wl.h"
#include <string.h>

static Eina_Bool _e_keyrouter_send_key_events(int type, Ecore_Event_Key *ev);
static Eina_Bool _e_keyrouter_send_key_events_press(int type, Ecore_Event_Key *ev);
static Eina_Bool _e_keyrouter_send_key_events_release(int type, Ecore_Event_Key *ev);
static Eina_Bool _e_keyrouter_send_key_event(int type, struct wl_resource *surface, struct wl_client *wc, Ecore_Event_Key *ev, Eina_Bool focused, unsigned int mode);

static Eina_Bool _e_keyrouter_send_key_events_focus(int type, struct wl_resource *surface, Ecore_Event_Key *ev, struct wl_resource *delivered_surface);

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
        krt->HardKeys[key].shared_ptr)
     {
        return EINA_TRUE;
     }

   return EINA_FALSE;
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
   if(krt->playback_daemon_surface)
     {
       struct wl_client *wc = wl_resource_get_client(krt->playback_daemon_surface);
       if(wc)
          {
            _e_keyrouter_send_key_event(type, krt->playback_daemon_surface, wc, ev, EINA_FALSE, TIZEN_KEYROUTER_MODE_REGISTERED);
            KLDBG("Sent key to playback-daemon\n");
          }
     }

   if (krt->max_tizen_hwkeys < ev->keycode)
     {
        KLWRN("The key(%d) is too larger to process keyrouting: Invalid keycode\n", ev->keycode);
        goto finish;
     }
 /* JEON_CHECK: check keycode or is key grabbed */
   if ((ECORE_EVENT_KEY_DOWN == type) && !krt->HardKeys[ev->keycode].keycode)
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
   /* Call process key combination to lookup for any particular combinaton */
   e_keyrouter_process_key_combination(ev->timestamp, ev->keycode, type);
   KLDBG("[%s] keyname: %s, key: %s, keycode: %d\n", (type == ECORE_EVENT_KEY_DOWN) ? "KEY_PRESS" : "KEY_RELEASE", ev->keyname, ev->key, ev->keycode);
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
   if (ECORE_EVENT_KEY_DOWN == type)
     {
        res = _e_keyrouter_send_key_events_press(type, ev);
     }
  else
     {
        res = _e_keyrouter_send_key_events_release(type, ev);
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
   struct wl_resource *surface_focus = NULL;
   E_Client *ec_focus = NULL;
   struct wl_resource *delivered_surface = NULL;
   Eina_Bool res = EINA_TRUE;

   E_Keyrouter_Key_List_NodePtr key_node_data;
   Eina_List *l = NULL;

   ec_focus = e_client_focused_get();
   surface_focus = e_keyrouter_util_get_surface_from_eclient(ec_focus);

   if(krt->isPictureOffEnabled == 1)
     {
       EINA_LIST_FOREACH(krt->HardKeys[keycode].pic_off_ptr, l, key_node_data)
          {
            if (key_node_data)
                {
                 res = _e_keyrouter_send_key_event(type, key_node_data->surface, key_node_data->wc, ev, key_node_data->focused, TIZEN_KEYROUTER_MODE_REGISTERED);
                 KLINF("PICTURE OFF Mode : Key %s(%d) ===> Surface (%p) WL_Client (%p)\n",
                       ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keycode, key_node_data->surface, key_node_data->wc);
                }
          }
       return res;
     }
   if(!_e_keyrouter_is_key_grabbed(ev->keycode))
     {
       res = _e_keyrouter_send_key_events_focus(type, surface_focus, ev, delivered_surface);
       return res;
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
       goto need_shared;
     }

   if (krt->HardKeys[keycode].shared_ptr)
     {
need_shared:
        //res = _e_keyrouter_send_key_event(type, surface_focus, NULL, ev, EINA_TRUE, TIZEN_KEYROUTER_MODE_SHARED);
        res = _e_keyrouter_send_key_events_focus(type, surface_focus, ev, delivered_surface);
        EINA_LIST_FOREACH(krt->HardKeys[keycode].shared_ptr, l, key_node_data)
          {
             if (key_node_data)
               {
                  if( delivered_surface && key_node_data->surface == delivered_surface)
                    {
                       // Check for already delivered surface
                       // do not deliver double events in this case.
                       continue;
                    }
                 else
                   {
                      _e_keyrouter_send_key_event(type, key_node_data->surface, key_node_data->wc, ev, key_node_data->focused, TIZEN_KEYROUTER_MODE_SHARED);
                      KLINF("SHARED Mode : Key %s(%s:%d) ===> Surface (%p) WL_Client (%p) (pid: %d)\n",
                            ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                            key_node_data->surface, key_node_data->wc, e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface));
               }
           }
       }
       return res;
    }
#if 0
        KLINF("SHARED [Focus client] : Key %s (%s:%d) ===> Surface (%p) (pid: %d)\n",
                 ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up "), ev->keyname, ev->keycode,
                 surface_focus, e_keyrouter_util_get_pid(NULL, surface_focus));
        e_keyrouter_add_surface_destroy_listener(surface_focus);

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
#endif
   return EINA_FALSE;
}

static Eina_Bool
_e_keyrouter_send_key_events_focus(int type, struct wl_resource *surface_focus,  Ecore_Event_Key *ev, struct wl_resource *delivered_surface)
{
   E_Client *ec_top = NULL, *ec_focus = NULL;
   Eina_Bool below_focus = EINA_FALSE;
   struct wl_resource *surface = NULL;
   Eina_List* key_list = NULL;
   int *key_data = NULL;
   Eina_List *ll = NULL;
   int deliver_invisible = 0;
   Eina_Bool res = EINA_TRUE;

   ec_top = e_client_top_get();
   ec_focus = e_client_focused_get();

   // loop over to next window from top of window stack
   for (; ec_top != NULL; ec_top = e_client_below_get(ec_top))
     {
        surface = e_keyrouter_util_get_surface_from_eclient(ec_top);
        if(surface == NULL)
          {
             // Not a valid surface.
             continue;
          }

        // Check if window stack reaches to focus window
        if (ec_top == ec_focus)
          {
             KLINF("%p is focus client & surface_focus %p. ==> %p\n", ec_top, surface_focus, surface);
             below_focus = EINA_TRUE;
          }

        // Check for FORCE DELIVER to INVISIBLE WINDOW
        if (deliver_invisible && IsInvisibleGetWindow(surface))
          {
             res = _e_keyrouter_send_key_event(type, surface, NULL, ev, EINA_TRUE, TIZEN_KEYROUTER_MODE_REGISTERED);
             KLINF("FORCE DELIVER : Key %s(%s:%d) ===> Surface (%p) (pid: %d)\n",
                   ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                   surface, e_keyrouter_util_get_pid(NULL, surface));
             delivered_surface = surface;
             return res;
          }

        // Check for visible window first <Consider VISIBILITY>
        // return if not visible
        if (ec_top->visibility.obscured == E_VISIBILITY_FULLY_OBSCURED || ec_top->visibility.obscured == E_VISIBILITY_UNKNOWN)
          {
             continue;
          }

        // Set key Event Delivery for INVISIBLE WINDOW
        if (IsInvisibleSetWindow(surface))
          {
             deliver_invisible = 1;
          }

        if (IsNoneKeyRegisterWindow(surface))
          {
             // Registered None property is set for this surface
             // No event will be delivered to this surface.
             KLINF("Surface(%p) is a none register window.\n", surface);
             continue;
          }

        if (e_keyrouter_is_registered_window(surface))
          {
             // get the key list and deliver events if it has registered for that key
             // Write a function to get the key list for register window.
             key_list = _e_keyrouter_registered_window_key_list(surface);
             if (key_list)
               {
                  EINA_LIST_FOREACH(key_list, ll, key_data)
                    {
                       if(!key_data) continue;

                       if(*key_data == ev->keycode)
                         {
                            res = _e_keyrouter_send_key_event(type, surface, NULL, ev, EINA_TRUE, TIZEN_KEYROUTER_MODE_REGISTERED);
                            KLINF("REGISTER Mode : Key %s(%s:%d) ===> Surface (%p) (pid: %d)\n",
                                  ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                                  surface, e_keyrouter_util_get_pid(NULL, surface));
                            delivered_surface = surface;
                            krt->isRegisterDelivery = EINA_TRUE;
                            return res;
                         }
                    }
               }
             else
               {
                  KLDBG("Key_list is Null for registered surface %p\n", surface);
               }
          }

        if (surface != surface_focus)
          {
             if (below_focus == EINA_FALSE) continue;

             // Deliver to below Non Registered window
             else if (!e_keyrouter_is_registered_window(surface))
               {
                  res = _e_keyrouter_send_key_event(type, surface, NULL, ev, EINA_TRUE, TIZEN_KEYROUTER_MODE_SHARED);
                  KLINF("NOT REGISTER : Key %s(%s:%d) ===> Surface (%p) (pid: %d)\n",
                        ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                        surface, e_keyrouter_util_get_pid(NULL, surface));
                  delivered_surface = surface;
                  return res;
               }
             else continue;
          }
        else
          {
             // Deliver to Focus if Non Registered window
             if (!e_keyrouter_is_registered_window(surface))
               {
                  res = _e_keyrouter_send_key_event(type, surface, NULL,ev, EINA_TRUE, TIZEN_KEYROUTER_MODE_SHARED);
                  KLINF("FOCUS : Key %s(%s:%d) ===> Surface (%p) (pid: %d)\n",
                        ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                        surface, e_keyrouter_util_get_pid(NULL, surface));
                  delivered_surface = surface;
                  return res;
               }
             else continue;
          }
    }

    KLINF("Couldnt Deliver key:(%s:%d) to any window. Focused Surface: %p\n", ev->keyname, ev->keycode, surface_focus);
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
   struct wl_client *wc_send;
   Ecore_Event_Key *ev_cpy;
   int len;

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
            mode == TIZEN_KEYROUTER_MODE_REGISTERED)
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

   KLDBG("Generate new key event! wc_send: %p(%d)\n", wc_send, e_keyrouter_util_get_pid(wc_send, NULL));

   len = sizeof(Ecore_Event_Key) + strlen(ev->key) + strlen(ev->keyname) + ((ev->compose) ? strlen(ev->compose) : 0) + 3;
   ev_cpy = calloc(1, len);
   memcpy(ev_cpy, ev, len);
   ev_cpy->data = wc_send;

   if (ECORE_EVENT_KEY_DOWN == type)
     ecore_event_add(ECORE_EVENT_KEY_DOWN, ev_cpy, NULL, NULL);
   else
     ecore_event_add(ECORE_EVENT_KEY_UP, ev_cpy, NULL, NULL);

   return EINA_TRUE;
}

struct wl_resource *
e_keyrouter_util_get_surface_from_eclient(E_Client *client)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL
     (client, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL
     (client->comp_data, NULL);

   return client->comp_data->wl_surface;
}

int
e_keyrouter_util_get_pid(struct wl_client *client, struct wl_resource *surface)
{
   pid_t pid = 0;
   uid_t uid = 0;
   gid_t gid = 0;
   struct wl_client *cur_client = NULL;

   if (client) cur_client = client;
   else if (surface) cur_client = wl_resource_get_client(surface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cur_client, 0);

   wl_client_get_credentials(cur_client, &pid, &uid, &gid);

   return pid;
}
