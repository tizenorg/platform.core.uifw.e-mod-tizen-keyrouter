#include "e.h"
#include "e_mod_main.h"
#include <string.h>

#define LOG_TAG	"KEYROUTER"
#include "dlog.h"

EAPI E_Module_Api e_modapi = { E_MODULE_API_VERSION, "Keyrouter Module of Window Manager" };

KeyRouter krt;

EAPI void *
e_modapi_init(E_Module *m)
{
   if (!_e_keyrouter_init()) return NULL;

   E_LIST_HANDLER_APPEND(krt.handlers, ECORE_X_EVENT_GENERIC,          _e_keyrouter_cb_event_generic,    NULL);
   E_LIST_HANDLER_APPEND(krt.handlers, ECORE_X_EVENT_ANY,              _e_keyrouter_cb_event_any,        NULL);
   E_LIST_HANDLER_APPEND(krt.handlers, ECORE_X_EVENT_WINDOW_PROPERTY,  _e_keyrouter_cb_window_property,  NULL);
   E_LIST_HANDLER_APPEND(krt.handlers, E_EVENT_CLIENT_STACK,           _e_keyrouter_cb_e_client_stack,   NULL);
   E_LIST_HANDLER_APPEND(krt.handlers, E_EVENT_CLIENT_REMOVE,          _e_keyrouter_cb_e_client_remove,  NULL);
   E_LIST_HANDLER_APPEND(krt.handlers, ECORE_X_EVENT_WINDOW_CREATE,    _e_keyrouter_cb_window_create,    NULL);
   E_LIST_HANDLER_APPEND(krt.handlers, ECORE_X_EVENT_WINDOW_DESTROY,   _e_keyrouter_cb_window_destroy,   NULL);
   E_LIST_HANDLER_APPEND(krt.handlers, ECORE_X_EVENT_WINDOW_CONFIGURE, _e_keyrouter_cb_window_configure, NULL);
   E_LIST_HANDLER_APPEND(krt.handlers, ECORE_X_EVENT_WINDOW_STACK,     _e_keyrouter_cb_window_stack,     NULL);
   E_LIST_HANDLER_APPEND(krt.handlers, ECORE_X_EVENT_CLIENT_MESSAGE,   _e_keyrouter_cb_client_message,   NULL);

   return m;
}

EAPI int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
   Ecore_Event_Handler *h = NULL;

   EINA_LIST_FREE(krt.handlers, h)
     ecore_event_handler_del(h);

   if (krt.e_longpress_timer)
     ecore_timer_del(krt.e_longpress_timer);

   krt.e_longpress_timer = NULL;

   _e_keyrouter_fini();

   return 1;
}

EAPI int
e_modapi_save(E_Module *m EINA_UNUSED)
{
   /* Save something to be kept */
   return 1;
}

static void
_e_keyrouter_x_input_init(void)
{
   int event, error;
   int maj = 2, min = 0;

#ifdef _F_REMAP_MOUSE_BUTTON_TO_HWKEY_
   XIGrabModifiers modifiers[] = {{XIAnyModifier, 0}};
   int nmods = sizeof(modifiers) / sizeof(modifiers[0]);
   int res = 0;
#endif/* _F_REMAP_MOUSE_BUTTON_TO_HWKEY_ */

   if (!XQueryExtension(krt.disp,
                        "XInputExtension",
                        &krt.xi2_opcode,
                        &event,
                        &error))
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt][%s] XInput Extension isn't supported.\n",
             __FUNCTION__);
        krt.xi2_opcode = -1;
        return;
     }

   if (XIQueryVersion(krt.disp, &maj, &min) == BadRequest)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt][%s] Failed to query XI version.\n",
             __FUNCTION__);
        krt.xi2_opcode = -1;
        return;
     }

   memset(&krt.eventmask, 0L, sizeof(XIEventMask));
   krt.eventmask.deviceid = XIAllDevices;
   krt.eventmask.mask_len = XIMaskLen(XI_RawMotion);
   krt.eventmask.mask = calloc(krt.eventmask.mask_len, sizeof(char));

   XISetMask(krt.eventmask.mask, XI_HierarchyChanged);

#ifdef _F_REMAP_MOUSE_BUTTON_TO_HWKEY_
   XISetMask(krt.eventmask.mask, XI_ButtonPress);
   XISetMask(krt.eventmask.mask, XI_ButtonRelease);

   res = XIGrabButton(krt.disp,
                      XIAllDevices,
                      2,
                      krt.rootWin,
                      NULL,
                      GrabModeAsync,
                      GrabModeAsync,
                      False,
                      &krt.eventmask,
                      nmods,
                      modifiers);
   if (res < 0)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt][%s] Failed to XIGrabButton(2), res=%d\n",
             __FUNCTION__, res);
     }

   res = XIGrabButton(krt.disp,
                      XIAllDevices,
                      3,
                      krt.rootWin,
                      NULL,
                      GrabModeAsync,
                      GrabModeAsync,
                      False,
                      &krt.eventmask,
                      nmods,
                      modifiers);
   if (res < 0)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt][%s] Failed to XIGrabButton(3), res=%d\n",
             __FUNCTION__, res);
     }
#endif /* _F_REMAP_MOUSE_BUTTON_TO_HWKEY_ */

   /* select XI events for a part */
   XISelectEvents(krt.disp,
                  krt.rootWin,
                  &krt.eventmask,
                  1);
}

static int
_e_keyrouter_cb_event_any(void *data, int ev_type, void *event)
{
   XEvent *ev = (XEvent *)event;
   XDeviceKeyEvent *xdevkey = (XDeviceKeyEvent *)ev;
   int type = ev->xcookie.type;

   if (type == krt.DeviceKeyPress)
     {
        ev->type = KeyPress;
        krt.first_press_flag++;
     }
   else if (type == krt.DeviceKeyRelease)
     {
        ev->type = KeyRelease;
        krt.first_press_flag--;
     }
   else
     return 1;

   ev->xany.display = krt.disp;
   ev->xkey.keycode = xdevkey->keycode;
   ev->xkey.time = xdevkey->time;
   ev->xkey.state = 0;
   ev->xkey.root = krt.rootWin;
   ev->xkey.send_event = 1;
   ev->xkey.subwindow = None;

   if (!_e_keyrouter_is_waiting_key_list_empty(ev))
     return 1;

   if (_e_keyrouter_is_key_in_ignored_list(ev))
     return 1;

   _e_keyrouter_hwkey_event_handler(ev);

   return 1;
}

static int
LongPressRecognize(int keycode)
{
   longpress_info *info = NULL;
   Eina_List *l;
   longpress_info *d;
   Ecore_Timer *t;

   EINA_LIST_FOREACH(krt.longpress_list, l, d)
     {
        if (d->keycode != keycode) continue;

        info = malloc(sizeof(longpress_info));
        info->keycode = keycode;
        info->longpress_timeout = d->longpress_timeout;
        info->longpress_window = d->longpress_window;

        t = ecore_timer_add((double)info->longpress_timeout/1000.0,
                            LongPressEventDeliver, info);

        krt.pressinfo->longpress_timeout = d->longpress_timeout;
        krt.timer_flag = 1;
        krt.e_longpress_timer = t;

        break;
     }

   if (!krt.e_longpress_timer)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt][%s] Failed to add Long_Press_Timer \n",
             __FUNCTION__);

        free(info);
     }

   return 0;
}

static Eina_Bool
LongPressEventDeliver(void *data)
{
   longpress_info *lpinfo;
   key_event_info* key_data;
   int menu_keycode, back_keycode;
   int col=0;
   Eina_Bool ret_val = EINA_FALSE;
   KeySym sym_long, sym_menu, sym_back;
   Ecore_X_Atom atom = 0;

   lpinfo = (longpress_info*)data;
   if (!lpinfo)
     return ret_val;

   menu_keycode = ecore_x_keysym_keycode_get(KEY_MENU);
   back_keycode = ecore_x_keysym_keycode_get(KEY_BACK);

   sym_long = XkbKeycodeToKeysym(krt.disp, lpinfo->keycode, col, 0);
   sym_menu = XkbKeycodeToKeysym(krt.disp, menu_keycode, col, 0);
   sym_back = XkbKeycodeToKeysym(krt.disp, back_keycode, col, 0);

   if (sym_long == sym_menu)
     atom = krt.atomMenuLongPress;
   else if (sym_long == sym_back)
     atom = krt.atomBackLongPress;

   if (atom)
     {
        if (ecore_x_client_message32_send(lpinfo->longpress_window,
                                          atom,
                                          ECORE_X_EVENT_MASK_NONE,
                                          lpinfo->longpress_timeout,
                                          0, 0, 0, 0))
          {
             ret_val = EINA_TRUE;
          }
     }

   /* Delete timer & Clear flag */
   krt.short_press_flag = 0;
   krt.timer_flag = 0;
   ecore_timer_del(krt.e_longpress_timer);
   krt.e_longpress_timer = NULL;

   key_data = malloc(sizeof(key_event_info));
   key_data->ev_type = KeyRelease;
   key_data->keycode = lpinfo->keycode;
   krt.ignored_key_list = eina_list_append(krt.ignored_key_list, key_data);
   free(lpinfo);

   return ret_val;
}

static Eina_Bool
ShortPressEventDeliver(longpress_info *kevinfo)
{
   krt.timer_flag = 0;
   krt.short_press_flag = 1;
   ecore_timer_del(krt.e_longpress_timer);
   krt.e_longpress_timer = NULL;
   XEvent xev;

   xev.xkey.state = 0;
   xev.xkey.display = krt.disp;
   xev.xkey.root = krt.rootWin;
   xev.xkey.window= kevinfo->longpress_window;
   xev.xkey.keycode = kevinfo->keycode;
   xev.xkey.time = kevinfo->evtime;
   xev.xkey.type = KeyPress;
   xev.xkey.send_event = 1;
   xev.xkey.subwindow = None;

   free(krt.pressinfo);
   krt.pressinfo = NULL;
   _e_keyrouter_hwkey_event_handler(&xev);

   return EINA_TRUE;
}

static void
_e_keyrouter_cancel_key(XEvent *xev, int keycode)
{
   keylist_node *tmp_ptr;
   keylist_node* ptr;

   SECURE_SLOGD("[krt] Begin of cancel process of a keycode(%d)!\n", keycode);

   for (ptr = krt.HardKeys[keycode].pressed_win_ptr; (NULL != ptr); )
     {
        xev->xkey.window = ptr->wid;

        /* Send Cancel KeyPress */
        xev->xkey.type = KeyPress;
        xev->xkey.keycode = krt.cancel_key.keycode;

        SECURE_SLOGD("[krt] Deliver KeyPress (keycode:%d) to window (0x%x) !\n",
                     xev->xkey.keycode, (int)xev->xkey.window);

        XSendEvent(krt.disp,
                   xev->xkey.window,
                   False,
                   KeyPressMask | KeyReleaseMask,
                   xev);

        /* Send KeyRelease of the keycode */
        xev->xkey.type = KeyRelease;
        xev->xkey.keycode = keycode;

        SECURE_SLOGD("[krt] Deliver KeyRelease (keycode:%d) to window (0x%x) !\n",
                     xev->xkey.keycode, (int)xev->xkey.window);

        XSendEvent(krt.disp,
                   xev->xkey.window,
                   False,
                   KeyPressMask | KeyReleaseMask,
                   xev);

        /* Cancel KeyRelease */
        xev->xkey.type = KeyRelease;
        xev->xkey.keycode = krt.cancel_key.keycode;

        SECURE_SLOGD("[krt] Deliver KeyRelease (keycode:%d) to window (0x%x) !\n",
                     xev->xkey.keycode, (int)xev->xkey.window);

        XSendEvent(krt.disp,
                   xev->xkey.window,
                   False,
                   KeyPressMask | KeyReleaseMask,
                   xev);

        tmp_ptr = ptr;
        ptr = ptr->next ;
        free(tmp_ptr);
     }

   SECURE_SLOGD("[krt] End of cancel process of a keycode(%d)!\n", keycode);

   krt.HardKeys[keycode].pressed_win_ptr = NULL;
}

static void
_e_keyrouter_hwkey_event_handler(XEvent *ev)
{
   int i;
   key_event_info *key_data;
   int result;

   /* KeyRelease handling for key composition */
   if (ev->type == KeyRelease)
     {
        if ((krt.longpress_enabled == 1) &&
            (krt.timer_flag == 1))
          {
             result = ShortPressEventDeliver(krt.pressinfo);
             if (result == False)
               return;
          }
        /* deliver the key */
        DeliverDeviceKeyEvents(ev);

        ResetModKeyInfo();
        return;
     }
   /* KeyPress handling for key composition */
   else if (ev->type == KeyPress)
     {
        SECURE_SLOGD("\n[krt][%s] KeyPress (keycode:%d)\n",
                     __FUNCTION__, ev->xkey.keycode);

        if ((krt.longpress_enabled == 1) &&
            (krt.short_press_flag == 0) &&
            (krt.timer_flag == 0) &&
            (krt.first_press_flag == 1))
          {
             if (krt.HardKeys[ev->xkey.keycode].longpress == True)
               {
                  krt.pressinfo = malloc(sizeof(longpress_info));
                  krt.pressinfo->keycode = ev->xkey.keycode;
                  krt.pressinfo->longpress_window = ev->xkey.window;
                  krt.pressinfo->evtime = ev->xkey.time;
                  LongPressRecognize(ev->xkey.keycode);
                  return;
               }
          }
        if ((krt.longpress_enabled == 1) &&
            (krt.short_press_flag == 0) &&
            (krt.timer_flag == 1))
          {
             ShortPressEventDeliver(krt.pressinfo);
             /* Two key Press... if modkey???? */
          }
        krt.short_press_flag = 0;
        krt.pressinfo = NULL;

        if (!krt.modkey_set)
          {
             for (i = 0; i < NUM_KEY_COMPOSITION_ACTIONS; i++)
               {
                  if (!krt.modkey[i].set)
                    {
                       /* check modifier key */
                       krt.modkey[i].idx_mod = IsModKey(ev->xkey.keycode, i);

                       if (krt.modkey[i].idx_mod)
                         {
                            SECURE_SLOGD("\n[krt][%s][%d] Modifier Key ! (keycode=%d)\n",
                                         __FUNCTION__, i, ev->xkey.keycode);

                            krt.modkey[i].set = 1;
                            krt.modkey_set = 1;
                            krt.modkey[i].time = ev->xkey.time;
                         }
                    }
               }

             /* deliver the key */
             DeliverDeviceKeyEvents(ev);
             return;
          }
        else
          {
             for (i = 0; i < NUM_KEY_COMPOSITION_ACTIONS; i++)
               {
                  krt.modkey[i].composited = IsKeyComposited(ev->xkey.keycode, ev->xkey.time, i);

                  if (krt.modkey[i].composited)
                    {
                       SECURE_SLOGD("\n[krt][%s][%d] Composition Key ! (keycode=%d)\n",
                                    __FUNCTION__, i, ev->xkey.keycode);

                       _e_keyrouter_cancel_key(ev, krt.modkey[i].keys[krt.modkey[i].idx_mod-1].keycode);

                       /* Do Action : ex> send ClientMessage to root window */
                       DoKeyCompositionAction(i, 1);

                       if (krt.modkey[i].press_only)
                         {
                            /* Put Modifier/composited keys' release in ignored_key_list to ignore them */
                            key_data = malloc(sizeof(key_event_info));
                            key_data->ev_type = KeyRelease;
                            key_data->keycode = krt.modkey[i].keys[0].keycode;
                            krt.ignored_key_list = eina_list_append(krt.ignored_key_list, key_data);
                            SECURE_SLOGD("[krt][%s] ignored key added (keycode=%d, type=%d)\n",
                                         __FUNCTION__, key_data->keycode, key_data->ev_type);

                            key_data = malloc(sizeof(key_event_info));
                            key_data->ev_type = KeyRelease;
                            key_data->keycode = krt.modkey[i].keys[1].keycode;
                            krt.ignored_key_list = eina_list_append(krt.ignored_key_list, key_data);
                            SECURE_SLOGD("[krt][%s] ignored key added (keycode=%d, type=%d)\n",
                                         __FUNCTION__, key_data->keycode, key_data->ev_type);
                         }
                       else /* need to be waited for keys */
                         {
                            /* Put Modifier/composited keys' release in waiting_key_list to check them */
                            key_data = malloc(sizeof(key_event_info));
                            key_data->ev_type = KeyRelease;
                            key_data->keycode = krt.modkey[i].keys[0].keycode;
                            key_data->modkey_index = i;
                            krt.waiting_key_list = eina_list_append(krt.waiting_key_list, key_data);
                            SECURE_SLOGD("[krt][%s] waiting key added (keycode=%d, type=%d)\n",
                                         __FUNCTION__, key_data->keycode, key_data->ev_type);

                            key_data = malloc(sizeof(key_event_info));
                            key_data->ev_type = KeyRelease;
                            key_data->keycode = krt.modkey[i].keys[1].keycode;
                            key_data->modkey_index = i;
                            krt.waiting_key_list = eina_list_append(krt.waiting_key_list, key_data);
                            SECURE_SLOGD("[krt][%s] waiting key added (keycode=%d, type=%d)\n",
                                         __FUNCTION__, key_data->keycode, key_data->ev_type);
                         }

                       ResetModKeyInfo();
                       return;
                    }
               }

             /* deliver the key */
             DeliverDeviceKeyEvents(ev);

             ResetModKeyInfo();
             return;
          }
     }
}

static int
_e_keyrouter_cb_event_generic(void *data, int ev_type, void *event)
{
   Ecore_X_Event_Generic *e = (Ecore_X_Event_Generic *)event;
   XIDeviceEvent *evData = (XIDeviceEvent *)(e->data);
   XEvent xev;

#ifdef _F_REMAP_MOUSE_BUTTON_TO_HWKEY_
   int keycode = 0;
   static int home_keycode = 0;
   static int back_keycode = 0;
#endif /* _F_REMAP_MOUSE_BUTTON_TO_HWKEY_ */

   if (e->extension != krt.xi2_opcode)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt][%s] Invalid event !(extension:%d, evtype:%d)\n",
             __FUNCTION__, e->extension, e->evtype);
        return 1;
     }

   if ((!evData) || (evData->send_event))
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt][%s] Generic event data is not available or the event "
             "was sent via XSendEvent (and will be ignored) !\n",
             __FUNCTION__);
        return 1;
     }

   switch (e->evtype)
     {
      case XI_HierarchyChanged:
         _e_keyrouter_xi2_device_hierarchy_handler((XIHierarchyEvent *)evData);
         break;

#ifdef _F_REMAP_MOUSE_BUTTON_TO_HWKEY_
      case XI_ButtonPress:
      case XI_ButtonRelease:
         if (evData->detail == 2) /* Home key */
           {
              if (!home_keycode)
                home_keycode = ecore_x_keysym_keycode_get(KEY_HOME);

              keycode = home_keycode;
           }
         else if (evData->detail == 3) /* Back key */
           {
              if (!back_keycode)
                back_keycode = ecore_x_keysym_keycode_get(KEY_BACK);

              keycode = back_keycode;
           }

         if (!keycode)
           break;

         xev.xkey.display = krt.disp;
         xev.xkey.root = krt.rootWin;
         xev.xkey.keycode = keycode;
         xev.xkey.time = evData->time;
         xev.xkey.type = (XI_ButtonPress==e->evtype) ? KeyPress : KeyRelease;
         xev.xkey.state = 0;
         xev.xkey.send_event = 1;
         xev.xkey.subwindow = None;

         if (xev.xkey.type == KeyPress)
           krt.first_press_flag++;
         else if (xev.xkey.type == KeyRelease)
           krt.first_press_flag--;

         if (!_e_keyrouter_is_waiting_key_list_empty(&xev))
           return 1;

         if (_e_keyrouter_is_key_in_ignored_list(&xev))
           return 1;

         _e_keyrouter_hwkey_event_handler(&xev);
         break;
#endif /* _F_REMAP_MOUSE_BUTTON_TO_HWKEY_ */

      case XI_KeyPress:
      case XI_KeyRelease:
         xev.type = e->evtype;
         xev.xkey.keycode = evData->detail;
         xev.xany.display = krt.disp;
         xev.xkey.time = evData->time;
         xev.xkey.state = 0;
         xev.xkey.send_event = 1;
         xev.xkey.subwindow = None;
         xev.xkey.root = krt.rootWin;

         if (xev.type == XI_KeyPress) krt.first_press_flag++;
         else if (xev.type == XI_KeyRelease) krt.first_press_flag--;

         if (!_e_keyrouter_is_waiting_key_list_empty(&xev))
           return 1;

         if (_e_keyrouter_is_key_in_ignored_list(&xev))
           return 1;

         _e_keyrouter_hwkey_event_handler(&xev);
         break;
     }

   return 1;
}

static int
_e_keyrouter_cb_window_create(void *data, int ev_type, void *ev)
{
   Ecore_X_Window_Attributes att;
   Ecore_X_Event_Window_Create *e = ev;
   int ret = 0, count = 0;
   int i, keycode, grab_mode;
   unsigned int *prop_data = NULL;

   /* Check if current window is TOP-level window or not */
   if ((!e) || (krt.rootWin != e->parent) || (!e->win))
     return 1;

   /* Setting PropertyChangeMask and SubstructureNotifyMask
    * for TOP-level window (e->win)
    */
   ecore_x_window_attributes_get(e->win, &att);
   XSelectInput(krt.disp, e->win,
                att.event_mask.mine | PropertyChangeMask | StructureNotifyMask);
   ecore_x_sync();

   /* Get the window property using the atom */
   ret = ecore_x_window_prop_property_get(e->win,
                                          krt.atomGrabKey,
                                          ECORE_X_ATOM_CARDINAL,
                                          32,
                                          (unsigned char **)&prop_data,
                                          &count);
   if (!ret || !prop_data)
     goto out;

   /* add to list to watch property */
   for (i = 0; i < count; i++)
     {
        grab_mode = prop_data[i] & GRAB_MODE_MASK;
        keycode = prop_data[i] & (~GRAB_MODE_MASK);

        _e_keyrouter_update_key_delivery_list(e->win,
                                              keycode,
                                              grab_mode,
                                              1);
     }

out:
   if (prop_data) free(prop_data);
   return 1;
}

static int
_e_keyrouter_cb_window_property(void *data, int ev_type, void *ev)
{
   Ecore_X_Event_Window_Property *e = ev;
   int ret = 0, count = 0, res = -1;
   unsigned int *prop_data = NULL;
   unsigned int ret_val = 0;
   unsigned int long_enable_data = 0;
   int i, keycode, grab_mode;

   if ((e->atom == krt.atomDeviceStatus) &&
       (e->win == krt.rootWin))
     {
        res = ecore_x_window_prop_card32_get(e->win,
                                             krt.atomDeviceStatus,
                                             &ret_val,
                                             1);
        if (res == 1)
          Device_Status(ret_val);
        goto out;
     }

   if ((e->atom == krt.atomGrabStatus) &&
       (e->win == krt.rootWin))
     {
        res = ecore_x_window_prop_card32_get(e->win,
                                             krt.atomGrabStatus,
                                             &ret_val,
                                             1);
        if (res == 1)
          Keygrab_Status(ret_val);
        goto out;
     }

   if ((e->atom == krt.atomLongPressEnable) &&
       (e->win == krt.rootWin))
     {
        res = ecore_x_window_prop_card32_get(e->win,
                                             krt.atomLongPressEnable,
                                             &long_enable_data,
                                             1);
        if (res == 1)
          krt.longpress_enabled = long_enable_data;

        goto out;
     }

   /* See the client window has interesting atom (_atomGrabKey) */
   if (e->atom != krt.atomGrabKey)
     goto out;

   /* Get the window property using the atom */
   ret = ecore_x_window_prop_property_get(e->win,
                                          e->atom,
                                          ECORE_X_ATOM_CARDINAL,
                                          32,
                                          (unsigned char **)&prop_data,
                                          &count);
   if ((!ret) || (!prop_data))
     {
        RemoveWindowDeliveryList(e->win, 0, 0);
        goto out;
     }

   RemoveWindowDeliveryList(e->win, 0, 0);

   for (i = 0; i < count; i++)
     {
        grab_mode = prop_data[i] & GRAB_MODE_MASK;
        keycode = prop_data[i] & (~GRAB_MODE_MASK);

        _e_keyrouter_update_key_delivery_list(e->win,
                                              keycode,
                                              grab_mode,
                                              1);
     }

out:
   if (prop_data) free(prop_data);
   return 1;
}

static int
_e_keyrouter_cb_e_client_stack(void *data, int ev_type, void *ev)
{
   E_Event_Client *e = ev;

   if (!e->ec)
     return 1;

   krt.isWindowStackChanged = 1;
   AdjustTopPositionDeliveryList(e_client_util_win_get(e->ec), 1);

   return 1;
}

static int
_e_keyrouter_cb_e_client_remove(void *data, int ev_type, void *ev)
{
   E_Event_Client *e = ev;

   if (!e->ec)
     return 1;

   krt.isWindowStackChanged = 1;
   RemoveWindowDeliveryList(e_client_util_win_get(e->ec), 0, 1);

   return 1;
}

static int
_e_keyrouter_cb_window_destroy(void *data, int ev_type, void *ev)
{
   E_Client *ec;
   Ecore_X_Event_Window_Destroy *e = ev;

   /* Skip for client windows which have border as their parents */
   ec = e_pixmap_find_client(E_PIXMAP_TYPE_X, e->win);
   if (ec)
     return 1;

   krt.isWindowStackChanged = 1;
   RemoveWindowDeliveryList(e->win, 0, 1);

   return 1;
}

static int
_e_keyrouter_cb_window_configure(void *data, int ev_type, void *ev)
{
   E_Client *ec;
   Ecore_X_Event_Window_Configure *e = ev;

   /* Skip for client windows which have border as their parents */
   ec = e_pixmap_find_client(E_PIXMAP_TYPE_X, e->win);
   if (ec)
     return 1;

   if (krt.rootWin != e->event_win)
     return 1;

   krt.isWindowStackChanged = 1;
   AdjustTopPositionDeliveryList(e->win, !!e->abovewin);

   return 1;
}

static int
_e_keyrouter_cb_client_message(void* data, int type, void* event)
{
   int event_type = 0;
   int keycode;
   int cancel = 0;
   Ecore_X_Event_Client_Message* ev;
   Ecore_X_Window longpress_win;;
   int longpress_timeout;
   longpress_info *linfo;
   int i, col=0;
   KeySym msgsym;
   longpress_info *info;
   Eina_List *l;
   unsigned int enable = 1;

   ev = event;
   if (ev->message_type != krt.atomHWKeyEmulation) return 1;
   if (ev->format == 8)
     {
        if (ev->data.b[0] == 'P')
          event_type = KeyPress;
        else if (ev->data.b[0] == 'R')
          event_type = KeyRelease;

        if (ev->data.b[1] == 'C')
          cancel = 1;

        switch (event_type)
          {
           case KeyPress:
           case KeyRelease:
              keycode = ecore_x_keysym_keycode_get(&ev->data.b[2]);

              _e_keyrouter_do_hardkey_emulation(NULL,
                                                event_type,
                                                0,
                                                keycode,
                                                cancel);
              break;

           default:
              SLOG(LOG_DEBUG, "krt",
                   "[krt][cb_client_message] Unknown event type ! (type=%d)\n",
                   event_type);
              break;
          }
     }
   else if (ev->format == 32)
     {
        longpress_win = None;
        keycode = 0;
        longpress_timeout = 0;

        longpress_win = (Ecore_X_Window)ev->data.l[0];
        keycode = (int)ev->data.l[1];
        longpress_timeout= (int)ev->data.l[2];

        if ((longpress_win) &&
            (keycode) &&
            (longpress_timeout))
          {
             if (krt.HardKeys[keycode].longpress == False)
               {
                  msgsym = XkbKeycodeToKeysym(krt.disp,
                                              (KeyCode)keycode,
                                              col,
                                              0);

                  for (i = MIN_KEYCODE; i < MAX_HARDKEYS; i++)
                    {
                       if (XkbKeycodeToKeysym(krt.disp,
                                              i,
                                              col,
                                              0) == msgsym)
                         {
                            krt.HardKeys[i].longpress = True;

                            linfo = malloc(sizeof(longpress_info));
                            linfo->longpress_window = longpress_win;
                            linfo->keycode = i;
                            linfo->longpress_timeout = longpress_timeout;

                            krt.longpress_list = eina_list_append(krt.longpress_list, linfo);
                         }
                    }
               }
             else
               {
                  msgsym = XkbKeycodeToKeysym(krt.disp,
                                              (KeyCode)keycode,
                                              col,
                                              0);
                  for (i = MIN_KEYCODE; i < MAX_HARDKEYS; i++)
                    {
                       if (XkbKeycodeToKeysym(krt.disp,
                                              i,
                                              col,
                                              0) == msgsym)
                         {
                            EINA_LIST_FOREACH(krt.longpress_list, l, info)
                              {
                                 if (info->keycode == i)
                                   {
                                      info->longpress_window = longpress_win;
                                      info->longpress_timeout = longpress_timeout;
                                      break;
                                   }
                              }
                         }
                    }
               }

             krt.longpress_enabled = enable;
             ecore_x_window_prop_card32_set(krt.rootWin,
                                            krt.atomLongPressEnable,
                                            &enable,
                                            1);
          }
     }

   return 1;
}

static int
_e_keyrouter_cb_window_stack(void *data, int ev_type, void *ev)
{
   E_Client *ec;
   Ecore_X_Event_Window_Stack *e = ev;

   /* Skip for client windows which have border as their parents */
   ec = e_pixmap_find_client(E_PIXMAP_TYPE_X, e->win);
   if (ec)
     return 1;

   if (krt.rootWin != e->event_win)
     return 1;

   krt.isWindowStackChanged = 1;
   AdjustTopPositionDeliveryList(e->win, !e->detail);

   return 1;
}

/* e bindings functions and action callbacks */
static int
_e_keyrouter_modifiers(E_Binding_Modifier modifiers)
{
   int mod = 0;

   if (modifiers & E_BINDING_MODIFIER_SHIFT) mod |= ECORE_EVENT_MODIFIER_SHIFT;
   if (modifiers & E_BINDING_MODIFIER_CTRL ) mod |= ECORE_EVENT_MODIFIER_CTRL;
   if (modifiers & E_BINDING_MODIFIER_ALT  ) mod |= ECORE_EVENT_MODIFIER_ALT;
   if (modifiers & E_BINDING_MODIFIER_WIN  ) mod |= ECORE_EVENT_MODIFIER_WIN;

   return mod;
}

static void
_e_keyrouter_do_bound_key_action(XEvent *xev)
{
   Ecore_Event_Key *ev;
   int keycode = xev->xkey.keycode;
   size_t len;

   if (!krt.HardKeys[keycode].bind)
     {
        SECURE_SLOGD("[krt][do_bound_key_action] bind info of key(%d) "
                     "is NULL !\n", keycode);
        return;
     }

   ev = malloc(sizeof(Ecore_Event_Key));
   if (!ev)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt][do_bound_key_action] Failed to allocate memory "
             "for Ecore_Event_Key !\n");
        return;
     }

   ev->keyname = (char *)malloc(strlen(krt.HardKeys[keycode].bind->key)+1);
   ev->key = (char *)malloc(strlen(krt.HardKeys[keycode].bind->key)+1);

   if ((!ev->keyname) || (!ev->key))
     {
        free(ev);
        SLOG(LOG_DEBUG, "krt",
             "[krt][do_bound_key_action] Failed to allocate memory "
             "for key name !\n");
        return;
     }

   len = strlen(krt.HardKeys[keycode].bind->key) + 1;
   strncpy((char *)ev->keyname, krt.HardKeys[keycode].bind->key, len);

   len = strlen(krt.HardKeys[keycode].bind->key) + 1;
   strncpy((char *)ev->key, krt.HardKeys[keycode].bind->key, len);

   ev->compose = NULL;
   ev->string = NULL;
   ev->modifiers = 0;
   ev->timestamp = xev->xkey.time;
   ev->event_window = xev->xkey.window;
   ev->root_window = xev->xkey.root;
   ev->same_screen = xev->xkey.same_screen;
   ev->window = xev->xkey.subwindow ? xev->xkey.subwindow : xev->xkey.window;

   if (xev->type == KeyPress)
     e_bindings_key_down_event_handle(krt.HardKeys[keycode].bind->ctxt,
                                      NULL,
                                      ev);
   else if (xev->type == KeyRelease)
     e_bindings_key_up_event_handle(krt.HardKeys[keycode].bind->ctxt,
                                    NULL,
                                    ev);

   free(ev);
}

static void
_e_keyrouter_xi2_device_hierarchy_handler(XIHierarchyEvent *event)
{
   int i;

   if ((event->flags & XIDeviceEnabled) ||
       (event->flags & XIDeviceDisabled))
     {
        for (i = 0 ; i < event->num_info ; i++)
          {
             if (event->info[i].flags & XIDeviceEnabled)
               {
                  _e_keyrouter_device_add(event->info[i].deviceid,
                                          event->info[i].use);
               }
             else if (event->info[i].flags & XIDeviceDisabled)
               {
                  _e_keyrouter_device_remove(event->info[i].deviceid,
                                             event->info[i].use);
               }
          }
     }
}

static Eina_Bool
_e_keyrouter_is_waiting_key_list_empty(XEvent *ev)
{
   int modkey_index;
   Eina_List *l;
   key_event_info *data, *key_data;

   if (!krt.waiting_key_list)
     return EINA_TRUE;

   SECURE_SLOGD("[krt][%s] waiting_key_list is NOT empty !\n", __FUNCTION__);
   SECURE_SLOGD("[krt][%s] type=%s, keycode=%d", __FUNCTION__,
                (ev->xkey.type==KeyPress) ? "KeyPress" : "KeyRelease",
                ev->xkey.keycode);

   EINA_LIST_FOREACH(krt.waiting_key_list, l, data)
     {
        if ((data) &&
            (ev->type == data->ev_type) &&
            (ev->xkey.keycode == data->keycode))
          {
             /* found !!! */
             SECURE_SLOGD("[krt][%s] found !!! (keycode:%d, type=%d)\n",
                          __FUNCTION__, data->keycode, data->ev_type);
             goto found;
          }
     }

   SECURE_SLOGD("[krt][%s] not found !!! (keycode:%d, type=%d)\n",
                __FUNCTION__, ev->xkey.keycode, ev->type);

   /* If a key press is coming before the waiting_key_list is cleared,
    * put its release into ignored_list.
    */
   if (ev->type == KeyPress)
     {
        key_data = malloc(sizeof(key_event_info));
        key_data->ev_type = KeyRelease;
        key_data->keycode = ev->xkey.keycode;

        krt.ignored_key_list = eina_list_append(krt.ignored_key_list, key_data);

        SECURE_SLOGD("[krt][%s] ignored key added (keycode=%d, type=%d)\n",
                     __FUNCTION__, key_data->keycode, key_data->ev_type);
     }
   else if (ev->type == KeyRelease)
     {
        /* If a key release which is contained in ignored_key_list is coming,
         * remove it from ignored_key_list.
         */
        SECURE_SLOGD("[krt][%s] check a key is exiting in ignored key list "
                     "(keycode=%d, type=%d)\n", __FUNCTION__,
                     ev->xkey.keycode, ev->type);

        _e_keyrouter_is_key_in_ignored_list(ev);
     }

   return EINA_FALSE;

found:
   modkey_index = data->modkey_index;

   krt.waiting_key_list = eina_list_remove(krt.waiting_key_list, data);

   SECURE_SLOGD("[krt][%s][%d] key was remove from waiting_key_list !"
                "(keycode:%d, type:%d)\n", __FUNCTION__,
                modkey_index, ev->xkey.keycode, ev->type);

   if (!krt.waiting_key_list)
     {
        SECURE_SLOGD("[krt][%s] Waiting conditions are satified !\n",
                     __FUNCTION__);

        /* Do Action : ex> send ClientMessage to root window */
        DoKeyCompositionAction(modkey_index, 0);
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_keyrouter_is_key_in_ignored_list(XEvent *ev)
{
   Eina_List *l;
   key_event_info *data;

   if (!krt.ignored_key_list)
     {
        return EINA_FALSE;
     }

   SECURE_SLOGD("[krt][%s] ignored_key_list is NOT empty !\n",
                __FUNCTION__);

   EINA_LIST_FOREACH(krt.ignored_key_list, l, data)
     {
        if ((data) &&
            (ev->type == data->ev_type) &&
            (ev->xkey.keycode == data->keycode))
          {
             /* found !!! */
             SECURE_SLOGD("[krt][%s] found !!! (keycode:%d, type=%d)\n",
                          __FUNCTION__, data->keycode, data->ev_type);
             goto found;
          }
     }

   SECURE_SLOGD("[krt][%s] not found !!! (keycode:%d, type=%d)\n",
                __FUNCTION__, ev->xkey.keycode, ev->type);

   return EINA_FALSE;

found:
   krt.ignored_key_list = eina_list_remove(krt.ignored_key_list, data);

   SECURE_SLOGD("[krt][%s] key was remove from ignored_list !"
                "(keycode:%d, type:%d)\n", __FUNCTION__,
                ev->xkey.keycode, ev->type);

   return EINA_TRUE;
}

static void
_e_keyrouter_device_add(int id, int type)
{
   int ndevices;
   Eina_List *l;
   XIDeviceInfo *info = NULL;
   KeyrouterDeviceType kdtype = E_KEYROUTER_NONE;
   E_Keyrouter_Device_Info *kdi = NULL;

   EINA_LIST_FOREACH(krt.device_list, l, kdi)
     {
        if ((kdi) &&
            (kdi->type==E_KEYROUTER_HWKEY) &&
            (kdi->id == id))
          {
             SLOG(LOG_DEBUG, "krt",
                  "[krt] Slave key device (id=%d, name=%s) was added !\n",
                  id, kdi->name);

             detachSlave(id);
             return;
          }
     }

   switch (type)
     {
      case XISlavePointer:
         info = XIQueryDevice(krt.disp, id, &ndevices);
         if ((!info) || (ndevices <= 0))
           {
              SLOG(LOG_DEBUG, "krt",
                   "[krt] There is no queried XI device. "
                   "(device id=%d, type=%d)\n",
                   id, type);
              goto out;
           }

         if (strcasestr(info->name, "XTEST") ||
             strcasestr(info->name, "Touch") ||
             strcasestr(info->name, "mouse"))
           goto out;

         SLOG(LOG_DEBUG, "krt",
              "[krt] XISlavePointer but has key events... "
              "(device id=%d, name=%s)\n",
              id, info->name);
         kdtype = E_KEYROUTER_KEYBOARD;
         goto keyboard_added;

         break;

      case XISlaveKeyboard:
         info = XIQueryDevice(krt.disp, id, &ndevices);
         if ((!info) || (ndevices <= 0))
           {
              SLOG(LOG_DEBUG, "krt",
                   "[krt][%s] There is no queried XI device. "
                   "(device id=%d, type=%d)\n",
                   __FUNCTION__, id, type);
              goto out;
           }
         if (strcasestr(info->name, "XTEST"))
           goto out;

keyboard_added:
         if ((kdtype == E_KEYROUTER_NONE) &&
             (strcasestr(info->name, "keyboard")))
           kdtype = E_KEYROUTER_KEYBOARD;
         else
           kdtype = E_KEYROUTER_HOTPLUGGED;

         E_Keyrouter_Device_Info *data = malloc(sizeof(E_Keyrouter_Device_Info));
         if (!data)
           {
              SLOG(LOG_DEBUG, "krt",
                   "[krt][%s] Failed to allocate memory for device info !\n",
                   __FUNCTION__);
              goto out;
           }

         data->type = kdtype;
         data->id = id;
         data->name = eina_stringshare_add(info->name);

         krt.device_list = eina_list_append(krt.device_list, data);

         _e_keyrouter_grab_hwkeys(id);
     }

out:
   if (info) XIFreeDeviceInfo(info);
}

static void
_e_keyrouter_device_remove(int id, int type)
{
   Eina_List *l;
   E_Keyrouter_Device_Info *data;

   if (!krt.device_list)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt][%s] device list is empty ! something's wrong ! "
             "(id=%d, type=%d)\n",
             __FUNCTION__, id, type);
        goto out;
     }

   EINA_LIST_FOREACH(krt.device_list, l, data)
     {
        if ((data) && (data->id == id))
          {
             switch (data->type)
               {
                case E_KEYROUTER_HWKEY:
                   break;

                case E_KEYROUTER_HOTPLUGGED:
                case E_KEYROUTER_KEYBOARD:
                   SLOG(LOG_DEBUG, "krt",
                        "[krt][%s] Slave hotplugged key|keyboard device "
                        "(id=%d, name=%s, type=%d) was removed/disabled !\n",
                        __FUNCTION__, id, data->name, type);
                   krt.device_list = eina_list_remove(krt.device_list, data);
                   free(data);
                   goto out;

                default:
                   SLOG(LOG_DEBUG, "krt",
                        "[krt][%s] Unknown type of device ! "
                        "(id=%d, type=%d, name=%s, device type=%d)\n",
                        __FUNCTION__, data->id, type, data->name, data->type);
                   krt.device_list = eina_list_remove(krt.device_list, data);
                   free(data);
                   goto out;
               }
          }
     }

out:
   return;
}

static E_Zone *
_e_keyrouter_get_zone(void)
{
   E_Zone *zone = NULL;
   Eina_List *zl;
   E_Zone *z;

   if (krt.zone)
     return krt.zone;

   EINA_LIST_FOREACH(e_comp->zones, zl, z)
     {
        if (z)
          zone = z;
     }

   return zone;
}

static int
_e_keyrouter_init(void)
{
   int ret = 1;
   int grab_result;
   unsigned int enable = 0;

   _e_keyrouter_structure_init();

   krt.disp = ecore_x_display_get();
   if (!krt.disp)
     {
        SLOG(LOG_DEBUG, "krt", "[krt] Failed to open display..!\n");
        ret = 0;
        goto out;
     }

   krt.rootWin = DefaultRootWindow(krt.disp);

   _e_keyrouter_x_input_init();
   InitGrabKeyDevices();
   _e_keyrouter_bindings_init();

   krt.atomDeviceStatus = ecore_x_atom_get(STR_ATOM_DEVICE_STATUS);
   krt.atomGrabStatus = ecore_x_atom_get(STR_ATOM_GRAB_STATUS);
   krt.atomGrabKey = ecore_x_atom_get(STR_ATOM_GRAB_KEY);
   krt.atomGrabExclWin = ecore_x_atom_get(STR_ATOM_GRAB_EXCL_WIN);
   krt.atomGrabORExclWin = ecore_x_atom_get(STR_ATOM_GRAB_OR_EXCL_WIN);
   krt.atomHWKeyEmulation = ecore_x_atom_get(PROP_HWKEY_EMULATION);
   krt.atomMenuLongPress = ecore_x_atom_get(STR_ATOM_KEY_MENU_LONGPRESS);
   krt.atomBackLongPress = ecore_x_atom_get(STR_ATOM_KEY_BACK_LONGPRESS);
   krt.atomLongPressEnable = ecore_x_atom_get(STR_ATOM_LONG_PRESS_ENABLE);
   krt.atomNotiWindow = ecore_x_atom_get(STR_ATOM_KEYROUTER_NOTIWINDOW);

   krt.input_window = ecore_x_window_input_new(krt.rootWin, -1, -1, 1, 1);
   krt.noti_window = ecore_x_window_input_new(krt.rootWin, -1, -1, 1, 1);
   krt.longpress_enabled = enable;

   ecore_x_window_prop_card32_set(krt.rootWin,
                                  krt.atomLongPressEnable,
                                  &enable,
                                  1);

   if (!krt.input_window)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt] Failed to create input_window !\n");
     }
   else
     {
        ecore_x_window_prop_property_set(krt.rootWin,
                                         krt.atomHWKeyEmulation,
                                         ECORE_X_ATOM_WINDOW,
                                         32,
                                         &krt.input_window,
                                         1);
     }

   if (!krt.noti_window)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt] Failed to create noti_window !\n");
     }
   else
     {
        ecore_x_window_prop_property_set(krt.rootWin,
                                         krt.atomNotiWindow,
                                         ECORE_X_ATOM_WINDOW,
                                         32,
                                         &krt.noti_window,
                                         1);
     }

   krt.zone = _e_keyrouter_get_zone();
   if (!krt.zone)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt] Failed to get zone !\n");
        ret = 0;
        goto out;
     }

   grab_result = GrabKeyDevices(krt.rootWin);

   if (!grab_result)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt] Failed to GrabDevices() !\n");
        ret = 0;
        goto out;
     }

   krt.DeviceKeyPress = krt.nInputEvent[INPUTEVENT_KEY_PRESS];
   krt.DeviceKeyRelease = krt.nInputEvent[INPUTEVENT_KEY_RELEASE];

   krt.modkey = calloc(NUM_KEY_COMPOSITION_ACTIONS, sizeof(ModifierKey));

   if (!krt.modkey)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt] Failed to allocate memory for key composition !\n");
        ret = 0;
        goto out;
     }

   InitModKeys();
   BuildKeyGrabList(krt.rootWin);

out:
   return ret;
}

static void
_e_keyrouter_fini(void)
{
   UngrabKeyDevices();
}

static void
_e_keyrouter_structure_init(void)
{
   memset(&krt, 0L, sizeof(krt));

   krt.DeviceKeyPress = -1;
   krt.DeviceKeyRelease = -1;
   krt.xi2_opcode = -1;
   krt.isWindowStackChanged = 1;
   krt.prev_sent_keycode = 0;
   krt.resTopVisibleCheck = 0;
   krt.device_list = NULL;
   krt.ignored_key_list = NULL;
   krt.waiting_key_list = NULL;
   krt.hwkeymap_info_list = NULL;
   krt.longpress_list = NULL;
   krt.pressinfo = NULL;
   krt.num_hwkey_devices = 0;
   krt.timer_flag=0;
   krt.short_press_flag = 0;
   krt.first_press_flag = 0;
   krt.longpress_enabled = 0;

   krt.atomGrabKey = None;
   krt.atomDeviceStatus = None;
   krt.atomGrabStatus = None;
   krt.atomGrabExclWin = None;
   krt.atomGrabORExclWin = None;
   krt.atomMenuLongPress = None;
   krt.atomBackLongPress = None;
   krt.atomLongPressEnable = None;
}

static void
_e_keyrouter_grab_hwkeys(int devid)
{
   int i, j, k;
   int result;
   KeySym ksym;

   int min_keycode, max_keycode, keysyms_per_keycode;
   KeySym *keymap = NULL, *origkeymap = NULL;

   XIGrabModifiers modifiers[] = {{XIAnyModifier, 0}};
   int nmods = sizeof(modifiers)/sizeof(modifiers[0]);
   XIEventMask mask;

   if (!krt.hwkeymap_info_list)
     {
        XDisplayKeycodes(krt.disp, &min_keycode, &max_keycode);
        origkeymap = XGetKeyboardMapping(krt.disp, min_keycode,
                                         (max_keycode - min_keycode + 1),
                                         &keysyms_per_keycode);

        if (!origkeymap)
          {
             SLOG(LOG_DEBUG, "krt",
                  "[krt] Failed to get keyboard mapping from X\n");
             return;
          }

        for (i = 0; i < NUM_HWKEYS; i++)
          {
             if (!HWKeys[i]) break;

             Eina_Inlist *l = krt.hwkeymap_info_list;

             ksym = XStringToKeysym(HWKeys[i]);

             if ((ksym == NoSymbol) || (ksym == krt.cancel_key.keysym))
               continue;

             hwkeymap_info *hki = NULL;

             hki = calloc(1, sizeof(hwkeymap_info));

             if (!hki)
               {
                  SLOG(LOG_ERROR, "krt",
                       "[krt] Failed to allocate memory for HW keymap "
                       "information !\n");
                  XFree(origkeymap);
                  return;
               }

             hki->key_name = HWKeys[i];
             hki->key_sym = ksym;
             hki->keycodes = NULL;

             keymap = origkeymap;
             k = 0;
             for (j = min_keycode; j <= max_keycode; j++)
               {
                  if (ksym == keymap[0])
                    k++;
                  keymap += keysyms_per_keycode;
               }

             hki->num_keycodes = k;

             int *keycodes = calloc(1, sizeof(int)*(hki->num_keycodes));

             if (!keycodes)
               {
                  SLOG(LOG_ERROR, "krt",
                       "[krt] Failed to allocate memory for keycode "
                       "array !\n");
                  XFree(origkeymap);
                  free(hki);
                  return;
               }

             keymap = origkeymap;
             k = 0;
             for (j = min_keycode; j <= max_keycode; j++)
               {
                  if (ksym == keymap[0])
                    {
                       keycodes[k] = j;
                       k++;
                    }
                  keymap += keysyms_per_keycode;
               }

             hki->keycodes = keycodes;
             l = eina_inlist_append(l, (Eina_Inlist *)hki);
             krt.hwkeymap_info_list = l;
          }

        if (origkeymap)
          XFree(origkeymap);
     }

   hwkeymap_info *hkinfo = NULL;
   Eina_Inlist *lst = krt.hwkeymap_info_list;

   if (devid)
     {
        mask.deviceid = devid;
        mask.mask_len = XIMaskLen(XI_LASTEVENT);
        mask.mask = calloc(mask.mask_len, sizeof(char));
        XISetMask(mask.mask, XI_KeyPress);
        XISetMask(mask.mask, XI_KeyRelease);
     }

   EINA_INLIST_FOREACH(lst, hkinfo)
     {
        if (!hkinfo->keycodes)
          continue;

        for (k = 0; k < hkinfo->num_keycodes; k++)
          {
             if (devid)
               {
                  result = XIGrabKeycode(krt.disp,
                                         devid,
                                         hkinfo->keycodes[k],
                                         krt.rootWin,
                                         GrabModeAsync,
                                         GrabModeAsync,
                                         False,
                                         &mask,
                                         nmods,
                                         modifiers);
                  if (result < 0)
                    {
                       SECURE_SLOGD("[krt][grab_hwkeys] Failed to grab "
                                    "keycode (=%d) !\n", hkinfo->keycodes[k]);
                       continue;
                    }
               }

             /* disable repeat for a key */
             _e_keyrouter_set_key_repeat(hkinfo->keycodes[k], 0); /* OFF */
          }
     }

   if (devid) free(mask.mask);
}

static void
_e_keyrouter_set_key_repeat(int key, int auto_repeat_mode)
{
   unsigned long mask;
   XKeyboardControl values;

   mask = KBAutoRepeatMode;
   values.auto_repeat_mode = auto_repeat_mode;

   if (key != -1)
     {
        mask = KBKey | KBAutoRepeatMode;
        values.key = key;
     }

   XChangeKeyboardControl(krt.disp, mask, &values);
}

static void
_e_keyrouter_bindings_init(void)
{
   int i, keycode;
   KeySym ksym;

   for (i = 0; i < NUM_HWKEYS; i++)
     {
        if (HWKeys[i])
          {
             ksym = XStringToKeysym(HWKeys[i]);

             if (ksym)
               keycode = XKeysymToKeycode(krt.disp, ksym);
             else
               keycode = 0;

             if ((!keycode) || (keycode >= 255))
               {
                  continue;
               }

             /* get bound key information */
             krt.HardKeys[keycode].bind = e_bindings_key_find(HWKeys[i],
                                                                    E_BINDING_MODIFIER_NONE,
                                                                    1);

             if ((!krt.HardKeys[keycode].bind) ||
                 (strcmp(krt.HardKeys[keycode].bind->key, HWKeys[i])))
               {
                  continue;
               }

             /* ungrab bound key(s) */
             ecore_x_window_key_ungrab(krt.rootWin,
                                       krt.HardKeys[keycode].bind->key,
                                       _e_keyrouter_modifiers(krt.HardKeys[keycode].bind->mod),
                                       krt.HardKeys[keycode].bind->any_mod);

             SECURE_SLOGD("[krt][bindings_init] %s (keycode:%d) was bound !!\n",
                          krt.HardKeys[keycode].bind->key, keycode);
          }
        else
          break;
     }
}

static void
_e_keyrouter_update_key_delivery_list(Ecore_X_Window win, int keycode, const int grab_mode, const int IsOnTop)
{
   int c = 0, res = 0;
   Eina_Bool found = EINA_FALSE;
   hwkeymap_info *hkinfo = NULL;
   Eina_Inlist *l;

   if (!krt.hwkeymap_info_list)
     goto grab_a_keycode_only;

   l = krt.hwkeymap_info_list;
   EINA_INLIST_FOREACH(l, hkinfo)
     {
        if (!hkinfo) continue;
        if (!hkinfo->keycodes) continue;

        for (c = 0; c < hkinfo->num_keycodes; c++)
          {
             if (hkinfo->keycodes[c] == keycode)
               {
                  found = EINA_TRUE;
                  break;
               }
          }

        if (!found) continue;

        for (c = 0; c < hkinfo->num_keycodes; c++)
          {
             res = AddWindowToDeliveryList(win,
                                           hkinfo->keycodes[c],
                                           grab_mode,
                                           1);
             if (res)
               {
                  SECURE_SLOGD("[krt][%s] Failed to add window (0x%x) "
                               "to delivery list ! keycode=%x, grab_mode=0x%X\n",
                               __FUNCTION__, win, hkinfo->keycodes[c], grab_mode);
               }
          }

        return;
     }

   return;

grab_a_keycode_only:
   res = AddWindowToDeliveryList(win,  keycode, grab_mode, 1);
   if (res)
     {
        SECURE_SLOGD("[krt][%s] Failed to add window (0x%x) to delivery list !"
                     "keycode=%x, grab_mode=0x%X\n", __FUNCTION__, win, keycode,
                     grab_mode);
     }
}

static int
GetItemFromWindow(Ecore_X_Window win, const char* atom_name, unsigned int **key_list)
{
   int ret = 0;
   int nr_item = 0;
   int sz_remains_data = 0;
   Atom grabKey;

   grabKey = ecore_x_atom_get(atom_name);

   ret = ecore_x_window_prop_property_get(win,
                                          grabKey,
                                          XA_CARDINAL,
                                          sz_remains_data,
                                          (unsigned char **)key_list,
                                          &nr_item);
   if (!ret || !key_list)
     {
        nr_item = 0;
     }

   return nr_item;
}

static void
BuildKeyGrabList(Ecore_X_Window root)
{
   Ecore_X_Window *childwins = NULL;
   int num_children;
   register unsigned int i, j;
   int grab_mode, keycode;
   unsigned int *key_list = NULL;
   int n_items = 0;

   ecore_x_grab();
   childwins = ecore_x_window_children_get(root, &num_children);
   ecore_x_ungrab();
   if (!childwins) return;

   for (i = 0; i < num_children; i++)
     {
        BuildKeyGrabList(childwins[i]);

        n_items = GetItemFromWindow(childwins[i],
                                    STR_ATOM_GRAB_KEY,
                                    &key_list);
        if (n_items)
          {
             for (j = 0; j < n_items; j++)
               {
                  grab_mode = key_list[j] & GRAB_MODE_MASK;
                  keycode = key_list[j] & (~GRAB_MODE_MASK);

                  _e_keyrouter_update_key_delivery_list(childwins[i],
                                                        keycode,
                                                        grab_mode,
                                                        1);
               }

             if (key_list)
               {
                  free(key_list);
                  key_list = NULL;
               }
          }
     }

   if (key_list) free(key_list);
   if (childwins) free(childwins);
}

static void
InitGrabKeyDevices(void)
{
   memset(krt.HardKeys, (int)NULL, sizeof(krt.HardKeys));
}

/* Function for getting device pointer through device name */
static int
GrabKeyDevice(Ecore_X_Window win, const char* DeviceName, const int DeviceID)
{
   XEventClass eventList[32];
   XEventClass cls;
   XDevice *pDev = NULL;
   int res;

   pDev = XOpenDevice(krt.disp, DeviceID);
   if (!pDev)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt][%s] Fail to open the device (id=%d) !\n",
             __FUNCTION__, DeviceID);
        goto out;
     }

   /* key events */
   DeviceKeyPress(pDev, krt.nInputEvent[INPUTEVENT_KEY_PRESS], cls);
   if (cls) eventList[INPUTEVENT_KEY_PRESS] = cls;

   DeviceKeyRelease(pDev, krt.nInputEvent[INPUTEVENT_KEY_RELEASE], cls);
   if (cls) eventList[INPUTEVENT_KEY_RELEASE] = cls;

   res = XGrabDevice(krt.disp,
                     pDev,
                     win,
                     False,
                     INPUTEVENT_KEY_RELEASE + 1,
                     eventList,
                     GrabModeAsync,
                     GrabModeAsync,
                     CurrentTime);
   if (res)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt][%s] Fail to grab the device (error=%d, id=%d) !\n",
             __FUNCTION__, res, DeviceID);

        if (pDev) XCloseDevice(krt.disp, pDev);
        goto out;
     }

   return 1;

out:
   return 0;
}

static void
detachSlave(int DeviceID)
{
   XIDetachSlaveInfo detach;
   detach.type = XIDetachSlave;
   detach.deviceid = DeviceID;

   XIChangeHierarchy(krt.disp,
                     (XIAnyHierarchyChangeInfo*)&detach,
                     1);
}

static int
GrabKeyDevices(Ecore_X_Window win)
{
   int i, ndevices, result;
   XIDeviceInfo *dev, *info = NULL;
   KeyrouterDeviceType kdtype;
   E_Keyrouter_Device_Info *data = NULL;

   info = XIQueryDevice(krt.disp,
                        XIAllDevices,
                        &ndevices);
   if (!info)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt][%s] There is no queried XI device.\n",
             __FUNCTION__);
        return 0;
     }

   for (i = 0; i < ndevices; i++)
     {
        dev = &info[i];
        kdtype = E_KEYROUTER_HWKEY;

        switch (dev->use)
          {
           case XISlavePointer:
              if (strcasestr(dev->name, "XTEST") ||
                  strcasestr(dev->name, "Touch") ||
                  strcasestr(dev->name, "mouse"))
                continue;

              SLOG(LOG_DEBUG, "krt",
                   "[krt] XISlavePointer but has key events... "
                   "(device id=%d, name=%s)\n",
                   dev->deviceid, dev->name);

              kdtype = E_KEYROUTER_KEYBOARD;
              goto keyboard_added;
              break;

           case XISlaveKeyboard:
              if (strcasestr(dev->name, "XTEST"))
                continue;

              if (strcasestr(dev->name, "keyboard"))
                kdtype = E_KEYROUTER_KEYBOARD;

keyboard_added:
              if (kdtype == E_KEYROUTER_HWKEY)
                {
                   result = GrabKeyDevice(win, dev->name, dev->deviceid);
                   if (!result)
                     {
                        SLOG(LOG_DEBUG, "krt",
                             "[krt] Failed to grab key device"
                             "(name=%s, result=%d)\n",
                             dev->name, result);
                        continue;
                     }

                   _e_keyrouter_grab_hwkeys(0);
                }
              else
                _e_keyrouter_grab_hwkeys(dev->deviceid);

              data = malloc(sizeof(E_Keyrouter_Device_Info));
              if (data)
                {
                   data->id = dev->deviceid;
                   data->name = eina_stringshare_add(dev->name);
                   data->type = kdtype;

                   krt.device_list = eina_list_append(krt.device_list, data);
                }

              if (kdtype == E_KEYROUTER_HWKEY)
                {
                   detachSlave(dev->deviceid);
                   krt.num_hwkey_devices++;
                }
          }
     }

   XIFreeDeviceInfo(info);

   return 1;
}

static void
reattachSlave(int slave, int master)
{
   XIAttachSlaveInfo attach;

   attach.type = XIAttachSlave;
   attach.deviceid = slave;
   attach.new_master = master;

   XIChangeHierarchy(krt.disp,
                     (XIAnyHierarchyChangeInfo*)&attach,
                     1);
}

static void
UngrabKeyDevices(void)
{
   int i, ndevices;
   XIDeviceInfo *dev, *info = NULL;
   XDevice* pDev = NULL;

   info = XIQueryDevice(krt.disp, XIAllDevices, &ndevices);
   if (!info)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt][%s] There is no queried XI device.\n",
             __FUNCTION__);
        return;
     }

   for (i = 0; i < ndevices; i++)
     {
        dev = &info[i];

        if (XIFloatingSlave != dev->use)
          continue;

        if (dev->num_classes > 1) /* only for Floated SlaveKeyboard */
          continue;

        pDev = XOpenDevice(krt.disp, dev->deviceid);
        if (!pDev) continue;

        XUngrabDevice(krt.disp, pDev, CurrentTime);
        XCloseDevice(krt.disp, pDev);
        pDev = NULL;

        reattachSlave(dev->deviceid, 3); /* reattach to Virtual Core Keyboard */
     }

   XIFreeDeviceInfo(info);
}

static void
PrintKeyDeliveryList(void)
{
   int index;

   for (index = 0; index < MAX_HARDKEYS; index++)
     {
        char *keyname;
        int pid;
        KeySym ks;

        if (krt.HardKeys[index].keycode == 0) /* empty */
          continue;
        ks = XkbKeycodeToKeysym(krt.disp, index, 0, 0);

        SECURE_SLOGD("\n");
        keyname = XKeysymToString(ks);

        if (!keyname) continue;

        if      (!strncmp(keyname, KEY_VOLUMEDOWN, LEN_KEY_VOLUMEDOWN)) SECURE_SLOGD("[ KEY_VOLUMEDOWN : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_VOLUMEUP, LEN_KEY_VOLUMEUP)) SECURE_SLOGD("[ KEY_VOLUMEUP : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_PAUSE, LEN_KEY_PAUSE)) SECURE_SLOGD("[ KEY_PAUSE : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_SEND, LEN_KEY_SEND)) SECURE_SLOGD("[ KEY_SEND : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_SELECT, LEN_KEY_SELECT)) SECURE_SLOGD("[ KEY_SELECT : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_END, LEN_KEY_END)) SECURE_SLOGD("[ KEY_END : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_POWER, LEN_KEY_POWER)) SECURE_SLOGD("[ KEY_POWER : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_CAMERA, LEN_KEY_CAMERA)) SECURE_SLOGD("[ KEY_CAMERA : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_CONFIG, LEN_KEY_CONFIG)) SECURE_SLOGD("[ KEY_CONFIG : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_MEDIA, LEN_KEY_MEDIA)) SECURE_SLOGD("[ KEY_MEDIA : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_PLAYCD, LEN_KEY_PLAYCD)) SECURE_SLOGD("[ KEY_PLAYCD : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_STOPCD, LEN_KEY_STOPCD)) SECURE_SLOGD("[ KEY_STOPCD : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_PAUSECD, LEN_KEY_PAUSECD)) SECURE_SLOGD("[ KEY_PAUSECD : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_NEXTSONG, LEN_KEY_NEXTSONG)) SECURE_SLOGD("[ KEY_NEXTSONG : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_PREVIOUSSONG, LEN_KEY_PREVIOUSSONG)) SECURE_SLOGD("[ KEY_PREVIOUSSONG : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_REWIND, LEN_KEY_REWIND)) SECURE_SLOGD("[ KEY_REWIND : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_FASTFORWARD, LEN_KEY_FASTFORWARD)) SECURE_SLOGD("[ KEY_FASTFORWARD : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_PLAYPAUSE, LEN_KEY_PLAYPAUSE)) SECURE_SLOGD("[ KEY_PLAYPAUSE : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_MUTE, LEN_KEY_MUTE)) SECURE_SLOGD("[ KEY_MUTE : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_HOMEPAGE, LEN_KEY_HOMEPAGE)) SECURE_SLOGD("[ KEY_HOMEPAGE : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_WEBPAGE, LEN_KEY_WEBPAGE)) SECURE_SLOGD("[ KEY_WEBPAGE : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_MAIL, LEN_KEY_MAIL)) SECURE_SLOGD("[ KEY_MAIL : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_SCREENSAVER, LEN_KEY_SCREENSAVER)) SECURE_SLOGD("[ KEY_SCREENSAVER : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_BRIGHTNESSUP, LEN_KEY_BRIGHTNESSUP)) SECURE_SLOGD("[ KEY_BRIGHTNESSUP : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_BRIGHTNESSDOWN, LEN_KEY_BRIGHTNESSDOWN)) SECURE_SLOGD("[ KEY_BRIGHTNESSDOWN : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_SOFTKBD, LEN_KEY_SOFTKBD)) SECURE_SLOGD("[ KEY_SOFTKBD : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_QUICKPANEL, LEN_KEY_QUICKPANEL)) SECURE_SLOGD("[ KEY_QUICKPANEL : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_TASKSWITCH, LEN_KEY_TASKSWITCH)) SECURE_SLOGD("[ KEY_TASKSWITCH : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_APPS, LEN_KEY_APPS)) SECURE_SLOGD("[ KEY_APPS : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_SEARCH, LEN_KEY_SEARCH)) SECURE_SLOGD("[ KEY_SEARCH : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_VOICE, LEN_KEY_VOICE)) SECURE_SLOGD("[ KEY_VOICE : %s : %d ]\n", keyname, index);
        else if (!strncmp(keyname, KEY_LANGUAGE, LEN_KEY_LANGUAGE)) SECURE_SLOGD("[ KEY_LANGUAGE : %s : %d ]\n", keyname, index);
        else SECURE_SLOGD("[ UNKNOWN : %d ]\n", krt.HardKeys[index].keycode);

        /* Print EXCLUSIVE mode of grab */
        if (NULL != krt.HardKeys[index].excl_ptr)
          {
             ecore_x_netwm_pid_get((int)(krt.HardKeys[index].excl_ptr->wid), &pid);
             SECURE_SLOGD("== EXCLUSIVE : Window(0x%X) -> pid(%d)\n",
                          (int)(krt.HardKeys[index].excl_ptr->wid),
                          pid);
          }
        else
          {
             SECURE_SLOGD("== EXCLUSIVE : None\n");
          }

        /* Print OR_EXCLUSIVE mode of grab */
        if (NULL != krt.HardKeys[index].or_excl_ptr)
          {
             keylist_node* or_excl_ptr;
             or_excl_ptr = krt.HardKeys[index].or_excl_ptr;
             SECURE_SLOGD("== OR_EXCLUSIVE : ");

             do
               {
                  ecore_x_netwm_pid_get((unsigned int)(or_excl_ptr->wid), &pid);
                  SECURE_SLOGD("Window(0x%X) -> pid(%d)",
                               (unsigned int)(or_excl_ptr->wid),
                               pid);
                  or_excl_ptr = or_excl_ptr->next;
               }
             while (or_excl_ptr);

             SECURE_SLOGD("None\n");
          }
        else
          {
             SECURE_SLOGD("== OR_EXCLUSIVE : None\n");
          }

        /* Print TOP_POSITION mode of grab */
        if (NULL != krt.HardKeys[index].top_ptr)
          {
             keylist_node* top_ptr;
             top_ptr = krt.HardKeys[index].top_ptr;
             SECURE_SLOGD("== TOP_POSITION : ");

             do
               {
                  ecore_x_netwm_pid_get((unsigned int)(top_ptr->wid), &pid);
                  SECURE_SLOGD("Window(0x%X) -> pid(%d)", (unsigned int)(top_ptr->wid), pid);
                  top_ptr = top_ptr->next;
               }
             while (top_ptr);

             SECURE_SLOGD("None\n");
          }
        else
          {
             SECURE_SLOGD("== TOP_POSITION : None\n");
          }

        /* Print SHARED mode of grab */
        if (NULL != krt.HardKeys[index].shared_ptr)
          {
             keylist_node* shared_ptr;
             shared_ptr = krt.HardKeys[index].shared_ptr;
             SECURE_SLOGD("== SHARED : ");

             do
               {
                  ecore_x_netwm_pid_get((unsigned int)(shared_ptr->wid), &pid);
                  SECURE_SLOGD("Window(0x%X) -> pid(%d)", (unsigned int)(shared_ptr->wid), pid);
                  shared_ptr = shared_ptr->next;
               }
             while (shared_ptr);

             SECURE_SLOGD("None\n");
          }
        else
          {
             SECURE_SLOGD("== SHARED : None\n");
          }

        if ((ks >= 0x01000100) && (ks <= 0x0110ffff))
          free(keyname);
     }

   return;
}

static int
RemoveWindowDeliveryList(Ecore_X_Window win, int isTopPositionMode, int UnSetExclusiveProperty)
{
   int index, mode_count = 0;

   /* Remove win from EXCLUSIVE, TOP_POSITION and SHARED  grab list ! */
   /* If isTopPosition is true, remove win only from TOP_POSITION grab list ! */
   for (index = 0; index < MAX_HARDKEYS; index++)
     {
        if (krt.HardKeys[index].keycode == 0) /* empty */
          continue;

        if (isTopPositionMode)
          {
             /* Check & Delete TOP_POSITION mode of grab */
             if (NULL != krt.HardKeys[index].top_ptr)
               {
                  int flags = 0;
                  keylist_node* current;
                  keylist_node* next_current;

                  current = krt.HardKeys[index].top_ptr;

                  if (!current) continue;

                  next_current = current->next;

                  do
                    {
                       if (win == krt.HardKeys[index].top_ptr->wid)
                         {
                            if (current->next)
                              krt.HardKeys[index].top_ptr = current->next;
                            else
                              {
                                 krt.HardKeys[index].top_ptr = NULL;
                                 krt.HardKeys[index].top_tail = NULL;
                                 mode_count += TOP_GRAB_MODE;
                              }

                            if (current)
                              free(current);
                            flags = 1;
                            break;
                         }

                       if (NULL == next_current)
                         {
                            break;
                         }

                       if (win == next_current->wid)
                         {
                            if (next_current->next)
                              current->next = next_current->next;
                            else
                              {
                                 current->next = NULL;
                                 krt.HardKeys[index].top_tail = current;
                              }

                            if (next_current)
                              free(next_current);
                            flags = 1;
                            break;
                         }

                       current = next_current;
                       next_current = next_current->next;

                    }
                  while (NULL != next_current);

                  if (flags)
                    {
                       continue;
                    }
               }
          }
        else /* isTopPositionMode == 0 */
          {
             /* Check & Delete EXCLUSIVE mode of grab */
             if (NULL != krt.HardKeys[index].excl_ptr)
               {
                  if (win == krt.HardKeys[index].excl_ptr->wid)
                    {
                       if (UnSetExclusiveProperty)
                         UnSetExclusiveGrabInfoToRootWindow(krt.HardKeys[index].keycode,
                                                            EXCLUSIVE_GRAB);

                       if (krt.HardKeys[index].excl_ptr)
                         free(krt.HardKeys[index].excl_ptr);
                       krt.HardKeys[index].excl_ptr = NULL;
                       mode_count += EXCL_GRAB_MODE;
                       continue; /* need to check another keycode */
                    }
               }

             /* Check & Delete OR_EXCLUSIVE mode of grab */
             if (NULL != krt.HardKeys[index].or_excl_ptr)
               {
                  int flags = 0;
                  keylist_node* current = NULL;
                  keylist_node* next_current = NULL;

                  current = krt.HardKeys[index].or_excl_ptr;
                  if (current)
                    {
                       next_current = current->next;
                       do
                         {
                            if (win == krt.HardKeys[index].or_excl_ptr->wid)
                              {
                                 if (current->next)
                                   krt.HardKeys[index].or_excl_ptr = current->next;
                                 else
                                   {
                                      krt.HardKeys[index].or_excl_ptr = NULL;
                                      mode_count += OR_EXCL_GRAB_MODE;
                                   }

                                 if (current)
                                   {
                                      free(current);
                                      current = NULL;
                                   }
                                 flags = 1;
                                 break;
                              }

                            if (NULL == next_current)
                              {
                                 break;
                              }

                            if (win == next_current->wid)
                              {
                                 if (next_current->next)
                                   current->next = next_current->next;
                                 else
                                   current->next = NULL;

                                 if (next_current)
                                   {
                                      free(next_current);
                                      next_current = NULL;
                                   }
                                 flags = 1;
                                 break;
                              }

                            current = next_current;
                            next_current = next_current->next;
                         }
                       while (next_current);
                    }
                  if (NULL == krt.HardKeys[index].or_excl_ptr)
                    UnSetExclusiveGrabInfoToRootWindow(krt.HardKeys[index].keycode,
                                                       OR_EXCLUSIVE_GRAB);

                  if (flags) continue;
               }

             /* heck & Delete TOP_POSITION mode of grab */
             if (NULL != krt.HardKeys[index].top_ptr)
               {
                  int flags = 0;
                  keylist_node* current;
                  keylist_node* next_current;

                  current = krt.HardKeys[index].top_ptr;

                  if (!current)
                    goto null_top_ptr;

                  next_current = current->next;

                  do
                    {
                       if (win == krt.HardKeys[index].top_ptr->wid)
                         {
                            if (current->next)
                              krt.HardKeys[index].top_ptr = current->next;
                            else
                              {
                                 krt.HardKeys[index].top_ptr = NULL;
                                 krt.HardKeys[index].top_tail = NULL;
                                 mode_count += TOP_GRAB_MODE;
                              }

                            if (current)
                              free(current);
                            flags = 1;
                            break;
                         }

                       if (NULL == next_current)
                         {
                            break;
                         }

                       if (win == next_current->wid)
                         {
                            if (next_current->next)
                              current->next = next_current->next;
                            else
                              {
                                 current->next = NULL;
                                 krt.HardKeys[index].top_tail = current;
                              }

                            if (next_current)
                              free(next_current);
                            flags = 1;
                            break;
                         }

                       current = next_current;
                       next_current = next_current->next;

                    }
                  while (NULL != next_current);

                  if (flags)
                    {
                       continue;
                    }
               }

null_top_ptr:
             /* Check & Delete SHARED mode of grab */
             if (NULL != krt.HardKeys[index].shared_ptr)
               {
                  int flags = 0;
                  keylist_node* current = NULL;
                  keylist_node* next_current = NULL;

                  current = krt.HardKeys[index].shared_ptr;
                  if (current)
                    {
                       next_current = current->next;
                       do
                         {
                            if (win == krt.HardKeys[index].shared_ptr->wid)
                              {
                                 if (current->next)
                                   krt.HardKeys[index].shared_ptr = current->next;
                                 else
                                   {
                                      krt.HardKeys[index].shared_ptr = NULL;
                                      mode_count += SHARED_GRAB_MODE;
                                   }

                                 if (current)
                                   {
                                      free(current);
                                      current = NULL;
                                   }
                                 flags = 1;
                                 break;
                              }

                            if (NULL == next_current)
                              {
                                 break;
                              }

                            if (win == next_current->wid)
                              {
                                 if (next_current->next)
                                   current->next = next_current->next;
                                 else
                                   current->next = NULL;

                                 if (next_current)
                                   {
                                      free(next_current);
                                      next_current = NULL;
                                   }
                                 flags = 1;
                                 break;
                              }

                            current = next_current;
                            next_current = next_current->next;

                         }
                       while (next_current);
                    }

                  if (flags) continue;
               }
          }
     }

   return 0;
}

static void
UnSetExclusiveGrabInfoToRootWindow(int keycode, int grab_mode)
{
   int i;
   int cnt = 0;
   unsigned int *key_list = NULL;
   int *new_key_list = NULL;

   int ret;
   int nr_item = 0;
   int sz_remains_data = 0;
   Ecore_X_Window ex_grabwin;

   if (grab_mode == EXCLUSIVE_GRAB)
     {
        if (krt.atomGrabExclWin == None)
          krt.atomGrabExclWin = ecore_x_atom_get(STR_ATOM_GRAB_EXCL_WIN);
        ex_grabwin = krt.atomGrabExclWin;
     }
   else if (grab_mode == OR_EXCLUSIVE_GRAB)
     {
        if (krt.atomGrabORExclWin == None)
          krt.atomGrabORExclWin = ecore_x_atom_get(STR_ATOM_GRAB_OR_EXCL_WIN);
        ex_grabwin = krt.atomGrabORExclWin;
     }
   else
     return;

   ret = ecore_x_window_prop_property_get(krt.rootWin,
                                           ex_grabwin,
                                           XA_CARDINAL,
                                           sz_remains_data,
                                           (unsigned char **)&key_list,
                                           &nr_item);
   if (!ret || !key_list)
     {
        nr_item = 0;
     }

   if (nr_item == 0) goto out;

   for (i = 0; i < nr_item; i++)
     {
        if (key_list[i] == keycode)
          {
             continue;
          }
        cnt++;
     }

   if (0 < cnt)
     {
        new_key_list = malloc(sizeof(int)*cnt);
        cnt = 0;
     }
   else
     new_key_list = NULL;

   if (!new_key_list)
     {
        ecore_x_window_prop_property_del(krt.rootWin, ex_grabwin);
        goto out;
     }

   for (i = 0; i < nr_item; i++)
     {
        if (key_list[i] == keycode)
          continue;
        else
          new_key_list[cnt++] = key_list[i];
     }

   ecore_x_window_prop_property_set(krt.rootWin,
                                    ex_grabwin,
                                    XA_CARDINAL,
                                    32,
                                    (unsigned char *)new_key_list,
                                    cnt);

out:
   if (new_key_list)
     free(new_key_list);
   if (key_list)
     free(key_list);
   return;
}

static int
AddWindowToDeliveryList(Ecore_X_Window win, int keycode, const int grab_mode, const int IsOnTop)
{
   int ret = 0;
   int index = keycode;

   if (index >= MAX_HARDKEYS)
     {
        SECURE_SLOGD("[krt][%s] Error ! index of krt.HardKeys must be smaller "
                     "than %d (index=%d)!)\n", __FUNCTION__, MAX_HARDKEYS, index);
        ret = -1;
        goto out;
     }

   keylist_node* ptr = NULL;
   krt.HardKeys[index].keycode = keycode;
   switch (grab_mode)
     {
      case EXCLUSIVE_GRAB:
         if (NULL != krt.HardKeys[index].excl_ptr)
           {
              SECURE_SLOGD("[krt][%s] krt.HardKeys[%d].Keycode(%d) was "
                           "EXCLUSIVELY grabbed already by window(0x%x) !\n",
                           __FUNCTION__, index, keycode,
                           (int)(krt.HardKeys[index].excl_ptr->wid));
              ret = -1;
              goto out;
           }

         ptr = (keylist_node*)malloc(sizeof(keylist_node));
         if (!ptr)
           {
              SECURE_SLOGD("[krt][%s] Failed to allocate memory for adding "
                           "excl_ptr!\n", __FUNCTION__);
              ret = -1;
              goto out;
           }

         ptr->wid = win;
         ptr->next = NULL;
         krt.HardKeys[index].excl_ptr = ptr;
         break;

      case OR_EXCLUSIVE_GRAB:
         ptr = (keylist_node*)malloc(sizeof(keylist_node));
         if (!ptr)
           {
              SLOG(LOG_WARN,
                   "krt", "Failed to allocate memory for adding "
                   "or_excl_ptr!\n");
              ret = -1;
              goto out;
           }

         ptr->wid = win;
         if (NULL != krt.HardKeys[index].or_excl_ptr)
           {
              ptr->next = krt.HardKeys[index].or_excl_ptr;
              krt.HardKeys[index].or_excl_ptr = ptr;
           }
         else
           {
              ptr->next=NULL;
              krt.HardKeys[index].or_excl_ptr=ptr;
           }
         break;

      case TOP_POSITION_GRAB:
         ptr = (keylist_node*)malloc(sizeof(keylist_node));
         if (!ptr)
           {
              SLOG(LOG_DEBUG,
                   "krt", "[krt][%s] Failed to allocate "
                   "memory for adding top_ptr!\n",
                   __FUNCTION__);
              ret = -1;
              goto out;
           }

         ptr->wid = win;
         ptr->next = NULL;

         if (NULL == krt.HardKeys[index].top_ptr)
           {
              krt.HardKeys[index].top_tail = ptr;
              krt.HardKeys[index].top_ptr = ptr;
              break;
           }

         if (IsOnTop)
           {
              ptr->next = krt.HardKeys[index].top_ptr;
              krt.HardKeys[index].top_ptr = ptr;
           }
         else
           {
              krt.HardKeys[index].top_tail->next = ptr;
              krt.HardKeys[index].top_tail = ptr;
           }
         break;

      case SHARED_GRAB:
         ptr = (keylist_node*)malloc(sizeof(keylist_node));
         if (!ptr)
           {
              SLOG(LOG_DEBUG, "krt",
                   "[krt][%s] Failed to allocate memory for "
                   "adding shared_ptr!\n",
                   __FUNCTION__);
              ret = -1;
              goto out;
           }

         ptr->wid = win;
         if (NULL != krt.HardKeys[index].shared_ptr)
           {
              ptr->next = krt.HardKeys[index].shared_ptr;
              krt.HardKeys[index].shared_ptr = ptr;
           }
         else
           {
              ptr->next = NULL;
              krt.HardKeys[index].shared_ptr = ptr;
           }
         break;

      default:
         SECURE_SLOGD("[krt][%s] Unknown mode of grab ! "
                      "(grab_mode=0x%X)\n",
                      __FUNCTION__, grab_mode);
         ret = -1;
         break;
     }

out:
   return ret;
}

static int
AddWindowToPressedList(Window win, int keycode, const int grab_mode)
{
   keylist_node* ptr = NULL;
   ptr = (keylist_node*)malloc(sizeof(keylist_node));
   int ret = 1;
   if (!ptr)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt] Failed to allocate memory for adding "
             "pressed_win_ptr!\n");

        ret = 0;
        goto out;
     }

   ptr->wid = win;
   krt.HardKeys[keycode].lastmode = grab_mode;
   if (NULL != krt.HardKeys[keycode].pressed_win_ptr)
     {
        ptr->next = krt.HardKeys[keycode].pressed_win_ptr;
        krt.HardKeys[keycode].pressed_win_ptr = ptr;
     }
   else
     {
        ptr->next = NULL;
        krt.HardKeys[keycode].pressed_win_ptr = ptr;
     }
out:
   return ret;
}

static int
AdjustTopPositionDeliveryList(Ecore_X_Window win, int IsOnTop)
{
   int nr_item = 0;
   int sz_remains_data = 0;
   unsigned int *key_list = NULL;

   int i, result;
   int grab_mode, keycode;

   if (krt.atomGrabKey == None)
     {
        krt.atomGrabKey = ecore_x_atom_get(STR_ATOM_GRAB_KEY);
     }

   result = ecore_x_window_prop_property_get(win,
                                             krt.atomGrabKey,
                                             XA_CARDINAL,
                                             sz_remains_data,
                                             (unsigned char **)&key_list,
                                             &nr_item);
   if (!result || !key_list)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt][%s] Failed to get window property from %s ! "
             "(result = %d)\n",
             __FUNCTION__, STR_ATOM_GRAB_KEY, result);

        RemoveWindowDeliveryList(win, 1, 0);
        goto out;
     }

   if (0 == nr_item)
     {
        RemoveWindowDeliveryList(win, 1, 0);
        goto out;
     }

   RemoveWindowDeliveryList(win, 1, 0);

   for (i = 0; i < nr_item; i++)
     {
        grab_mode = key_list[i] & GRAB_MODE_MASK;

        if (TOP_POSITION_GRAB != grab_mode)
          continue;

        keycode = key_list[i] & (~GRAB_MODE_MASK);
        _e_keyrouter_update_key_delivery_list(win,
                                              keycode,
                                              TOP_POSITION_GRAB,
                                              IsOnTop);
     }

out:
   if (key_list) free(key_list);
   return 0;
}

static int
IsGrabbed(unsigned int keycode)
{
   int index = keycode;

   if (krt.HardKeys[index].keycode == 0) /* empty */
     {
        goto out;
     }

   if (krt.HardKeys[index].keycode != keycode)
     {
        SECURE_SLOGD("[krt][%s] Error ! (krt.HardKeys[%d].keycode "
                     "must be equal to keycode(%d) !\n",
                     __FUNCTION__, index, keycode);
        goto out;
     }

   if (NULL != krt.HardKeys[index].excl_ptr)
     {
        index |= EXCLUSIVE_GRAB;
     }
   else if (NULL != krt.HardKeys[index].or_excl_ptr)
     {
        index |= OR_EXCLUSIVE_GRAB;
     }
   else if (NULL != krt.HardKeys[index].top_ptr)
     {
        index |= TOP_POSITION_GRAB;
     }
   else if (NULL != krt.HardKeys[index].shared_ptr)
     {
        index |= SHARED_GRAB;
     }
   else
     {
        index = -1;
     }

   return index;

out:
   return -1;
}

static E_Client *
_e_keyrouter_find_client_by_window(Window win)
{
   const Eina_List *l;
   E_Client *ec = NULL;

   EINA_LIST_FOREACH(e_comp->clients, l, ec)
     {
        if (ec)
          {
             if (e_client_util_win_get(ec) == win)
               break;
          }
     }

   return ec;
}

static int
IsWindowTopVisibleWithoutInputFocus(keylist_node *top_ptr, Ecore_X_Window focus, Ecore_X_Window *dest_window)
{
   int num_children;
   Ecore_X_Window *child_list = NULL;
   keylist_node *ptr = NULL;
   int i;
   E_Client *ec;

   child_list = ecore_x_window_children_get(krt.rootWin, &num_children);
   if (!child_list)
     {
        SLOG(LOG_DEBUG, "krt",
             "[krt][%s] Failed to query window tree !\n",
             __FUNCTION__);
        return 0;
     }

   /* Make the wininfo list */
   for (i = (int)num_children - 1; i >= 0; i--)
     {
        /* 1. check map status of window
         * figure out whether the window is mapped or not
         * if map status is 0 or border's visible is 0 */
        if (ecore_x_window_visible_get(child_list[i]) == 0)
          continue;

        /* 3. check window is border or not
         * Event though Border is delete status, bd->client.shell_win and bd->win
         * is still existed in server side. So focus window is bd->client.shell_win.
         * In this case we will deliver key-event to the TOP_POSITION grab window.
         * That is bug.
         */
        ec = _e_keyrouter_find_client_by_window(child_list[i]);
        if (ec)
          {
             if (!ec->visible)
               {
                  if (e_client_util_win_get(ec) == focus)
                    {
                       free(child_list);
                       return 0;
                    }
                  else
                    continue;
               }

             if ((ec->x >= ec->zone->w) || (ec->y >= ec->zone->h))
               continue;

             if (((ec->x + ec->w) <= 0) || ((ec->y + ec->h) <= 0))
               continue;
          }

        for (ptr = top_ptr; (NULL != ptr); ptr=ptr->next)
          {
             if (ec) /* child_list[i] is border */
               {
                  /* if the window is client window, check hint */
                  if (e_client_util_win_get(ec) == ptr->wid)
                    {
                       *dest_window = ptr->wid;
                       free(child_list);
                       return 1;
                    }
               }
             else /* child_list[i] is override-redirected window */
               {
                  /* if the window is not client window, it will be a border window
                   * or a override-redirected window then check the equality of the
                   * windows
                   */
                  if (child_list[i] == ptr->wid)
                    {
                       *dest_window = ptr->wid;
                       free(child_list);
                       return 1;
                    }
               }
          }

        if (ec)
          {
             if (e_client_util_win_get(ec) == focus)
               {
                  free(child_list);
                  return 0;
               }
          }
        else if (child_list[i] == focus)
          {
             free(child_list);
             return 0;
          }
     }
   free(child_list);
   return 0;
}

static void
DeliverDeviceKeyEvents(XEvent *xev)
{
   int index;
   Ecore_X_Window focus_window;
   keylist_node* ptr = NULL;
   Ecore_X_Window dest_window = None;

   index = IsGrabbed(xev->xkey.keycode);

   if (index < 0 && krt.HardKeys[xev->xkey.keycode].bind)
     {
        SECURE_SLOGD("[krt][DeliverDeviceKeyEvents] key(keycode=%d, name=%s) "
                     "was bound !\n", xev->xkey.keycode,
                     krt.HardKeys[xev->xkey.keycode].bind->key);

        _e_keyrouter_do_bound_key_action(xev);
        return;
     }

   focus_window = ecore_x_window_focus_get();

   /* Is Grabbed ? */
   if (index < 0) /* Code for non-grabbed key */
     {
        /* Deliver to focus window */
        xev->xkey.window = focus_window;
        if (xev->type == KeyPress)
          {
             AddWindowToPressedList(xev->xkey.window,
                                    xev->xkey.keycode,
                                    NONE_GRAB_MODE);
             XSendEvent(krt.disp,
                        xev->xkey.window,
                        False,
                        KeyPressMask | KeyReleaseMask,
                        xev);

             SECURE_SLOGD("[krt][%s] Non-grabbed key! Deliver %s (keycode:%d) "
                          "to focus window (0x%x) !\n", __FUNCTION__,
                          (xev->type == KeyPress) ? "KeyPress" : "KeyRelease",
                          xev->xkey.keycode,
                          (int)focus_window);
          }
        else
          {
             keylist_node *tmp_ptr;
             for (ptr = krt.HardKeys[xev->xkey.keycode].pressed_win_ptr; (NULL != ptr);)
               {
                  xev->xkey.window = ptr->wid;

                  SECURE_SLOGD("[krt][%s] Deliver KeyRelease (keycode:%d) "
                               "to window (0x%x) !\n", __FUNCTION__,
                               xev->xkey.keycode,
                               (int)xev->xkey.window);

                  XSendEvent(krt.disp,
                             xev->xkey.window,
                             False,
                             KeyPressMask | KeyReleaseMask,
                             xev);

                  tmp_ptr = ptr;
                  ptr=ptr->next ;
                  free(tmp_ptr);
               }

             krt.HardKeys[xev->xkey.keycode].pressed_win_ptr = NULL;
          }
        return;
     }

   int grab_mode = index & GRAB_MODE_MASK;
   index &= ~GRAB_MODE_MASK;

   if (xev->type == KeyPress)
     {
        switch (grab_mode)
          {
           case EXCLUSIVE_GRAB:
              /* Is Grab Mode equal to EXCLUSIVE ? */
              xev->xkey.window = krt.HardKeys[index].excl_ptr->wid;
              AddWindowToPressedList(xev->xkey.window,
                                     xev->xkey.keycode,
                                     EXCLUSIVE_GRAB);
              XSendEvent(krt.disp,
                         xev->xkey.window,
                         False,
                         KeyPressMask | KeyReleaseMask,
                         xev);

              SECURE_SLOGD("[krt][%s] EXCLUSIVE mode of grab ! Deliver %s "
                           "(keycode:%d) to window (0x%x) !\n", __FUNCTION__,
                           (xev->type == KeyPress) ? "KeyPress" : "KeyRelease",
                           xev->xkey.keycode, (int)xev->xkey.window);
              break;

           case OR_EXCLUSIVE_GRAB:
              /* Is Grab Mode equal to OR_EXCLUSIVE ? */
              xev->xkey.window = krt.HardKeys[index].or_excl_ptr->wid;
              AddWindowToPressedList(xev->xkey.window,
                                     xev->xkey.keycode,
                                     OR_EXCLUSIVE_GRAB);

              XSendEvent(krt.disp,
                         xev->xkey.window,
                         False,
                         KeyPressMask | KeyReleaseMask,
                         xev);

              SECURE_SLOGD("[krt][%s] OR_EXCLUSIVE mode of grab ! Deliver %s "
                           "(keycode:%d) to window (0x%x) !\n", __FUNCTION__,
                           (xev->type == KeyPress) ? "KeyPress" : "KeyRelease",
                           xev->xkey.keycode, (int)xev->xkey.window);
              break;

           case TOP_POSITION_GRAB:
              if (focus_window != krt.HardKeys[index].top_ptr->wid)
                {
                   if (focus_window == 0)
                     {
                        SECURE_SLOGD("[krt][%s] Focus_Window is NULL. "
                                     "Try it agin. focus_window=(0x%x) !\n",
                                     __FUNCTION__, focus_window);

                        focus_window = ecore_x_window_focus_get();
                     }

                   if ((krt.isWindowStackChanged) ||
                       (krt.prev_sent_keycode != xev->xkey.keycode))
                     {
                        krt.resTopVisibleCheck = IsWindowTopVisibleWithoutInputFocus(krt.HardKeys[index].top_ptr,
                                                                                           focus_window,
                                                                                           &dest_window);
                     }

                   krt.prev_sent_keycode = xev->xkey.keycode;

                   if (!krt.resTopVisibleCheck)
                     goto shared_delivery;

                   if (krt.isWindowStackChanged)
                     krt.isWindowStackChanged = 0;
                }

              /* Is Grab Mode equal to TOP_POSITION ? */
              if ((krt.resTopVisibleCheck == 1) &&
                  (dest_window != None))
                xev->xkey.window = dest_window;
              else
                xev->xkey.window = krt.HardKeys[index].top_ptr->wid;

              AddWindowToPressedList(xev->xkey.window,
                                     xev->xkey.keycode,
                                     TOP_POSITION_GRAB);

              XSendEvent(krt.disp,
                         xev->xkey.window,
                         False,
                         KeyPressMask | KeyReleaseMask,
                         xev);

              SECURE_SLOGD("[krt][%s] TOP_POSITION mode of grab ! Deliver %s "
                           "(keycode:%d) to window (0x%x) !\n",
                           __FUNCTION__,
                           (xev->type == KeyPress) ? "KeyPress" : "KeyRelease",
                           xev->xkey.keycode,
                           (int)xev->xkey.window);
              break;

           case SHARED_GRAB:
shared_delivery:
              /* Deliver to focus_window first */
              xev->xkey.window = focus_window;

              AddWindowToPressedList(xev->xkey.window,
                                     xev->xkey.keycode,
                                     SHARED_GRAB);

              XSendEvent(krt.disp,
                         xev->xkey.window,
                         False,
                         KeyPressMask | KeyReleaseMask,
                         xev);

              SECURE_SLOGD("[krt][%s] Deliver %s (keycode:%d) "
                           "to focus window (0x%x)!\n",
                           __FUNCTION__,
                           (xev->type == KeyPress) ? "KeyPress" : "KeyRelease",
                           xev->xkey.keycode,
                           (unsigned int)xev->xkey.window);

              /* Deliver to shared grabbed window(s) */
              for (ptr = krt.HardKeys[index].shared_ptr; (NULL != ptr); ptr = ptr->next)
                {
                   if (ptr->wid == focus_window) continue;

                   xev->xkey.window = ptr->wid;

                   AddWindowToPressedList(xev->xkey.window,
                                          xev->xkey.keycode,
                                          SHARED_GRAB);

                   XSendEvent(krt.disp,
                              xev->xkey.window,
                              False,
                              KeyPressMask | KeyReleaseMask,
                              xev);

                   SECURE_SLOGD("[krt][%s] SHARED mode of grab ! Deliver %s "
                                "(keycode:%d) to window (0x%x) !\n",
                                __FUNCTION__,
                                (xev->type == KeyPress) ? "KeyPress" : "KeyRelease",
                                xev->xkey.keycode,
                                (int)xev->xkey.window);
                }
              break;

           default:
              SECURE_SLOGD("[krt][%s] Unknown mode of grab "
                           "(mode = %d, index = %d, keycode = %d)\n",
                           __FUNCTION__, grab_mode, index,
                           xev->xkey.keycode);
              break;
          }
     }
   else
     {
        keylist_node *tmp_ptr;
        for (ptr = krt.HardKeys[index].pressed_win_ptr; (NULL != ptr) ;)
          {
             xev->xkey.window = ptr->wid;

             SECURE_SLOGD("[krt][%s] Deliver KeyRelease (keycode:%d) "
                          "to window (0x%x) !\n",
                          __FUNCTION__,
                          xev->xkey.keycode,
                          (int)xev->xkey.window);

             XSendEvent(krt.disp,
                        xev->xkey.window,
                        False,
                        KeyPressMask | KeyReleaseMask,
                        xev);

             tmp_ptr = ptr;
             ptr=ptr->next ;
             free(tmp_ptr);
          }

        krt.HardKeys[index].pressed_win_ptr = NULL;
     }
}

static void
_e_keyrouter_do_hardkey_emulation(const char *label, unsigned int key_event, unsigned int on_release, int keycode, int cancel)
{
   XEvent xev;

   if (label) return;

   memset(&xev, 0, sizeof(XEvent));
   xev.xkey.display = krt.disp;
   xev.xkey.root = krt.rootWin;
   xev.xkey.keycode = keycode;
   xev.xkey.time = CurrentTime;
   xev.xkey.type = key_event;

   if (cancel)
     {
        _e_keyrouter_cancel_key(&xev, keycode);

        SECURE_SLOGD("[krt][do_hardkey_emulation] HWKeyEmulation Done !\n");
        SECURE_SLOGD("...(Cancel KeyPress + KeyRelease(keycode:%d) + Cancel KeyRelease)\n",
                     xev.xkey.keycode);
     }
   else
     {
        DeliverDeviceKeyEvents(&xev);

        SECURE_SLOGD("[krt][do_hardkey_emulation] HWKeyEmulation Done !\n");
        SECURE_SLOGD("...(%s(keycode=%d)\n",
                     (xev.xkey.type==KeyPress) ? "KeyPress" : "KeyRelease",
                     xev.xkey.keycode);
     }
}

static void
Device_Status(unsigned int val)
{
   Eina_List* l;
   E_Keyrouter_Device_Info *data;

   SLOG(LOG_DEBUG, "krt", "\n[krt] - Device Status = Start\n");
   if (!krt.device_list)
     {
        SLOG(LOG_DEBUG, "krt", "No input devices...\n");
        goto out;
     }

   EINA_LIST_FOREACH(krt.device_list, l, data)
     {
        if (!data) continue;
        SLOG(LOG_DEBUG, "krt", "Device id : %d Name : %s\n", data->id, data->name);
        switch (data->type)
          {
           case E_KEYROUTER_HWKEY:      SLOG(LOG_DEBUG, "krt", "¦¦Device type : H/W Key Device\n");             break;
           case E_KEYROUTER_HOTPLUGGED: SLOG(LOG_DEBUG, "krt", "¦¦Device type : Hotplugged Key Device\n");      break;
           case E_KEYROUTER_KEYBOARD:   SLOG(LOG_DEBUG, "krt", "¦¦Device type : Hotplugged Keyboard Device\n"); break;
           default: SLOG(LOG_DEBUG, "krt", "¦¦Device type : Unknown\n");
          }
     }

out:
   SLOG(LOG_DEBUG, "krt", "\n[krt] - Device Status = End\n");
}

static void
Keygrab_Status(unsigned int val)
{
   SECURE_SLOGD("\n[krt] - Grab Status = Start\n");
   PrintKeyDeliveryList();
   SECURE_SLOGD("\n[krt] - Grab Status = End\n");
}

static void
InitModKeys(void)
{
   KeySym sym;
   unsigned int code;
   int i = 0;

   if (!krt.modkey)
     return;

   sym = XStringToKeysym(KEY_POWER);
   code = XKeysymToKeycode(krt.disp, krt.modkey[i].keys[0].keysym);
   krt.modkey[i].keys[0].keysym = sym;
   krt.modkey[i].keys[0].keycode = code;

   sym = XStringToKeysym(KEY_HOME);
   code = XKeysymToKeycode(krt.disp, krt.modkey[i].keys[1].keysym);
   krt.modkey[i].keys[1].keysym = sym;
   krt.modkey[i].keys[1].keycode = code;

   krt.modkey[i].press_only = EINA_TRUE;

   sym = XStringToKeysym(KEY_CANCEL);
   code = XKeysymToKeycode(krt.disp, krt.cancel_key.keysym);
   krt.cancel_key.keysym = sym;
   krt.cancel_key.keycode = code;

   SECURE_SLOGD("[krt][%s][%d] Modifier Key=%s (keycode:%d)\n",
                __FUNCTION__, i, KEY_POWER,
                krt.modkey[i].keys[0].keycode);
   SECURE_SLOGD("[krt][%s][%d] Composited Key=%s (keycode:%d)\n",
                __FUNCTION__, i, KEY_HOME,
                krt.modkey[i].keys[1].keycode);
   SECURE_SLOGD("[krt][%s][%d] Cancel Key=%s (keycode:%d)\n",
                __FUNCTION__, i, KEY_CANCEL,
                krt.cancel_key.keycode);
}

static void
ResetModKeyInfo(void)
{
   int i;

   for (i = 0; i < NUM_KEY_COMPOSITION_ACTIONS; i++)
     {
        krt.modkey[i].set = 0;
        krt.modkey[i].composited = 0;
        krt.modkey[i].time = 0;
        krt.modkey[i].idx_mod = 0;
        krt.modkey[i].idx_comp = 0;
     }

   krt.modkey_set = 0;

   SECURE_SLOGD("[krt][%s][%d] modkey_set=%d\n",
                __FUNCTION__, i, krt.modkey_set);
}

static int
IsModKey(unsigned int keycode, int index)
{
   int i, j = index;

   for (i = 0; i < NUM_COMPOSITION_KEY; i++)
     if (keycode == krt.modkey[j].keys[i].keycode)
       return (i+1);

   return 0;
}

static int
IsCompKey(unsigned int keycode, int index)
{
   int i = index;
   int mod_i = krt.modkey[i].idx_mod % NUM_COMPOSITION_KEY;

   if (keycode == krt.modkey[i].keys[mod_i].keycode)
     return 4;

   return 0;
}

static int
IsKeyComposited(unsigned int keycode, Time time, int index)
{
   int i = index;
   int mod_i = krt.modkey[i].idx_mod % NUM_COMPOSITION_KEY;

   if ((keycode == krt.modkey[i].keys[mod_i].keycode) &&
       (time <= (krt.modkey[i].time + KEY_COMPOSITION_TIME)))
     return 3;

   return 0;
}

static void
DoKeyCompositionAction(int index, int press)
{
   Atom xkey_composition_atom = None;

   int i = index;

   xkey_composition_atom = ecore_x_atom_get(STR_ATOM_XKEY_COMPOSITION);

   ecore_x_client_message32_send(krt.rootWin,
                                 xkey_composition_atom,
                                 StructureNotifyMask | SubstructureNotifyMask,
                                 krt.modkey[i].keys[0].keycode,
                                 krt.modkey[i].keys[1].keycode,
                                 press,
                                 0,
                                 0);

   ecore_x_client_message32_send(krt.noti_window,
                                 xkey_composition_atom,
                                 StructureNotifyMask | SubstructureNotifyMask,
                                 krt.modkey[i].keys[0].keycode,
                                 krt.modkey[i].keys[1].keycode,
                                 press,
                                 0,
                                 0);
   SECURE_SLOGD("\n[krt][%s][%d] Do Key Composition Action : ClientMessage "
                "to RootWindow(0x%x)!: %s\n", __FUNCTION__, i, krt.rootWin,
                press ? "Press" : "Release");

   SECURE_SLOGD("\n[krt][%s][%d] Do Key Composition Action : ClientMessage "
                "to Keyrouter NotiWindow (0x%x)!: %s\n", __FUNCTION__, i,
                krt.noti_window, press ? "Press" : "Release");
}
