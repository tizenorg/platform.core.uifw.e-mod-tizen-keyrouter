#define E_COMP_WL
#include "e_mod_main_wl.h"
#include <string.h>

static Eina_Bool _e_keyrouter_send_key_events(int type, Ecore_Event_Key *ev);
static Eina_Bool _e_keyrouter_send_key_events_press(int type, Ecore_Event_Key *ev);
static Eina_Bool _e_keyrouter_send_key_events_release(int type, Ecore_Event_Key *ev);
static Eina_Bool _e_keyrouter_send_key_event(int type, struct wl_resource *surface, struct wl_client *wc, Ecore_Event_Key *ev, Eina_Bool focused, unsigned int mode);

static Eina_Bool _e_keyrouter_send_key_events_register(int type, Ecore_Event_Key *ev);

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

   return ret;
}

static Eina_Bool
_e_keyrouter_send_key_events_press(int type, Ecore_Event_Key *ev)
{
   unsigned int keycode = ev->keycode;
   struct wl_resource *surface_focus = NULL;
   E_Client *ec_focus = NULL;
   Eina_Bool res = EINA_TRUE;

   E_Keyrouter_Key_List_NodePtr key_node_data;
   Eina_List *l = NULL;

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
        res = _e_keyrouter_send_key_event(type, surface_focus, NULL, ev, EINA_TRUE, TIZEN_KEYROUTER_MODE_SHARED);
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

   if (_e_keyrouter_send_key_events_register(type, ev))
     {
        return EINA_TRUE;
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_keyrouter_send_key_events_register(int type, Ecore_Event_Key *ev)
{
   unsigned int keycode = ev->keycode;
   Eina_Bool res = EINA_TRUE;

   if (!krt->HardKeys[keycode].registered_ptr)
     {
        KLDBG("This keycode is not registered\n");
        return EINA_FALSE;
     }

   res = _e_keyrouter_send_key_event(type, krt->HardKeys[keycode].registered_ptr->surface,
                                     NULL, ev, krt->HardKeys[keycode].registered_ptr->focused,
                                     TIZEN_KEYROUTER_MODE_REGISTERED);
   KLINF("REGISTER Mode : Key %s(%s:%d) ===> Surface (%p) (pid: %d)\n",
            ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
            krt->HardKeys[keycode].registered_ptr->surface, e_keyrouter_util_get_pid(NULL, krt->HardKeys[keycode].registered_ptr->surface));

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
   struct wl_client *wc_send, *wc_focus = NULL;
   struct wl_resource *surface_focus;
   E_Client *ec_focus;
   Ecore_Event_Key *ev_cpy;
   int len;

   ec_focus = e_client_focused_get();
   surface_focus = e_keyrouter_util_get_surface_from_eclient(ec_focus);
   if (surface_focus) wc_focus = wl_resource_get_client(surface_focus);

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

   ev_cpy = (Ecore_Event_Key *)calloc(1, sizeof(Ecore_Event_Key));
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
