#define E_COMP_WL
#include "e_mod_main_wl.h"
#include <string.h>

static Eina_Bool _e_keyrouter_send_key_events(int type, Ecore_Event_Key *ev);
static Eina_Bool _e_keyrouter_send_key_events_press(int type, Ecore_Event_Key *ev);
static Eina_Bool _e_keyrouter_send_key_events_release(int type, Ecore_Event_Key *ev);
static void _e_keyrouter_send_key_event(int type, struct wl_resource *surface, struct wl_client *wc, Ecore_Event_Key *ev);

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

   /* Deliver release  clean up pressed key list */
   EINA_LIST_FREE(krt->HardKeys[ev->keycode].press_ptr, key_node_data)
     {
        if (key_node_data)
          {
             _e_keyrouter_send_key_event(type, key_node_data->surface, key_node_data->wc, ev);
             KLINF("Release Pair : Key %s(%s:%d) ===> E_Client (%p) WL_Client (%p) (pid: %d)\n",
                      ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                      key_node_data->surface, key_node_data->wc, e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface));
             E_FREE(key_node_data);
          }
     }
   krt->HardKeys[ev->keycode].press_ptr = NULL;

   return EINA_TRUE;
}

static Eina_Bool
_e_keyrouter_send_key_events_press(int type, Ecore_Event_Key *ev)
{
   unsigned int keycode = ev->keycode;
   struct wl_resource *surface_focus = NULL;
   E_Client *ec_focus = NULL;

   E_Keyrouter_Key_List_NodePtr key_node_data;
   Eina_List *l = NULL;

   EINA_LIST_FOREACH(krt->HardKeys[keycode].excl_ptr, l, key_node_data)
     {
        if (key_node_data)
          {
             _e_keyrouter_send_key_event(type, key_node_data->surface, key_node_data->wc, ev);
             KLINF("EXCLUSIVE Mode : Key %s(%s:%d) ===> Surface (%p) WL_Client (%p) (pid: %d)\n",
                      ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                      key_node_data->surface, key_node_data->wc, e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface));

             return EINA_TRUE;
          }
     }

   EINA_LIST_FOREACH(krt->HardKeys[keycode].or_excl_ptr, l, key_node_data)
     {
        if (key_node_data)
          {
             _e_keyrouter_send_key_event(type, key_node_data->surface, key_node_data->wc, ev);
             KLINF("OVERRIDABLE_EXCLUSIVE Mode : Key %s(%s:%d) ===> Surface (%p) WL_Client (%p) (pid: %d)\n",
                     ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                     key_node_data->surface, key_node_data->wc, e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface));

             return EINA_TRUE;
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
                       _e_keyrouter_send_key_event(type, key_node_data->surface, NULL, ev);
                       KLINF("TOPMOST (TOP_POSITION) Mode : Key %s (%s:%d) ===> Surface (%p) (pid: %d)\n",
                                ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                                key_node_data->surface, e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface));

                       return EINA_TRUE;
                    }
                  krt->isWindowStackChanged = EINA_FALSE;

                  if (_e_keyrouter_check_top_visible_window(ec_focus, keycode))
                    {
                       _e_keyrouter_send_key_event(type, key_node_data->surface, NULL, ev);
                       KLINF("TOPMOST (TOP_POSITION) Mode : Key %s (%s:%d) ===> Surface (%p) (pid: %d)\n",
                             ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                             key_node_data->surface, e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface));

                       return EINA_TRUE;
                    }
                  break;
               }
          }
     }

   if (krt->HardKeys[keycode].shared_ptr)
     {
        _e_keyrouter_send_key_event(type, surface_focus, NULL, ev);
        KLINF("SHARED [Focus client] : Key %s (%s:%d) ===> Surface (%p) (pid: %d)\n",
                 ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up "), ev->keyname, ev->keycode,
                 surface_focus, e_keyrouter_util_get_pid(NULL, surface_focus));

        EINA_LIST_FOREACH(krt->HardKeys[keycode].shared_ptr, l, key_node_data)
          {
             if (key_node_data)
               {
                  if (key_node_data->surface)
                    {
                       if (key_node_data->surface != surface_focus)
                         {
                            _e_keyrouter_send_key_event(type, key_node_data->surface, key_node_data->wc, ev);
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
                            _e_keyrouter_send_key_event(type, key_node_data->surface, key_node_data->wc, ev);
                            KLINF("SHARED Mode : Key %s(%s:%d) ===> Surface (%p) WL_Client (%p) (pid: %d)\n",
                                     ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                                     key_node_data->surface, key_node_data->wc, e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface));
                         }
                    }
               }
          }

        return EINA_TRUE;
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

   if (!krt->HardKeys[keycode].registered_ptr)
     {
        KLDBG("This keycode is not registered\n");
        return EINA_FALSE;
     }

   _e_keyrouter_send_key_event(type, krt->HardKeys[keycode].registered_ptr->surface, NULL, ev);
   KLINF("REGISTER Mode : Key %s(%s:%d) ===> Surface (%p) (pid: %d)\n",
            ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
            krt->HardKeys[keycode].registered_ptr->surface, e_keyrouter_util_get_pid(NULL, krt->HardKeys[keycode].registered_ptr->surface));

   return EINA_TRUE;
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

static void
_e_keyrouter_send_event_device(struct wl_client *wc, uint32_t timestamp, const char *dev_name, uint32_t serial)
{
   const char *last_dev_name;
   E_Comp_Wl_Input_Device *input_dev;
   struct wl_resource *dev_res;
   Eina_List *l, *ll;

   last_dev_name = e_comp_wl->input_device_manager.last_device_name;
   if (!last_dev_name || (last_dev_name && (strcmp(last_dev_name, dev_name))))
     {
        if (last_dev_name)
          eina_stringshare_del(last_dev_name);
        last_dev_name = eina_stringshare_add(dev_name);
        e_comp_wl->input_device_manager.last_device_name = last_dev_name;

        EINA_LIST_FOREACH(e_comp_wl->input_device_manager.device_list, l, input_dev)
          {
             if ((strcmp(input_dev->identifier, dev_name)) ||
                 (input_dev->capability != ECORE_DEVICE_KEYBOARD))
               continue;
             e_comp_wl->input_device_manager.last_device_cap = input_dev->capability;
             EINA_LIST_FOREACH(input_dev->resources, ll, dev_res)
               {
                  if (wl_resource_get_client(dev_res) != wc) continue;
                  tizen_input_device_send_event_device(dev_res, serial, input_dev->identifier, timestamp);
               }
          }
     }
}

/* Function for sending key event to wl_client(s) */
static void
_e_keyrouter_send_key_event(int type, struct wl_resource *surface, struct wl_client *wc, Ecore_Event_Key *ev)
{
   struct wl_client *wc_send;
   struct wl_resource *res;
   const char *dev_name;

   uint evtype;
   uint serial;
   Eina_List *l;

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
        return;
     }

   if (ECORE_EVENT_KEY_DOWN == type)
     {
        e_keyrouter_prepend_to_keylist(surface, wc, ev->keycode, TIZEN_KEYROUTER_MODE_PRESSED);
        evtype = WL_KEYBOARD_KEY_STATE_PRESSED;
     }
   else
     {
        evtype = WL_KEYBOARD_KEY_STATE_RELEASED;
     }

   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   EINA_LIST_FOREACH(e_comp_wl->kbd.resources, l, res)
     {
        if (res)
          {
             if (wl_resource_get_client(res) != wc_send) continue;

             if (ev->dev)
               {
                  dev_name = ecore_device_identifier_get(ev->dev);
                  if (dev_name)
                    _e_keyrouter_send_event_device(wc_send, ev->timestamp, dev_name, serial);
               }
             KLDBG("[time: %d] res: %p, serial: %d send a key(%d):%d to wl_client:%p\n", ev->timestamp, res, serial, (ev->keycode)-8, evtype, wc_send);
             TRACE_INPUT_BEGIN(_e_keyrouter_send_key_event);
             wl_keyboard_send_key(res, serial, ev->timestamp, ev->keycode-8, evtype);
             TRACE_INPUT_END();
          }
     }
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
