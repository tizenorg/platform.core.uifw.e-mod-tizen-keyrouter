#define E_COMP_WL
#include "e.h"
#include "e_mod_main_wl.h"
#include <string.h>

E_KeyrouterPtr krt = NULL;
EAPI E_Module_Api e_modapi = { E_MODULE_API_VERSION, "Keyrouter Module of Window Manager" };

static Eina_Bool _e_keyrouter_init();
static void _e_keyrouter_init_handlers(void);
static void _e_keyrouter_deinit_handlers(void);
static Eina_Bool _e_keyrouter_process_key_event(void *event, int type);
static Eina_Bool _e_keyrouter_send_key_events(int type, Ecore_Event_Key *ev);
static Eina_Bool _e_keyrouter_send_key_events_press(int type, Ecore_Event_Key *ev);
static Eina_Bool _e_keyrouter_send_key_events_release(int type, Ecore_Event_Key *ev);
static void _e_keyrouter_send_key_event(int type, E_Client *ec, struct wl_client *wc, Ecore_Event_Key *ev);
static Eina_Bool _e_keyrouter_is_key_grabbed(int key);
static Eina_Bool _e_keyrouter_check_top_visible_window(E_Comp *c, E_Client *ec_focus, int arr_idx);
static void _e_keyrouter_query_tizen_key_table(void);

static Eina_Bool _e_keyrouter_client_cb_stack(void *data, int type, void *event);
static Eina_Bool _e_keyrouter_client_cb_remove(void *data, int type, void *event);
static void _e_keyrouter_wl_client_cb_destroy(struct wl_listener *l, void *data);

static int _e_keyrouter_keygrab_set(struct wl_client *client, struct wl_resource *surface, uint32_t key, uint32_t mode);
static int _e_keyrouter_keygrab_unset(struct wl_client *client, struct wl_resource *surface, uint32_t key);


static int
_e_keyrouter_keygrab_set(struct wl_client *client, struct wl_resource *surface, uint32_t key, uint32_t mode)
{
   int res=0;

   if (!surface)
     {
        /* Regarding topmost mode, a client must request to grab a key with a valid surface. */
        if (mode == TIZEN_KEYROUTER_MODE_TOPMOST)
          {
             KLDBG("Invalid surface for TOPMOST grab mode ! (key=%d, mode=%d)\n", key, mode);

             return TIZEN_KEYROUTER_ERROR_INVALID_SURFACE;
          }
        else
          {
             KLDBG("Null surface will be permitted for EXCLUSIVE, OR_EXCLUSIVE and SHARED !\n");
          }
     }

   /* Check the given key range */
   if (0 > key || MAX_HWKEYS < key )
     {
        KLDBG("Invalid range of key ! (keycode:%d)\n", key);
        return TIZEN_KEYROUTER_ERROR_INVALID_KEY;
     }

   /* Check whether the key can be grabbed or not !
    * Only key listed in Tizen key layout file can be grabbed. */
   if (0 == krt->HardKeys[key].keycode)
     {
        KLDBG("Invalid key ! Disabled to grab ! (keycode:%d)\n", key);
        return TIZEN_KEYROUTER_ERROR_INVALID_KEY;
     }

   /* Check whether the mode is valid or not */
   if (TIZEN_KEYROUTER_MODE_NONE > mode || TIZEN_KEYROUTER_MODE_EXCLUSIVE < mode)
     {
        KLDBG("Invalid range of mode ! (mode:%d)\n", mode);
        return  TIZEN_KEYROUTER_ERROR_INVALID_MODE;
     }

   /* Check whether the request key can be grabbed or not */
   res = e_keyrouter_set_keygrab_in_list(surface, client, key, mode);

   KLDBG("Result of grab check for a key (key:%d, mode:%d, res:%d)\n", key, mode, res);

   return res;
}

static int
_e_keyrouter_keygrab_unset(struct wl_client *client, struct wl_resource *surface, uint32_t key)
{
   E_Pixmap *ep = NULL;
   E_Client *ec = NULL;

   if (!surface)
     {
        /* EXCLUSIVE grab */
        e_keyrouter_find_and_remove_client_from_list(NULL, client, key, TIZEN_KEYROUTER_MODE_EXCLUSIVE);

        /* OVERRIDABLE_EXCLUSIVE grab */
        e_keyrouter_find_and_remove_client_from_list(NULL, client, key, TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE);

        /* TOPMOST(TOP_POSITION) grab */
        e_keyrouter_find_and_remove_client_from_list(NULL, client, key, TIZEN_KEYROUTER_MODE_TOPMOST);

        /* SHARED grab */
        e_keyrouter_find_and_remove_client_from_list(NULL, client, key, TIZEN_KEYROUTER_MODE_SHARED);

        return TIZEN_KEYROUTER_ERROR_NONE;
     }

    if (!surface || !(ep = wl_resource_get_user_data(surface)))
     {
        KLDBG("Surface or E_Pixman from the surface is invalid ! Return error !\n");
        return TIZEN_KEYROUTER_ERROR_INVALID_SURFACE;
     }

   if (!(ec = e_pixmap_client_get(ep)))
     {
        KLDBG("E_Client pointer from E_Pixman from surface is invalid ! Return error !\n");
        return TIZEN_KEYROUTER_ERROR_INVALID_SURFACE;
     }

   /* EXCLUSIVE grab */
   e_keyrouter_find_and_remove_client_from_list(ec, NULL, key, TIZEN_KEYROUTER_MODE_EXCLUSIVE);

   /* OVERRIDABLE_EXCLUSIVE grab */
   e_keyrouter_find_and_remove_client_from_list(ec, NULL, key, TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE);

   /* TOPMOST(TOP_POSITION) grab */
   e_keyrouter_find_and_remove_client_from_list(ec, NULL, key, TIZEN_KEYROUTER_MODE_TOPMOST);

   /* SHARED grab */
   e_keyrouter_find_and_remove_client_from_list(ec, NULL, key, TIZEN_KEYROUTER_MODE_SHARED);

   return TIZEN_KEYROUTER_ERROR_NONE;
}

/* tizen_keyrouter_set_keygrab request handler */
static void
_e_keyrouter_cb_keygrab_set(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, uint32_t key, uint32_t mode)
{
   (void) client;
   (void) resource;
   (void) surface;
   (void) key;
   (void) mode;
   int res = 0;

   KLDBG("Key grab request (key:%d, mode:%d)\n", key, mode);

   res = _e_keyrouter_keygrab_set(client, surface, key, mode);

   tizen_keyrouter_send_keygrab_notify(resource, surface, key, mode, res);
}

/* tizen_keyrouter unset_keygrab request handler */
static void
_e_keyrouter_cb_keygrab_unset(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, uint32_t key)
{
   (void) client;
   (void) resource;
   (void) surface;
   (void) key;
   int res = 0;

   KLDBG("Key ungrab request (key:%d)\n", key);

   res = _e_keyrouter_keygrab_unset(client, surface, key);

   tizen_keyrouter_send_keygrab_notify(resource, surface, key, TIZEN_KEYROUTER_MODE_NONE, res);
}

/* tizen_keyrouter get_keygrab_status request handler */
static void
_e_keyrouter_cb_get_keygrab_status(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, uint32_t key)
{
   (void) client;
   (void) resource;
   (void) surface;
   (void) key;

   if (!surface)
     {
        KLDBG("No surface !\n");

        /* Regarding exclusive mode, a client can request to grab a key without a surface.
         * TODO : Need to check the (grab) mode */
#if 0
        if (mode < TIZEN_KEYROUTER_MODE_EXCLUSIVE)
          {
             KLDBG("Invalid surface ! (key=%d, mode=%d)\n", key, mode);
             WL_KEYGRAB_NOTIFY_WITH_VAL(resource, surface, key, TIZEN_KEYROUTER_MODE_NONE, TIZEN_KEYROUTER_ERROR_INVALID_SURFACE);
             return;
          }
        else
          {
             KLDBG("Null surface will be permitted only for EXCLUSIVE mode !\n");
          }
#endif
     }

   /* TODO : Need to check key grab status for the requesting wl client */

   tizen_keyrouter_send_keygrab_notify(resource, surface, key, TIZEN_KEYROUTER_MODE_NONE, TIZEN_KEYROUTER_ERROR_NONE);
}

static void
_e_keyrouter_cb_keygrab_set_list(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, struct wl_array *grab_list)
{
   E_Keyrouter_Grab_List *grab_data = NULL;
   int res = TIZEN_KEYROUTER_ERROR_NONE;

   wl_array_for_each(grab_data, grab_list)
     {
        if (grab_data)
          {
             KLDBG("Grab request using list  [key: %d, mode: %d, res: %d]\n", grab_data->key, grab_data->mode, grab_data->err);
             res = _e_keyrouter_keygrab_set(client, surface, grab_data->key, grab_data->mode);
             grab_data->err = res;
          }
     }

   tizen_keyrouter_send_keygrab_notify_list(resource, surface, grab_list);
}

static void
_e_keyrouter_cb_keygrab_unset_list(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, struct wl_array *grab_list)
{
   E_Keyrouter_Grab_List *grab_data = NULL;
   int res = TIZEN_KEYROUTER_ERROR_NONE;

   wl_array_for_each(grab_data, grab_list)
     {
        if (grab_data)
          {
             KLDBG("Ungrab request using list  [key: %d, res: %d]\n", grab_data->key, grab_data->err);
             res = _e_keyrouter_keygrab_unset(client, surface, grab_data->key);
             grab_data->err = res;
          }
     }

   tizen_keyrouter_send_keygrab_notify_list(resource, surface, grab_list);
}


/* Function for registering wl_client destroy listener */
int
e_keyrouter_add_client_destroy_listener(struct wl_client *client)
{
   struct wl_listener *destroy_listener = NULL;
   Eina_List *l;
   struct wl_client *wc_data;

   EINA_LIST_FOREACH(krt->none_surface_grab_client, l, wc_data)
     {
        if (wc_data)
          {
             if (wc_data == client)
               {
                  KLDBG("client(%p)'s destroy listener is already added, wc_data(%p)\n", client, wc_data);
                  return TIZEN_KEYROUTER_ERROR_NONE;
               }
          }
     }

   destroy_listener = E_NEW(struct wl_listener, 1);

   if (!destroy_listener)
     {
        KLDBG("Failed to allocate memory for wl_client destroy listener !\n");
        return TIZEN_KEYROUTER_ERROR_NO_SYSTEM_RESOURCES;
     }

   destroy_listener->notify = _e_keyrouter_wl_client_cb_destroy;
   wl_client_add_destroy_listener(client, destroy_listener);
   krt->none_surface_grab_client = eina_list_append(krt->none_surface_grab_client, client);

   KLDBG("Add a wl_client(%p) destroy listener(%p)\n", client, destroy_listener);

   return TIZEN_KEYROUTER_ERROR_NONE;
}

static const struct tizen_keyrouter_interface _e_keyrouter_implementation = {
   _e_keyrouter_cb_keygrab_set,
   _e_keyrouter_cb_keygrab_unset,
   _e_keyrouter_cb_get_keygrab_status,
   _e_keyrouter_cb_keygrab_set_list,
   _e_keyrouter_cb_keygrab_unset_list
};

/* tizen_keyrouter global object destroy function */
static void
_e_keyrouter_cb_destory(struct wl_resource *resource)
{
   /* TODO : destroy resources if exist */
}

/* tizen_keyrouter global object bind function */
static void
_e_keyrouter_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   E_KeyrouterPtr krt_instance = data;
   struct wl_resource *resource;

   resource = wl_resource_create(client, &tizen_keyrouter_interface, MIN(version, 1), id);

   KLDBG("wl_resource_create(...,&tizen_keyrouter_interface,...)\n");

   if (!resource)
     {
        KLDBG("Failed to create resource ! (version :%d, id:%d)\n", version, id);
        wl_client_post_no_memory(client);
	 return;
     }

   wl_resource_set_implementation(resource, &_e_keyrouter_implementation, krt_instance, _e_keyrouter_cb_destory);

   KLDBG("wl_resource_set_implementation(..., _e_keyrouter_implementation, ...)\n");
}

static Eina_Bool
_e_keyrouter_is_key_grabbed(int key)
{
   if (!krt->HardKeys[key].keycode)
     {
        return EINA_FALSE;
     }
   if (!krt->HardKeys[key].excl_ptr &&
        !krt->HardKeys[key].or_excl_ptr &&
        !krt->HardKeys[key].top_ptr &&
        !krt->HardKeys[key].shared_ptr)
     {
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

/* Function for checking the existing grab for a key and sending key event(s) */
static Eina_Bool
_e_keyrouter_process_key_event(void *event, int type)
{
   Eina_Bool res = EINA_TRUE;
   Ecore_Event_Key *ev = event;

   if (!ev) return res;

   KLDBG("type=%s\n", (type == ECORE_EVENT_KEY_DOWN) ? "ECORE_EVENT_KEY_DOWN" : "ECORE_EVENT_KEY_UP");

   if (0 > ev->keycode || MAX_HWKEYS < ev->keycode)
     {
        KLDBG("The key(%d) is too larger to process keyrouting: Invalid keycode\n", ev->keycode);
        return res;
     }

   if ( (ECORE_EVENT_KEY_DOWN == type) && (!_e_keyrouter_is_key_grabbed(ev->keycode)) )
     {
        KLDBG("The press key(%d) isn't a grabbable key or has not been grabbed yet !\n", ev->keycode);
        return res;
     }

   if ( (ECORE_EVENT_KEY_UP == type) && (!krt->HardKeys[ev->keycode].press_ptr) )
     {
        KLDBG("The release key(%d) isn't a grabbable key or has not been grabbed yet !\n", ev->keycode);
        return res;
     }

   //KLDBG("The key(%d) is going to be sent to the proper wl client(s) !\n", ev->keycode);

  if (_e_keyrouter_send_key_events(type, ev))
    {
       res = EINA_FALSE;
       KLDBG("Key event(s) has/have been sent to wl client(s) !\n");
    }

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
   Eina_List *l = NULL;

   /* Deliver release  clean up pressed key list */
   EINA_LIST_FREE(krt->HardKeys[ev->keycode].press_ptr, key_node_data)
     {
        if (key_node_data)
          {
             _e_keyrouter_send_key_event(type, key_node_data->ec, key_node_data->wc, ev);
             KLDBG("Release Pair : Key %s(%d) ===> E_Client (%p) WL_Client (%p)\n",
                      ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keycode, key_node_data->ec, key_node_data->wc);
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
   E_Client *ec_focus = NULL;
   E_Comp *c = NULL;

   E_Keyrouter_Key_List_NodePtr key_node_data;
   Eina_List *l = NULL;

   EINA_LIST_FOREACH(krt->HardKeys[keycode].excl_ptr, l, key_node_data)
     {
        if (key_node_data)
          {
             _e_keyrouter_send_key_event(type, key_node_data->ec, key_node_data->wc, ev);
             KLDBG("EXCLUSIVE Mode : Key %s(%d) ===> E_Client (%p) WL_Client (%p)\n",
                      ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keycode, key_node_data->ec, key_node_data->wc);

             return EINA_TRUE;
          }
     }

   EINA_LIST_FOREACH(krt->HardKeys[keycode].or_excl_ptr, l, key_node_data)
     {
        if (key_node_data)
          {
             _e_keyrouter_send_key_event(type, key_node_data->ec, key_node_data->wc, ev);
             KLDBG("OVERRIDABLE_EXCLUSIVE Mode : Key %s(%d) ===> E_Client (%p) WL_Client (%p)\n",
                     ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keycode, key_node_data->ec, key_node_data->wc);

             return EINA_TRUE;
          }
     }

   ec_focus = e_client_focused_get();

   EINA_LIST_FOREACH(krt->HardKeys[keycode].top_ptr, l, key_node_data)
     {
        if (key_node_data)
          {
             if ((EINA_FALSE == krt->isWindowStackChanged) && (ec_focus == key_node_data->ec))
               {
                  _e_keyrouter_send_key_event(type, key_node_data->ec, NULL, ev);
                  KLDBG("TOPMOST (TOP_POSITION) Mode : Key %s (%d) ===> Client (%p)\n",
                           ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keycode, key_node_data->ec);

                  return EINA_TRUE;
               }
             krt->isWindowStackChanged = EINA_FALSE;

             c = e_comp_find_by_window(ev->window);
             if (_e_keyrouter_check_top_visible_window(c, ec_focus, keycode))
               {
                  _e_keyrouter_send_key_event(type, key_node_data->ec, NULL, ev);
                  KLDBG("TOPMOST (TOP_POSITION) Mode : Key %s (%d) ===> Client (%p)\n",
                        ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keycode,key_node_data->ec);

                  return EINA_TRUE;
               }
             break;
          }
     }

   if (krt->HardKeys[keycode].shared_ptr)
     {
        _e_keyrouter_send_key_event(type, ec_focus, NULL, ev);
        KLDBG("SHARED [Focus client] : Key %s (%d) ===> Client (%p)\n",
                 ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up "), ev->keycode, ec_focus);

        EINA_LIST_FOREACH(krt->HardKeys[keycode].shared_ptr, l, key_node_data)
          {
             if (key_node_data)
               {
                  if (key_node_data->ec)
                    {
                       if (key_node_data->ec != ec_focus)
                         {
                            _e_keyrouter_send_key_event(type, key_node_data->ec, key_node_data->wc, ev);
                            KLDBG("SHARED Mode : Key %s(%d) ===> E_Client (%p) WL_Client (%p)\n",
                                     ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keycode, key_node_data->ec, key_node_data->wc);
                         }
                    }
                  else
                    {
                       if (key_node_data->wc != wl_resource_get_client(ec_focus->comp_data->wl_surface))
                         {
                            _e_keyrouter_send_key_event(type, key_node_data->ec, key_node_data->wc, ev);
                            KLDBG("SHARED Mode : Key %s(%d) ===> E_Client (%p) WL_Client (%p)\n",
                                     ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keycode, key_node_data->ec, key_node_data->wc);
                         }
                    }
               }
          }

        return EINA_TRUE;
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_keyrouter_check_top_visible_window(E_Comp *c, E_Client *ec_focus, int arr_idx)
{
   E_Client *ec_top = NULL;
   Eina_List *l = NULL, *l_next = NULL;
   E_Keyrouter_Key_List_NodePtr key_node_data = NULL;

   ec_top = e_client_top_get(c);
   KLDBG("Top Client: %p\n", ec_top);

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
                  if (ec_top == key_node_data->ec)
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
        KLDBG("Next client: %p\n", ec_top);
     }
   return EINA_FALSE;
}

/* Function for sending key event to wl_client(s) */
static void
_e_keyrouter_send_key_event(int type, E_Client *ec, struct wl_client *wc, Ecore_Event_Key *ev)
{
   struct wl_client *wc_send;
   struct wl_resource *res;

   uint evtype;
   uint serial;
   Eina_List *l;

   if (ec == NULL)
     {
        wc_send = wc;
     }
   else
     {
        wc_send = wl_resource_get_client(ec->comp_data->wl_surface);
     }

   if (ECORE_EVENT_KEY_DOWN == type)
     {
        e_keyrouter_prepend_to_keylist(ec, wc, ev->keycode, TIZEN_KEYROUTER_MODE_PRESSED);
        evtype = WL_KEYBOARD_KEY_STATE_PRESSED;
     }
   else
     {
        evtype = WL_KEYBOARD_KEY_STATE_RELEASED;
     }

   serial = wl_display_next_serial(krt->cdata->wl.disp);
   EINA_LIST_FOREACH(krt->cdata->kbd.resources, l, res)
     {
        if (res)
          {
             if (wl_resource_get_client(res) != wc_send) continue;

             KLDBG("[time: %d] res: %p, serial: %d send a key(%d):%d to wl_client:%p\n", ev->timestamp, res, serial, (ev->keycode)-8, evtype, wc_send);
             wl_keyboard_send_key(res, serial, ev->timestamp, ev->keycode-8, evtype);
          }
     }
}


static Eina_Bool
_event_filter(void *data, void *loop_data EINA_UNUSED, int type, void *event)
{
   (void) data;
   (void) type;
   (void) event;

   /* Filter only for key down/up event */
   if (ECORE_EVENT_KEY_DOWN == type || ECORE_EVENT_KEY_UP == type)
     {
        return _e_keyrouter_process_key_event(event, type);
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_keyrouter_init()
{
   krt = E_NEW(E_Keyrouter, 1);

   if (!krt)
     {
        KLDBG("Failed to allocate memory for krt !\n");
        return EINA_FALSE;
     }

   if (!e_comp)
     {
        KLDBG("Failed to initialize keyrouter module ! (e_comp == NULL)\n");
        goto err;
     }

   E_Comp_Data *cdata = e_comp->wl_comp_data;

   if (!cdata)
     {
        KLDBG("Failed to get wl_comp_data ! (e_comp->wl_comp_data == NULL)\n");
        goto err;
     }

   krt->cdata = cdata;
   krt->global = wl_global_create(cdata->wl.disp, &tizen_keyrouter_interface, 1, krt, _e_keyrouter_cb_bind);

   if (!krt->global)
     {
        KLDBG("Failed to create global !\n");
        goto err;
     }

   /* Get keyname and keycode pair from Tizen Key Layout file */
   _e_keyrouter_query_tizen_key_table();

#if 0
   int i = 0;
   for (i=0 ; i < krt->numTizenHWKeys ; i++)
     {
        KLDBG("keycode[%d], keyname[%s] : Enabled to grab\n", krt->TizenHWKeys[i].keycode, krt->TizenHWKeys[i].name);
     }
#endif

   /* Add filtering mechanism */
   krt->ef_handler = ecore_event_filter_add(NULL, _event_filter, NULL, NULL);
   _e_keyrouter_init_handlers();

   return EINA_TRUE;

err:
   if (krt && krt->ef_handler) ecore_event_filter_del(krt->ef_handler);
   if (krt) E_FREE(krt);

   return EINA_FALSE;
}

EAPI void *
e_modapi_init(E_Module *m)
{
   return (_e_keyrouter_init() ? m : NULL);
}

EAPI int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
   _e_keyrouter_deinit_handlers();
   /* TODO: free allocated memory */

   return 1;
}

EAPI int
e_modapi_save(E_Module *m EINA_UNUSED)
{
   /* Save something to be kept */
   return 1;
}

/* Function for getting keyname/keycode information from a key layout file */
static void
_e_keyrouter_query_tizen_key_table(void)
{
   FILE *fp_key_tables = NULL;
   char keyname[64] = {0, };
   int key_count = 0;
   int key_size = 0;
   int keycode = 0;
   int i = 0;

   fp_key_tables = fopen(KEYLAYOUT_PATH, "r");

   if (!fp_key_tables)
     {
        KLDBG("Failed to read file (%s)\n", KEYLAYOUT_PATH);
        return;
     }

   //KLDBG("Support Tizen Keymap\n");
   while ( 0 < fscanf(fp_key_tables, "%s %d", keyname, &keycode))
     {
        key_count++;
        //KLDBG(" - [%s : %d]\n", keyname, keycode);
     }

   krt->TizenHWKeys = E_NEW(E_Keyrouter_Tizen_HWKey, key_count);
   krt->numTizenHWKeys = key_count;

   fseek(fp_key_tables, 0, SEEK_SET);

   for (i=0; i<key_count; i++)
     {
        if (fscanf(fp_key_tables, "%s %d", keyname, &keycode) <= 0) continue;

        key_size = sizeof(keyname);

        krt->TizenHWKeys[i].name = (char*)calloc(key_size, sizeof(char));

        if (!krt->TizenHWKeys[i].name)
          {
             KLDBG("Failed to allocate memory !\n");
             continue;
          }

        strncpy(krt->TizenHWKeys[i].name, keyname, key_size);

        krt->TizenHWKeys[i].keycode = keycode+8;
        krt->HardKeys[keycode+8].keycode = keycode+8;
     }

   fclose(fp_key_tables);
}

static void
_e_keyrouter_deinit_handlers(void)
{
   Ecore_Event_Handler *h = NULL;

   if (!krt ||  !krt->handlers) return;

   EINA_LIST_FREE(krt->handlers, h)
     ecore_event_handler_del(h);
}

static void
_e_keyrouter_init_handlers(void)
{
   E_LIST_HANDLER_APPEND(krt->handlers, E_EVENT_CLIENT_STACK, _e_keyrouter_client_cb_stack, NULL);
   E_LIST_HANDLER_APPEND(krt->handlers, E_EVENT_CLIENT_REMOVE, _e_keyrouter_client_cb_remove, NULL);
}

static Eina_Bool
_e_keyrouter_client_cb_stack(void *data, int type, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec = ev->ec;

   (void) data;
   (void) type;
   (void) event;
   (void) ev;
   (void) ec;

   //KLDBG("ec: %p, visibile: %d, focused: %d, take_focus: %d, want_focus: %d, bordername: %s, input_only: %d\n",
   //        ec, ec->visible, ec->focused, ec->take_focus, ec->want_focus, ec->bordername, ec->input_only);

   krt->isWindowStackChanged = EINA_TRUE;

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_keyrouter_client_cb_remove(void *data, int type, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec = ev->ec;

   (void) data;
   (void) type;
   (void) event;

   KLDBG("e_client: %p is died\n", ec);

   e_keyrouter_remove_client_from_list(ec, NULL);

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_keyrouter_wl_client_cb_destroy(struct wl_listener *l, void *data)
{
   struct wl_client *client = data;

   KLDBG("Listener(%p) called: wl_client: %p is died\n", l, client);
   e_keyrouter_remove_client_from_list(NULL, client);

   E_FREE(l);
   l = NULL;

   krt->none_surface_grab_client = eina_list_remove(krt->none_surface_grab_client, client);
}
