#define E_COMP_WL
#include "e_mod_main_wl.h"
#include <string.h>
#include <device/power.h>
#include <device/callback.h>
#include <device/display.h>

#define KRT_IPD_INPUT_CONFIG          444

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
static Eina_Bool _e_keyrouter_cb_idler(void *data);
static void _e_keyrouter_cb_power_change(device_callback_e type, void* value, void* user_data);
#ifdef ENABLE_CYNARA
static void _e_keyrouter_util_cynara_log(const char *func_name, int err);
static Eina_Bool _e_keyrouter_util_do_privilege_check(struct wl_client *client, int socket_fd, uint32_t mode, uint32_t keycode);

#define E_KEYROUTER_CYNARA_ERROR_CHECK_GOTO(func_name, ret, label) \
  do \
    { \
       if (EINA_UNLIKELY(CYNARA_API_SUCCESS != ret)) \
          { \
             _e_keyrouter_util_cynara_log(func_name, ret); \
             goto label; \
          } \
    } \
  while (0)
#endif

static int
_e_keyrouter_keygrab_set(struct wl_client *client, struct wl_resource *surface, uint32_t key, uint32_t mode)
{
   int res=0;

#ifdef ENABLE_CYNARA
   if (EINA_FALSE == _e_keyrouter_util_do_privilege_check(client,
                       wl_client_get_fd(client), mode, key))
     {
        return TIZEN_KEYROUTER_ERROR_NO_PERMISSION;
     }
#endif

   if (!surface)
     {
        /* Regarding topmost mode, a client must request to grab a key with a valid surface. */
        if (mode == TIZEN_KEYROUTER_MODE_TOPMOST ||
            mode == TIZEN_KEYROUTER_MODE_REGISTERED)
          {
             KLWRN("Invalid surface for %d grab mode ! (key=%d)\n", mode, key);

             return TIZEN_KEYROUTER_ERROR_INVALID_SURFACE;
          }
     }

   /* Check the given key range */
   if (krt->max_tizen_hwkeys < key)
     {
        KLWRN("Invalid range of key ! (keycode:%d)\n", key);
        return TIZEN_KEYROUTER_ERROR_INVALID_KEY;
     }

   /* Check whether the key can be grabbed or not !
    * Only key listed in Tizen key layout file can be grabbed. */
   if (0 == krt->HardKeys[key].keycode)
     {
        KLWRN("Invalid key ! Disabled to grab ! (keycode:%d)\n", key);
        return TIZEN_KEYROUTER_ERROR_INVALID_KEY;
     }

   /* Check whether the mode is valid or not */
   if (TIZEN_KEYROUTER_MODE_REGISTERED < mode)
     {
        KLWRN("Invalid range of mode ! (mode:%d)\n", mode);
        return  TIZEN_KEYROUTER_ERROR_INVALID_MODE;
     }

   /* Check whether the request key can be grabbed or not */
   res = e_keyrouter_set_keygrab_in_list(surface, client, key, mode);

   return res;
}

static int
_e_keyrouter_keygrab_unset(struct wl_client *client, struct wl_resource *surface, uint32_t key)
{
   /* Ungrab top position grabs first. This grab mode do not need privilege */
   if (!surface)
     e_keyrouter_find_and_remove_client_from_list(NULL, client, key, TIZEN_KEYROUTER_MODE_TOPMOST);
   else
     e_keyrouter_find_and_remove_client_from_list(surface, client, key, TIZEN_KEYROUTER_MODE_TOPMOST);

#ifdef ENABLE_CYNARA
   if (EINA_FALSE == _e_keyrouter_util_do_privilege_check(client,
                       wl_client_get_fd(client), TIZEN_KEYROUTER_MODE_NONE, key))
     {
        return TIZEN_KEYROUTER_ERROR_NONE;
     }
#endif

   if (!surface)
     {
        /* EXCLUSIVE grab */
        e_keyrouter_find_and_remove_client_from_list(NULL, client, key, TIZEN_KEYROUTER_MODE_EXCLUSIVE);

        /* OVERRIDABLE_EXCLUSIVE grab */
        e_keyrouter_find_and_remove_client_from_list(NULL, client, key, TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE);

        /* SHARED grab */
        e_keyrouter_find_and_remove_client_from_list(NULL, client, key, TIZEN_KEYROUTER_MODE_SHARED);

        return TIZEN_KEYROUTER_ERROR_NONE;
     }

   /* EXCLUSIVE grab */
   e_keyrouter_find_and_remove_client_from_list(surface, client, key, TIZEN_KEYROUTER_MODE_EXCLUSIVE);

   /* OVERRIDABLE_EXCLUSIVE grab */
   e_keyrouter_find_and_remove_client_from_list(surface, client, key, TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE);

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

   TRACE_INPUT_BEGIN(_e_keyrouter_cb_keygrab_set);
   KLINF("Key grab request (client: %p, surface: %p, pid: %d, key:%d, mode:%d)\n", client, surface, e_keyrouter_util_get_pid(client, surface), key, mode);

   res = _e_keyrouter_keygrab_set(client, surface, key, mode);

   TRACE_INPUT_END();
   tizen_keyrouter_send_keygrab_notify(resource, surface, key, mode, res);
}

/* tizen_keyrouter unset_keygrab request handler */
static void
_e_keyrouter_cb_keygrab_unset(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, uint32_t key)
{
   int res = 0;

   TRACE_INPUT_BEGIN(_e_keyrouter_cb_keygrab_unset);
   KLINF("Key ungrab request (client: %p, surface: %p, pid: %d, key:%d)\n", client, surface, e_keyrouter_util_get_pid(client, surface), key);

   res = _e_keyrouter_keygrab_unset(client, surface, key);

   TRACE_INPUT_END();
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

   TRACE_INPUT_BEGIN(_e_keyrouter_cb_get_keygrab_status);
   mode = e_keyrouter_find_key_in_list(surface, client, key);

   TRACE_INPUT_END();
   tizen_keyrouter_send_keygrab_notify(resource, surface, key, mode, TIZEN_KEYROUTER_ERROR_NONE);
}

static void
_e_keyrouter_cb_keygrab_set_list(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, struct wl_array *grab_list)
{
   E_Keyrouter_Grab_Request *grab_request = NULL;
   int res = TIZEN_KEYROUTER_ERROR_NONE;

   TRACE_INPUT_BEGIN(_e_keyrouter_cb_keygrab_set_list);

   if (0 != (_e_keyrouter_wl_array_length(grab_list) % 3))
     {
        /* FIX ME: Which way is effectively to notify invalid pair to client */
        KLWRN("Invalid keycode and grab mode pair. Check arguments in a list\n");
        TRACE_INPUT_END();
        tizen_keyrouter_send_keygrab_notify_list(resource, surface, NULL);
        return;
     }

   wl_array_for_each(grab_request, grab_list)
     {
        KLINF("Grab request using list  (client: %p, surface: %p, pid: %d, key: %d, mode: %d]\n", client, surface, e_keyrouter_util_get_pid(client, surface), grab_request->key, grab_request->mode);
        res = _e_keyrouter_keygrab_set(client, surface, grab_request->key, grab_request->mode);
        grab_request->err = res;
     }

   TRACE_INPUT_END();
   tizen_keyrouter_send_keygrab_notify_list(resource, surface, grab_list);
}

static void
_e_keyrouter_cb_keygrab_unset_list(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, struct wl_array *ungrab_list)
{
   E_Keyrouter_Ungrab_Request *ungrab_request = NULL;
   int res = TIZEN_KEYROUTER_ERROR_NONE;

   TRACE_INPUT_BEGIN(_e_keyrouter_cb_keygrab_unset_list);

   if (0 != (_e_keyrouter_wl_array_length(ungrab_list) % 2))
     {
        /* FIX ME: Which way is effectively to notify invalid pair to client */
        KLWRN("Invalid keycode and error pair. Check arguments in a list\n");
        TRACE_INPUT_END();
        tizen_keyrouter_send_keygrab_notify_list(resource, surface, ungrab_list);
        return;
     }

   wl_array_for_each(ungrab_request, ungrab_list)
     {
        KLINF("Ungrab request using list  (client: %p, surface: %p, pid: %d, key: %d, res: %d]\n", client, surface, e_keyrouter_util_get_pid(client, surface), ungrab_request->key, res);
        res = _e_keyrouter_keygrab_unset(client, surface, ungrab_request->key);
        ungrab_request->err = res;
     }

   TRACE_INPUT_END();
   tizen_keyrouter_send_keygrab_notify_list(resource, surface, ungrab_list);
}

static void
_e_keyrouter_cb_get_keyregister_status(struct wl_client *client, struct wl_resource *resource, uint32_t key)
{
   (void) client;
   (void) key;

   Eina_Bool res = EINA_FALSE;

   if (krt->isRegisterDelivery)
     {
        res = EINA_TRUE;
     }

   tizen_keyrouter_send_keyregister_notify(resource, (int)res);
}

static void
_e_keyrouter_cb_set_input_config(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, uint32_t config_mode, uint32_t value)
{
   Eina_Bool res = EINA_TRUE;
   KLINF("Given Surface(%p) for mode %d with value = %d \n", surface, config_mode, value);

   if (surface == NULL && config_mode != TIZEN_KEYROUTER_CONFIG_MODE_PICTURE_OFF)
     {
        KLWRN("Error Surface is NULL \n");
        res = EINA_FALSE;
        goto send_input_config_notify;
     }

   switch (config_mode)
     {
        case TIZEN_KEYROUTER_CONFIG_MODE_INVISIBLE_SET:
           if (value)
             {
                krt->invisible_set_window_list= eina_list_append(krt->invisible_set_window_list, surface);
             }
           else
             {
                krt->invisible_set_window_list= eina_list_remove(krt->invisible_set_window_list, surface);
             }
           break;

        case KRT_IPD_INPUT_CONFIG:
           krt->playback_daemon_surface = surface;
           KLINF("Registered playback daemon surface: %p",surface);
           break;

        case TIZEN_KEYROUTER_CONFIG_MODE_INVISIBLE_GET:
           if (value)
             {
                krt->invisible_get_window_list= eina_list_append(krt->invisible_get_window_list, surface);
             }
           else
             {
                krt->invisible_get_window_list= eina_list_remove(krt->invisible_get_window_list, surface);
             }
           break;

        case TIZEN_KEYROUTER_CONFIG_MODE_NUM_KEY_FOCUS:
            // to do ;
            break;

        case TIZEN_KEYROUTER_CONFIG_MODE_PICTURE_OFF:
            res = e_keyrouter_prepend_to_keylist(surface, surface ? NULL : client, value, TIZEN_KEYROUTER_MODE_PICTURE_OFF, EINA_FALSE);
            /* As surface/client destroy listener got added in e_keyrouter_prepend_to_keylist() function already */
            value = 0;
            break;

        default:
            KLWRN("Wrong mode requested: %d \n", config_mode);
            res= EINA_FALSE;
            goto send_input_config_notify;
     }

   if (value)
     {
        KLDBG("Add a surface(%p) destory listener\n", surface);
        e_keyrouter_add_surface_destroy_listener(surface);
     }
send_input_config_notify:
   tizen_keyrouter_send_set_input_config_notify(resource, (int)res);
}

/* tizen_keyrouter check if given surface in register none key list */
Eina_Bool
IsNoneKeyRegisterWindow(struct wl_resource *surface)
{
   struct wl_resource *surface_ldata = NULL;
   Eina_List *l = NULL, *l_next = NULL;

   EINA_LIST_FOREACH_SAFE (krt->registered_none_key_window_list, l, l_next, surface_ldata)
     {
        if (surface_ldata == surface)
          {
             KLDBG("Given surface(%p) is in NoneKeyRegisterWindow \n", surface);
             return EINA_TRUE;
          }
     }
   return EINA_FALSE;
}

/* tizen_keyrouter check if given surface in register invisible set list */
Eina_Bool
IsInvisibleSetWindow(struct wl_resource *surface)
{
   struct wl_resource *surface_ldata = NULL;
   Eina_List *l = NULL, *l_next = NULL;

   EINA_LIST_FOREACH_SAFE(krt->invisible_set_window_list, l, l_next, surface_ldata)
     {
        if (surface_ldata == surface)
          {
             return EINA_TRUE;
          }
     }
   return EINA_FALSE;
}

/* tizen_keyrouter check if given surface in register invisible get list */
Eina_Bool
IsInvisibleGetWindow(struct wl_resource *surface)
{
   struct wl_resource *surface_ldata = NULL;
   Eina_List *l = NULL, *l_next = NULL;

   EINA_LIST_FOREACH_SAFE(krt->invisible_get_window_list, l, l_next, surface_ldata)
     {
        if (surface_ldata == surface)
          {
             return EINA_TRUE;
          }
     }
   return EINA_FALSE;
}

static void
_e_keyrouter_cb_set_register_none_key(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, uint32_t data)
{
   (void) client;

   // Register None key set/get
   krt->register_none_key = data;
   if (krt->register_none_key)
     {
        krt->registered_none_key_window_list = eina_list_append(krt->registered_none_key_window_list, surface);
        if (surface)
          {
             e_keyrouter_add_surface_destroy_listener(surface);
             /* TODO: if failed add surface_destory_listener, remove keygrabs */
          }
     }
   else
     {
        krt->registered_none_key_window_list = eina_list_remove(krt->registered_none_key_window_list, surface);
     }

   KLDBG("Set Registered None Key called on surface (%p) with data (%d)\n", surface, krt->register_none_key);
   tizen_keyrouter_send_set_register_none_key_notify(resource, NULL, krt->register_none_key);
}

static void
_e_keyrouter_cb_keygrab_get_list(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface)
{
   E_Keyrouter_Key_List_NodePtr key_node_data = NULL;
   struct wl_array grab_result_list = {0,};
   E_Keyrouter_Grab_Request *grab_result = NULL;
   E_Keyrouter_Registered_Window_Info *rwin_info = NULL;
   Eina_List *l = NULL, *ll = NULL, *l_next = NULL;
   int *key_data;
   int i;

   wl_array_init(&grab_result_list);

   for (i = 0; i < krt->max_tizen_hwkeys; i++)
     {
        if (0 == krt->HardKeys[i].keycode) continue;

        EINA_LIST_FOREACH_SAFE(krt->HardKeys[i].excl_ptr, l, l_next, key_node_data)
          {
             if (surface == key_node_data->surface)
               {
                  grab_result = wl_array_add(&grab_result_list, sizeof(E_Keyrouter_Grab_Request));
                  if (grab_result)
                    {
                       grab_result->key = i;
                       grab_result->mode = TIZEN_KEYROUTER_MODE_EXCLUSIVE;
                       grab_result->err = TIZEN_KEYROUTER_ERROR_NONE;
                    }
               }
          }
        EINA_LIST_FOREACH_SAFE(krt->HardKeys[i].or_excl_ptr, l, l_next, key_node_data)
          {
             if (surface == key_node_data->surface)
               {
                  grab_result = wl_array_add(&grab_result_list, sizeof(E_Keyrouter_Grab_Request));
                  if (grab_result)
                    {
                       grab_result->key = i;
                       grab_result->mode = TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE;
                       grab_result->err = TIZEN_KEYROUTER_ERROR_NONE;
                    }
               }
          }
        EINA_LIST_FOREACH_SAFE(krt->HardKeys[i].top_ptr, l, l_next, key_node_data)
          {
             if (surface == key_node_data->surface)
               {
                  grab_result = wl_array_add(&grab_result_list, sizeof(E_Keyrouter_Grab_Request));
                  if (grab_result)
                    {
                       grab_result->key = i;
                       grab_result->mode = TIZEN_KEYROUTER_MODE_TOPMOST;
                       grab_result->err = TIZEN_KEYROUTER_ERROR_NONE;
                    }
               }
          }
        EINA_LIST_FOREACH_SAFE(krt->HardKeys[i].shared_ptr, l, l_next, key_node_data)
          {
             if (surface == key_node_data->surface)
               {
                  grab_result = wl_array_add(&grab_result_list, sizeof(E_Keyrouter_Grab_Request));
                  if (grab_result)
                    {
                       grab_result->key = i;
                       grab_result->mode = TIZEN_KEYROUTER_MODE_SHARED;
                       grab_result->err = TIZEN_KEYROUTER_ERROR_NONE;
                    }
               }
          }
     }
   // handle register mode here
   EINA_LIST_FOREACH(krt->registered_window_list, l, rwin_info)
     {
        if (rwin_info->surface == surface)
          {
             EINA_LIST_FOREACH(rwin_info->keys, ll, key_data)
               {
                  grab_result = wl_array_add(&grab_result_list, sizeof(E_Keyrouter_Grab_Request));
                  if (grab_result)
                    {
                       grab_result->key = *key_data;
                       grab_result->mode = TIZEN_KEYROUTER_MODE_REGISTERED;
                       grab_result->err = TIZEN_KEYROUTER_ERROR_NONE;
                    }
               }
          }
     }

   tizen_keyrouter_send_getgrab_notify_list(resource, surface, &grab_result_list);
   wl_array_release(&grab_result_list);
}

/* Function for registering wl_client destroy listener */
int
e_keyrouter_add_client_destroy_listener(struct wl_client *client)
{
   struct wl_listener *destroy_listener = NULL;
   Eina_List *l;
   struct wl_client *wc_data;

   if (!client) return TIZEN_KEYROUTER_ERROR_NONE;

   EINA_LIST_FOREACH(krt->grab_client_list, l, wc_data)
     {
        if (wc_data)
          {
             if (wc_data == client)
               {
                  return TIZEN_KEYROUTER_ERROR_NONE;
               }
          }
     }

   destroy_listener = E_NEW(struct wl_listener, 1);

   if (!destroy_listener)
     {
        KLERR("Failed to allocate memory for wl_client destroy listener !\n");
        return TIZEN_KEYROUTER_ERROR_NO_SYSTEM_RESOURCES;
     }

   destroy_listener->notify = _e_keyrouter_wl_client_cb_destroy;
   wl_client_add_destroy_listener(client, destroy_listener);
   krt->grab_client_list = eina_list_append(krt->grab_client_list, client);

   return TIZEN_KEYROUTER_ERROR_NONE;
}

/* Function for registering wl_surface destroy listener */
int
e_keyrouter_add_surface_destroy_listener(struct wl_resource *surface)
{
   struct wl_listener *destroy_listener = NULL;
   Eina_List *l;
   struct wl_resource *surface_data;

   if (!surface) return TIZEN_KEYROUTER_ERROR_NONE;

   EINA_LIST_FOREACH(krt->grab_surface_list, l, surface_data)
     {
        if (surface_data)
          {
             if (surface_data == surface)
               {
                  return TIZEN_KEYROUTER_ERROR_NONE;
               }
          }
     }

   destroy_listener = E_NEW(struct wl_listener, 1);

   if (!destroy_listener)
     {
        KLERR("Failed to allocate memory for wl_surface destroy listener !\n");
        return TIZEN_KEYROUTER_ERROR_NO_SYSTEM_RESOURCES;
     }

   destroy_listener->notify = _e_keyrouter_wl_surface_cb_destroy;
   wl_resource_add_destroy_listener(surface, destroy_listener);
   krt->grab_surface_list = eina_list_append(krt->grab_surface_list, surface);

   return TIZEN_KEYROUTER_ERROR_NONE;
}


static const struct tizen_keyrouter_interface _e_keyrouter_implementation = {
   _e_keyrouter_cb_keygrab_set,
   _e_keyrouter_cb_keygrab_unset,
   _e_keyrouter_cb_get_keygrab_status,
   _e_keyrouter_cb_keygrab_set_list,
   _e_keyrouter_cb_keygrab_unset_list,
   _e_keyrouter_cb_keygrab_get_list,
   _e_keyrouter_cb_set_register_none_key,
   _e_keyrouter_cb_get_keyregister_status,
   _e_keyrouter_cb_set_input_config
};

/* tizen_keyrouter global object destroy function */
static void
_e_keyrouter_cb_destory(struct wl_resource *resource)
{
   krt->resources = eina_list_remove(krt->resources, resource);
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
        KLERR("Failed to create resource ! (version :%d, id:%d)\n", version, id);
        wl_client_post_no_memory(client);
	 return;
     }

   krt->resources = eina_list_append(krt->resources, resource);

   wl_resource_set_implementation(resource, &_e_keyrouter_implementation, krt_instance, _e_keyrouter_cb_destory);
}

static void
_e_keyrouter_keygrab_status_print(FILE *log_fl, Eina_List *list)
{
   Eina_List *l;
   E_Keyrouter_Key_List_NodePtr kdata;
   int pid;
   char *cmd;

   EINA_LIST_FOREACH(list, l, kdata)
     {
        pid = e_keyrouter_util_get_pid(kdata->wc, kdata->surface);
        cmd = e_keyrouter_util_cmd_get_from_pid(pid);
        fprintf(log_fl, "                [surface: %p, client: %p, pid: %d(%s)]\n", kdata->surface, kdata->wc, pid, cmd);
        if (kdata->surface)
          {
             fprintf(log_fl, "                    -- Surface Information --\n");
             fprintf(log_fl, "                        = client: %p\n", wl_resource_get_client(kdata->surface));
             fprintf(log_fl, "                        = resource: %s(%d)\n", wl_resource_get_name(kdata->surface), wl_resource_get_id(kdata->surface));
          }
        else
          {
             fprintf(log_fl, "                    -- Client Information --\n");
             fprintf(log_fl, "                        = connected fd: %d\n", wl_client_get_fd(kdata->wc));
          }
     }
}


static void
_e_keyrouter_info_print(void *data, const char *log_path)
{
   char *keyname, *cmd;
   int  i, c, pid, *idata;
   FILE *log_fl;
   Eina_List *l, *ll;
   E_Keyrouter_Registered_Window_Info *rdata;

   log_fl = fopen(log_path, "a");
   if (!log_fl)
     {
        KLERR("failed: open file(%s)\n", log_path);
        return;
     }

   setvbuf(log_fl, NULL, _IOLBF, 512);

   fprintf(log_fl, "\n===== Keyrouter Information =====\n");
   fprintf(log_fl, "    ----- Grabbable Keys -----\n");
   for (i = 8; i < krt->max_tizen_hwkeys; i++)
     {
        if (!krt->HardKeys[i].keycode) continue;

        keyname = e_keyrouter_util_keyname_get_from_keycode(i);

        fprintf(log_fl, "         Key [%3d], Keyname: %s\n", i, keyname);

        free(keyname);
        keyname = NULL;
     }
   fprintf(log_fl, "    ----- End -----\n\n");

   fprintf(log_fl, "    ----- Register Window List -----\n");
   EINA_LIST_FOREACH(krt->registered_window_list, l, rdata)
     {
        pid = e_keyrouter_util_get_pid(NULL, rdata->surface);
        cmd = e_keyrouter_util_cmd_get_from_pid(pid);
        fprintf(log_fl, "        [ surface: %p, client: %p, pid: %d(%s) ]\n",
                         rdata->surface, wl_resource_get_client(rdata->surface), pid, cmd);
        free(cmd);
        cmd = NULL;
        c = 0;
        EINA_LIST_FOREACH(rdata->keys, ll, idata)
          {
             keyname = e_keyrouter_util_keyname_get_from_keycode(*idata);
             if (c == 0)
               fprintf(log_fl, "            registered key: Key [%3d], Keyname: %s\n", *idata, keyname);
             else
               fprintf(log_fl, "                            Key [%3d], Keyname: %s\n", *idata, keyname);
             c++;
             free(keyname);
             keyname = NULL;
          }
     }
   fprintf(log_fl, "    ----- End -----\n\n");

   fclose(log_fl);
   log_fl = NULL;
}

static void
_e_keyrouter_keygrab_print(void *data, const char *log_path)
{
   Eina_List *l;
   E_Keyrouter_Key_List_NodePtr kdata;
   E_Client *ec_focus;
   struct wl_resource *surface_focus;
   struct wl_client *wc_focus;
   int pid_focus, pid, i;
   char *cmd_focus, *cmd, *keyname;
   FILE *log_fl;

   (void) data;

   log_fl = fopen(log_path, "a");
   if (!log_fl)
     {
        KLERR("failed: open file(%s)\n", log_path);
        return;
     }

   setvbuf(log_fl, NULL, _IOLBF, 512);

   fprintf(log_fl, "\n===== Keygrab Status =====\n");

   ec_focus = e_client_focused_get();
   fprintf(log_fl, "    ----- Focus Window Info -----\n");
   if (ec_focus)
     {
        surface_focus = e_keyrouter_util_get_surface_from_eclient(ec_focus);
        wc_focus = wl_resource_get_client(surface_focus);
        pid_focus = e_keyrouter_util_get_pid(NULL, surface_focus);
        cmd_focus = e_keyrouter_util_cmd_get_from_pid(pid_focus);

        fprintf(log_fl, "        Focus Client: E_Client: %p\n", ec_focus);
        fprintf(log_fl, "                      Surface: %p, Client: %p\n", surface_focus, wc_focus);
        fprintf(log_fl, "                      pid: %d, cmd: %s\n", pid_focus, cmd_focus);
        free(cmd_focus);
     }
   else
     {
        fprintf(log_fl, "        No Focus Client\n");
     }
   fprintf(log_fl, "    ----- End -----\n\n");

   fprintf(log_fl, "    ----- Grabbed keys Info -----\n");
   for (i = 8; i < krt->max_tizen_hwkeys; i++)
     {
        if (!krt->HardKeys[i].keycode) continue;
        if (!krt->HardKeys[i].excl_ptr &&
            !krt->HardKeys[i].or_excl_ptr &&
            !krt->HardKeys[i].top_ptr &&
            !krt->HardKeys[i].shared_ptr &&
            !krt->HardKeys[i].registered_ptr)
          continue;

        keyname = e_keyrouter_util_keyname_get_from_keycode(i);

        fprintf(log_fl, "        [ Keycode: %d, Keyname: %s ]\n", i, keyname);

        free(keyname);
        keyname = NULL;

        if (krt->HardKeys[i].excl_ptr)
          {
             fprintf(log_fl, "            == Exclusive Grab ==\n");
             EINA_LIST_FOREACH(krt->HardKeys[i].excl_ptr, l, kdata)
               {
                  pid = e_keyrouter_util_get_pid(kdata->wc, kdata->surface);
                  cmd = e_keyrouter_util_cmd_get_from_pid(pid);
                  fprintf(log_fl, "                [surface: %p, client: %p, pid: %d(%s)]\n", kdata->surface, kdata->wc, pid, cmd);
                  free(cmd);
                  cmd = NULL;
                  if (kdata->surface)
                    {
                       fprintf(log_fl, "                    -- Surface Information --\n");
                       fprintf(log_fl, "                        = wl_client: %p\n", wl_resource_get_client(kdata->surface));
                       fprintf(log_fl, "                        = resource: %s(%d)\n", wl_resource_get_name(kdata->surface), wl_resource_get_id(kdata->surface));
                    }
                  else
                    {
                       fprintf(log_fl, "                    -- Client Information --\n");
                       fprintf(log_fl, "                        = connected fd: %d\n", wl_client_get_fd(kdata->wc));
                    }
                  break;
               }
            }

        if (krt->HardKeys[i].or_excl_ptr)
          {
             fprintf(log_fl, "            == Overidable Exclusive Grab ==\n");
             _e_keyrouter_keygrab_status_print(log_fl, krt->HardKeys[i].or_excl_ptr);
          }

        if (krt->HardKeys[i].top_ptr)
          {
             fprintf(log_fl, "            == Top Position Grab ==\n");
             _e_keyrouter_keygrab_status_print(log_fl, krt->HardKeys[i].top_ptr);
          }

        if (krt->HardKeys[i].shared_ptr)
          {
             fprintf(log_fl, "            == Shared Grab ==\n");
             _e_keyrouter_keygrab_status_print(log_fl, krt->HardKeys[i].shared_ptr);
          }

        fprintf(log_fl, "\n");
     }

   fprintf(log_fl, "    ----- End -----\n\n");

   fclose(log_fl);
   log_fl = NULL;
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
        if (ECORE_EVENT_KEY_DOWN == type)
          {
             TRACE_INPUT_BEGIN(event_filter:KEY_PRESS);
             TRACE_INPUT_END();
          }
        else if (ECORE_EVENT_KEY_UP == type)
          {
             TRACE_INPUT_BEGIN(event_filter:KEY_RELEASE);
             TRACE_INPUT_END();
          }
        return e_keyrouter_process_key_event(event, type);
     }

   return EINA_TRUE;
}

static void
_e_keyrouter_cb_power_change(device_callback_e type, void* value, void* user_data)
{
   if (type != DEVICE_CALLBACK_DISPLAY_STATE)
     {
        KLWRN("type is not DISPLAY_STATE");
        return;
     }

   display_state_e state = (display_state_e)value;
   switch (state)
     {
        case DISPLAY_STATE_SCREEN_OFF:
           krt->isPictureOffEnabled = 1;
           break;
        case DISPLAY_STATE_NORMAL:
           krt->isPictureOffEnabled = 0;
           break;
        case DISPLAY_STATE_SCREEN_DIM:
           krt->isPictureOffEnabled = 0;
           break;
        default:
           krt->isPictureOffEnabled = 0;
           break;
     }
   KLDBG("Picture off flag changed to %d \n", krt->isPictureOffEnabled);
}

static Eina_Bool _e_keyrouter_cb_idler(void *data)
{
    device_add_callback(DEVICE_CALLBACK_DISPLAY_STATE,_e_keyrouter_cb_power_change,NULL);
    return ECORE_CALLBACK_CANCEL;
}


static E_Keyrouter_Config_Data *
_e_keyrouter_init(E_Module *m)
{
   E_Keyrouter_Config_Data *kconfig = NULL;
   krt = E_NEW(E_Keyrouter, 1);
   Eina_Bool res = EINA_FALSE;
   int ret;

   TRACE_INPUT_BEGIN(_e_keyrouter_init);

   if (!krt)
     {
        KLERR("Failed to allocate memory for krt !\n");
        TRACE_INPUT_END();
        return NULL;
     }

   if (!e_comp)
     {
        KLERR("Failed to initialize keyrouter module ! (e_comp == NULL)\n");
        goto err;
     }

   kconfig = E_NEW(E_Keyrouter_Config_Data, 1);
   EINA_SAFETY_ON_NULL_GOTO(kconfig, err);

   kconfig->module = m;

   e_keyrouter_conf_init(kconfig);
   EINA_SAFETY_ON_NULL_GOTO(kconfig->conf, err);
   krt->conf = kconfig;

   e_keyrouter_key_combination_init();

   /* Get keyname and keycode pair from Tizen Key Layout file */
   res = _e_keyrouter_query_tizen_key_table();
   EINA_SAFETY_ON_FALSE_GOTO(res, err);

   /* Add filtering mechanism */
   krt->ef_handler = ecore_event_filter_add(NULL, _event_filter, NULL, NULL);
   //ecore handler add for power callback registration
   ecore_idle_enterer_add(_e_keyrouter_cb_idler, NULL);
   _e_keyrouter_init_handlers();

   krt->global = wl_global_create(e_comp_wl->wl.disp, &tizen_keyrouter_interface, 1, krt, _e_keyrouter_cb_bind);
   if (!krt->global)
     {
        KLERR("Failed to create global !\n");
        goto err;
     }

#ifdef ENABLE_CYNARA
   ret = cynara_initialize(&krt->p_cynara, NULL);
   if (EINA_UNLIKELY(CYNARA_API_SUCCESS != ret))
     {
        _e_keyrouter_util_cynara_log("cynara_initialize", ret);
        krt->p_cynara = NULL;
     }
#endif

   TRACE_INPUT_END();
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

   TRACE_INPUT_END();
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
   int i;
   Eina_List *l, *l_next;
   E_Keyrouter_Config_Data *kconfig = m->data;
   struct wl_resource *resource;
   struct wl_client *client;
   struct wl_listener *destroy_listener;

   e_keyrouter_conf_deinit(kconfig);
   E_FREE(kconfig);

   _e_keyrouter_deinit_handlers();

   for (i = 0; i < krt->max_tizen_hwkeys+1; i++)
     {
        if (krt->HardKeys[i].keyname)
          eina_stringshare_del(krt->HardKeys[i].keyname);
     }
   E_FREE(krt->HardKeys);

   EINA_LIST_FOREACH_SAFE(krt->grab_client_list, l, l_next, client)
     {
        destroy_listener = wl_client_get_destroy_listener(client, _e_keyrouter_wl_client_cb_destroy);
        if (destroy_listener)
          {
             wl_list_remove(&destroy_listener->link);
             E_FREE(destroy_listener);
          }
        krt->grab_client_list = eina_list_remove(krt->grab_client_list, client);
     }
   EINA_LIST_FOREACH_SAFE(krt->grab_surface_list, l, l_next, resource)
     {
        destroy_listener = wl_resource_get_destroy_listener(resource, _e_keyrouter_wl_surface_cb_destroy);
        if (destroy_listener)
          {
             wl_list_remove(&destroy_listener->link);
             E_FREE(destroy_listener);
          }
        krt->grab_surface_list = eina_list_remove(krt->grab_surface_list, client);
     }

   EINA_LIST_FREE(krt->registered_window_list, resource);

   EINA_LIST_FREE(krt->resources, resource)
     wl_resource_destroy(resource);

   wl_global_destroy(krt->global);

#ifdef ENABLE_CYNARA
   if (krt->p_cynara) cynara_finish(krt->p_cynara);
#endif

   E_FREE(krt);
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
   int res;
   struct xkb_rule_names names={0,};

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
             KLWRN("Given keycode(%d) is invalid. It must be bigger than zero, smaller than the maximum value(%d) or equal to it.\n", data->keycode, kconf->max_keycode);
             continue;
          }

        krt->HardKeys[data->keycode].keycode = data->keycode;
        krt->HardKeys[data->keycode].keyname = (char *)eina_stringshare_add(data->name);
        krt->HardKeys[data->keycode].no_privcheck = data->no_privcheck ? EINA_TRUE : EINA_FALSE;
        krt->HardKeys[data->keycode].repeat = data->repeat ? EINA_TRUE : EINA_FALSE;

        if (e_comp_wl_input_keymap_cache_file_use_get() == EINA_FALSE)
          {
             if (krt->HardKeys[data->keycode].repeat == EINA_FALSE)
               {
                  res = xkb_keymap_key_set_repeats(e_comp_wl->xkb.keymap, data->keycode, 0);
                  if (!res)
                    {
                       KLWRN("Failed to set repeat key(%d), value(%d)\n", data->keycode, 0);
                    }
               }
          }
     }

   if (e_comp_wl_input_keymap_cache_file_use_get() == EINA_FALSE)
     {
        KLINF("Server create a new cache file: %s\n", e_comp_wl_input_keymap_path_get(names));
        res = unlink(e_comp_wl_input_keymap_path_get(names));

        e_comp_wl_input_keymap_set(NULL, NULL, NULL, NULL, NULL, xkb_context_ref(e_comp_wl->xkb.context), e_comp_wl->xkb.keymap);
     }
   else
     KLINF("Currently cache file is exist. Do not change it.\n");

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

   e_info_server_hook_set("keyrouter", NULL, NULL);
   e_info_server_hook_set("keygrab", NULL, NULL);
}

static void
_e_keyrouter_init_handlers(void)
{
   E_LIST_HANDLER_APPEND(krt->handlers, E_EVENT_CLIENT_STACK, _e_keyrouter_client_cb_stack, NULL);
   E_LIST_HANDLER_APPEND(krt->handlers, E_EVENT_CLIENT_REMOVE, _e_keyrouter_client_cb_remove, NULL);

   e_info_server_hook_set("keyrouter", _e_keyrouter_info_print, NULL);
   e_info_server_hook_set("keygrab", _e_keyrouter_keygrab_print, NULL);
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

   wl_list_remove(&l->link);
   E_FREE(l);

   krt->grab_client_list = eina_list_remove(krt->grab_client_list, client);
}

static void
_e_keyrouter_wl_surface_cb_destroy(struct wl_listener *l, void *data)
{
   struct wl_resource *surface = (struct wl_resource *)data;

   KLDBG("Listener(%p) called: surface: %p is died\n", l, surface);
   e_keyrouter_remove_client_from_list(surface, NULL);

   wl_list_remove(&l->link);
   E_FREE(l);

   krt->grab_surface_list = eina_list_remove(krt->grab_surface_list, surface);
   krt->registered_none_key_window_list = eina_list_remove(krt->registered_none_key_window_list, surface);
   krt->invisible_set_window_list= eina_list_remove(krt->invisible_set_window_list, surface);
   krt->invisible_get_window_list= eina_list_remove(krt->invisible_get_window_list, surface);
   if (surface == krt->playback_daemon_surface)
     {
        krt->playback_daemon_surface = NULL;
        KLDBG("playback daemon surface destroyed!");
     }
}

#ifdef ENABLE_CYNARA
static void
_e_keyrouter_util_cynara_log(const char *func_name, int err)
{
#define CYNARA_BUFSIZE 128
   char buf[CYNARA_BUFSIZE] = "\0";
   int ret;

   ret = cynara_strerror(err, buf, CYNARA_BUFSIZE);
   if (ret != CYNARA_API_SUCCESS)
     {
        KLWRN("Failed to cynara_strerror: %d (error log about %s: %d)\n", ret, func_name, err);
        return;
     }
   KLWRN("%s is failed: %s\n", func_name, buf);
}

static Eina_Bool
_e_keyrouter_util_do_privilege_check(struct wl_client *client, int socket_fd, uint32_t mode, uint32_t keycode)
{
   int ret, pid, retry_cnt=0;
   char *clientSmack=NULL, *uid=NULL, *client_session=NULL;
   Eina_Bool res = EINA_FALSE;
   Eina_List *l;
   struct wl_client *wc_data;
   static Eina_Bool retried = EINA_FALSE;

   /* Top position grab is always allowed. This mode do not need privilege.*/
   if (mode == TIZEN_KEYROUTER_MODE_TOPMOST)
     return EINA_TRUE;

   if (krt->HardKeys[keycode].no_privcheck == EINA_TRUE)
     return EINA_TRUE;

   /* If initialize cynara is failed, allow keygrabs regardless of the previlege permition. */
   if (krt->p_cynara == NULL)
     {
        if (retried == EINA_FALSE)
          {
             retried = EINA_TRUE;
             for(retry_cnt = 0; retry_cnt < 5; retry_cnt++)
               {
                  KLDBG("Retry cynara initialize: %d\n", retry_cnt+1);
                  ret = cynara_initialize(&krt->p_cynara, NULL);
                  if (EINA_UNLIKELY(CYNARA_API_SUCCESS != ret))
                    {
                      _e_keyrouter_util_cynara_log("cynara_initialize", ret);
                       krt->p_cynara = NULL;
                    }
                  else
                    {
                       KLDBG("Success cynara initialize to try %d times\n", retry_cnt+1);
                       break;
                    }
               }
          }
        return EINA_TRUE;
     }

   EINA_LIST_FOREACH(krt->grab_client_list, l, wc_data)
     {
        if (wc_data == client)
          {
             res = EINA_TRUE;
             goto finish;
          }
     }

   ret = cynara_creds_socket_get_client(socket_fd, CLIENT_METHOD_SMACK, &clientSmack);
   E_KEYROUTER_CYNARA_ERROR_CHECK_GOTO("cynara_creds_socket_get_client", ret, finish);

   ret = cynara_creds_socket_get_user(socket_fd, USER_METHOD_UID, &uid);
   E_KEYROUTER_CYNARA_ERROR_CHECK_GOTO("cynara_creds_socket_get_user", ret, finish);

   ret = cynara_creds_socket_get_pid(socket_fd, &pid);
   E_KEYROUTER_CYNARA_ERROR_CHECK_GOTO("cynara_creds_socket_get_pid", ret, finish);

   client_session = cynara_session_from_pid(pid);

   ret = cynara_check(krt->p_cynara, clientSmack, client_session, uid, "http://tizen.org/privilege/keygrab");
   if (CYNARA_API_ACCESS_ALLOWED == ret)
     {
        res = EINA_TRUE;
        e_keyrouter_add_client_destroy_listener(client);
     }

finish:
   E_FREE(client_session);
   E_FREE(clientSmack);
   E_FREE(uid);

   return res;
}
#endif
