#define E_COMP_WL
#include "e.h"
#include "e_mod_main_wl.h"
#include <string.h>

E_KeyrouterPtr krt = NULL;
E_API E_Module_Api e_modapi = { E_MODULE_API_VERSION, "Keyrouter Module of Window Manager" };

static E_Keyrouter_Config_Data *_e_keyrouter_init(E_Module *m);
static void _e_keyrouter_init_handlers(void);
static void _e_keyrouter_deinit_handlers(void);

static Eina_Bool _e_keyrouter_query_tizen_key_table(void);
static int _e_keyrouter_wl_array_length(const struct wl_array *array);

static Eina_Bool _e_keyrouter_client_cb_stack(void *data, int type, void *event);
static Eina_Bool _e_keyrouter_client_cb_remove(void *data, int type, void *event);
static void _e_keyrouter_wl_client_cb_destroy(struct wl_listener *l, void *data);
static void _e_keyrouter_wl_surface_cb_destroy(struct wl_listener *l, void *data);

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
   if (krt->max_tizen_hwkeys < key)
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
   if (TIZEN_KEYROUTER_MODE_REGISTERED < mode)
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

   /* EXCLUSIVE grab */
   e_keyrouter_find_and_remove_client_from_list(surface, client, key, TIZEN_KEYROUTER_MODE_EXCLUSIVE);

   /* OVERRIDABLE_EXCLUSIVE grab */
   e_keyrouter_find_and_remove_client_from_list(surface, client, key, TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE);

   /* TOPMOST(TOP_POSITION) grab */
   e_keyrouter_find_and_remove_client_from_list(surface, client, key, TIZEN_KEYROUTER_MODE_TOPMOST);

   /* SHARED grab */
   e_keyrouter_find_and_remove_client_from_list(surface, client, key, TIZEN_KEYROUTER_MODE_SHARED);

   /* REGISTERED grab */
   e_keyrouter_unset_keyregister(surface, client, key);

   return TIZEN_KEYROUTER_ERROR_NONE;
}

/* tizen_keyrouter_set_keygrab request handler */
static void
_e_keyrouter_cb_keygrab_set(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, uint32_t key, uint32_t mode)
{
   int res = 0;

   KLDBG("Key grab request (key:%d, mode:%d)\n", key, mode);

   res = _e_keyrouter_keygrab_set(client, surface, key, mode);

   tizen_keyrouter_send_keygrab_notify(resource, surface, key, mode, res);
}

/* tizen_keyrouter unset_keygrab request handler */
static void
_e_keyrouter_cb_keygrab_unset(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, uint32_t key)
{
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
   int mode = TIZEN_KEYROUTER_MODE_NONE;

   mode = e_keyrouter_find_key_in_list(surface, client, key);

   tizen_keyrouter_send_keygrab_notify(resource, surface, key, mode, TIZEN_KEYROUTER_ERROR_NONE);
}

static void
_e_keyrouter_cb_keygrab_set_list(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, struct wl_array *grab_list)
{
   struct wl_array grab_result_list = {0,};
   E_Keyrouter_Grab_Result *grab_result = NULL;
   E_Keyrouter_Grab_Request *grab_request = NULL;
   int res = TIZEN_KEYROUTER_ERROR_NONE;

   wl_array_init(&grab_result_list);

   if (0 != (_e_keyrouter_wl_array_length(grab_list) % 2))
     {
        /* FIX ME: Which way is effectively to notify invalid pair to client */
        KLDBG("Invalid keycode and grab mode pair. Check arguments in a list\n");
        grab_result = wl_array_add(&grab_result_list, sizeof(E_Keyrouter_Grab_Result));
        grab_result->request_data.key = 0;
        grab_result->request_data.mode = 0;
        grab_result->err = TIZEN_KEYROUTER_ERROR_INVALID_ARRAY;
        goto send_notify;
     }

   wl_array_for_each(grab_request, grab_list)
     {
        res = _e_keyrouter_keygrab_set(client, surface, grab_request->key, grab_request->mode);
        KLDBG("Grab request using list  [key: %d, mode: %d, res: %d]\n", grab_request->key, grab_request->mode, res);
        grab_result = wl_array_add(&grab_result_list, sizeof(E_Keyrouter_Grab_Result));
        if (grab_result)
          {
             grab_result->request_data.key = grab_request->key;
             grab_result->request_data.mode = grab_request->mode;
             grab_result->err = res;
          }
     }

send_notify:
   tizen_keyrouter_send_keygrab_notify_list(resource, surface, &grab_result_list);
   wl_array_release(&grab_result_list);
}

static void
_e_keyrouter_cb_keygrab_unset_list(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, struct wl_array *ungrab_list)
{
   struct wl_array grab_result_list = {0,};
   E_Keyrouter_Grab_Result *grab_result = NULL;
   int *ungrab_request = NULL;
   int res = TIZEN_KEYROUTER_ERROR_NONE;

   wl_array_init(&grab_result_list);

   wl_array_for_each(ungrab_request, ungrab_list)
     {
        res = _e_keyrouter_keygrab_unset(client, surface, *ungrab_request);
        KLDBG("Ungrab request using list  [key: %d, res: %d]\n", *ungrab_request, res);
        grab_result = wl_array_add(&grab_result_list, sizeof(E_Keyrouter_Grab_Result));
        if (grab_result)
          {
             grab_result->request_data.key = *ungrab_request;
             grab_result->request_data.mode = TIZEN_KEYROUTER_MODE_NONE;
             grab_result->err = res;
          }
     }

   tizen_keyrouter_send_keygrab_notify_list(resource, surface, &grab_result_list);
   wl_array_release(&grab_result_list);
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

/* Function for registering wl_surface destroy listener */
int
e_keyrouter_add_surface_destroy_listener(struct wl_resource *surface)
{
   struct wl_listener *destroy_listener = NULL;
   Eina_List *l;
   struct wl_resource *surface_data;

   EINA_LIST_FOREACH(krt->surface_grab_client, l, surface_data)
     {
        if (surface_data)
          {
             if (surface_data == surface)
               {
                  KLDBG("surface(%p)'s destroy listener is already added, wc_data(%p)\n", surface, surface_data);
                  return TIZEN_KEYROUTER_ERROR_NONE;
               }
          }
     }

   destroy_listener = E_NEW(struct wl_listener, 1);

   if (!destroy_listener)
     {
        KLDBG("Failed to allocate memory for wl_surface destroy listener !\n");
        return TIZEN_KEYROUTER_ERROR_NO_SYSTEM_RESOURCES;
     }

   destroy_listener->notify = _e_keyrouter_wl_surface_cb_destroy;
   wl_resource_add_destroy_listener(surface, destroy_listener);
   krt->surface_grab_client = eina_list_append(krt->surface_grab_client, surface);

   KLDBG("Add a surface(%p) destroy listener(%p)\n", surface, destroy_listener);

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
_event_filter(void *data, void *loop_data EINA_UNUSED, int type, void *event)
{
   (void) data;
   (void) type;
   (void) event;

   /* Filter only for key down/up event */
   if (ECORE_EVENT_KEY_DOWN == type || ECORE_EVENT_KEY_UP == type)
     {
        return e_keyrouter_process_key_event(event, type);
     }

   return EINA_TRUE;
}

static E_Keyrouter_Config_Data *
_e_keyrouter_init(E_Module *m)
{
   E_Keyrouter_Config_Data *kconfig = NULL;
   krt = E_NEW(E_Keyrouter, 1);
   Eina_Bool res = EINA_FALSE;

   if (!krt)
     {
        KLDBG("Failed to allocate memory for krt !\n");
        return NULL;
     }

   if (!e_comp)
     {
        KLDBG("Failed to initialize keyrouter module ! (e_comp == NULL)\n");
        goto err;
     }

   kconfig = E_NEW(E_Keyrouter_Config_Data, 1);
   EINA_SAFETY_ON_NULL_GOTO(kconfig, err);

   kconfig->module = m;

   e_keyrouter_conf_init(kconfig);
   EINA_SAFETY_ON_NULL_GOTO(kconfig->conf, err);
   krt->conf = kconfig;

   /* Get keyname and keycode pair from Tizen Key Layout file */
   res = _e_keyrouter_query_tizen_key_table();
   EINA_SAFETY_ON_FALSE_GOTO(res, err);

   /* Add filtering mechanism */
   krt->ef_handler = ecore_event_filter_add(NULL, _event_filter, NULL, NULL);
   _e_keyrouter_init_handlers();

   krt->global = wl_global_create(e_comp_wl->wl.disp, &tizen_keyrouter_interface, 1, krt, _e_keyrouter_cb_bind);
   if (!krt->global)
     {
        KLDBG("Failed to create global !\n");
        goto err;
     }

   return kconfig;

err:
   if (kconfig)
     {
        e_keyrouter_conf_deinit(kconfig);
        E_FREE(kconfig);
     }
   _e_keyrouter_deinit_handlers();
   if (krt && krt->ef_handler) ecore_event_filter_del(krt->ef_handler);
   if (krt) E_FREE(krt);

   return NULL;
}

E_API void *
e_modapi_init(E_Module *m)
{
   return _e_keyrouter_init(m);
}

E_API int
e_modapi_shutdown(E_Module *m)
{
   E_Keyrouter_Config_Data *kconfig = m->data;
   e_keyrouter_conf_deinit(kconfig);
   _e_keyrouter_deinit_handlers();
   /* TODO: free allocated memory */

   return 1;
}

E_API int
e_modapi_save(E_Module *m)
{
   /* Save something to be kept */
   E_Keyrouter_Config_Data *kconfig = m->data;
   e_config_domain_save("module.keyrouter",
                        kconfig->conf_edd,
                        kconfig->conf);

   return 1;
}

/* Function for getting keyname/keycode information from a key layout file */
static Eina_Bool
_e_keyrouter_query_tizen_key_table(void)
{
   E_Keyrouter_Conf_Edd *kconf = krt->conf->conf;
   Eina_List *l;
   E_Keyrouter_Tizen_HWKey *data;

   /* TODO: Make struct in HardKeys to pointer.
                  If a key is defined, allocate memory to pointer,
                  that makes to save unnecessary memory */
   krt->HardKeys = E_NEW(E_Keyrouter_Grabbed_Key, kconf->max_keycode + 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(krt->HardKeys, EINA_FALSE);

   krt->numTizenHWKeys = kconf->num_keycode;
   krt->max_tizen_hwkeys = kconf->max_keycode;

   EINA_LIST_FOREACH(kconf->KeyList, l, data)
     {
        if (!data) continue;

        if (0 > data->keycode || krt->max_tizen_hwkeys < data->keycode)
          {
             KLDBG("[ERR] Given keycode(%d) is invalid. It must be bigger than zero, smaller than the maximum value(%d) or equal to it.\n", data->keycode, kconf->max_keycode);
             continue;
          }

        krt->HardKeys[data->keycode].keycode = data->keycode;
        krt->HardKeys[data->keycode].keyname = eina_stringshare_add(data->name);
     }
   return EINA_TRUE;
}

static int
_e_keyrouter_wl_array_length(const struct wl_array *array)
{
   int *data = NULL;
   int count = 0;

   wl_array_for_each(data, array)
     {
        count++;
     }

   return count;
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
   e_keyrouter_clear_registered_window();

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_keyrouter_client_cb_remove(void *data, int type, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec = ev->ec;

   (void) data;
   (void) type;
   (void) ev;
   (void) ec;

   /* FIXME: Remove this callback or do something others.
    *             It was moved to _e_keyrouter_wl_surface_cb_destroy() where it had here.
    */

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

static void
_e_keyrouter_wl_surface_cb_destroy(struct wl_listener *l, void *data)
{
   struct wl_resource *surface = data;

   KLDBG("Listener(%p) called: surface: %p is died\n", l, surface);
   e_keyrouter_remove_client_from_list(surface, NULL);

   E_FREE(l);
   l = NULL;

   krt->surface_grab_client = eina_list_remove(krt->surface_grab_client, surface);
}
