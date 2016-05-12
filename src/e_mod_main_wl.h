#ifndef E_MOD_MAIN_H
#define E_MOD_MAIN_H

#include <e.h>
#include <tizen-extension-server-protocol.h>
#ifdef ENABLE_CYNARA
#include <cynara-session.h>
#include <cynara-client.h>
#include <cynara-creds-socket.h>
#endif
#include <dbus/dbus-glib.h>
#include <dbus/dbus.h>

#ifdef TRACE_INPUT_BEGIN
#undef TRACE_INPUT_BEGIN
#endif
#ifdef TRACE_INPUT_END
#undef TRACE_INPUT_END
#endif

#ifdef ENABLE_TTRACE
#include <ttrace.h>

#define TRACE_INPUT_BEGIN(NAME) traceBegin(TTRACE_TAG_INPUT, "INPUT:KRT:"#NAME)
#define TRACE_INPUT_END() traceEnd(TTRACE_TAG_INPUT)
#else
#define TRACE_INPUT_BEGIN(NAME)
#define TRACE_INPUT_END()
#endif

/* Temporary value of maximum number of HWKeys */

#define CHECK_ERR(val) if (TIZEN_KEYROUTER_ERROR_NONE != val) return;
#define CHECK_ERR_VAL(val) if (TIZEN_KEYROUTER_ERROR_NONE != val) return val;
#define CHECK_NULL(val) if (!val) return;
#define CHECK_NULL_VAL(val) if (!val) return val;

#define KLERR(msg, ARG...) ERR("[tizen_keyrouter][%s:%d] "msg, __FUNCTION__, __LINE__, ##ARG)
#define KLWRN(msg, ARG...) WRN("[tizen_keyrouter][%s:%d] "msg, __FUNCTION__, __LINE__, ##ARG)
#define KLINF(msg, ARG...) INF("[tizen_keyrouter][%s:%d] "msg, __FUNCTION__, __LINE__, ##ARG)
#define KLDBG(msg, ARG...) DBG("[tizen_keyrouter][%s:%d] "msg, __FUNCTION__, __LINE__, ##ARG)

typedef struct _E_Keyrouter E_Keyrouter;
typedef struct _E_Keyrouter* E_KeyrouterPtr;
typedef struct _E_Keyrouter_Key_List_Node E_Keyrouter_Key_List_Node;
typedef struct _E_Keyrouter_Key_List_Node* E_Keyrouter_Key_List_NodePtr;
typedef struct _E_Keyrouter_Tizen_HWKey E_Keyrouter_Tizen_HWKey;
typedef struct _E_Keyrouter_Grabbed_Key E_Keyrouter_Grabbed_Key;
typedef struct _E_Keyrouter_Grab_Request E_Keyrouter_Grab_Request;
typedef struct _E_Keyrouter_Grab_Result E_Keyrouter_Grab_Result;
typedef struct _E_Keyrouter_Registered_Window_Info E_Keyrouter_Registered_Window_Info;

typedef struct _E_Keyrouter_Conf_Edd E_Keyrouter_Conf_Edd;
typedef struct _E_Keyrouter_Config_Data E_Keyrouter_Config_Data;
typedef struct _E_Keyrouter_Conf_Edd E_Keyrouter_Conf_Edd;
typedef struct _E_Keyrouter_Modkey E_Keyrouter_Modkey;
typedef struct _E_Keyrouter_Modkey_Name E_Keyrouter_Modkey_Name;
typedef struct _E_Keyrouter_Modkey_Data E_Keyrouter_Modkey_Data;
typedef struct _E_Keyrouter_Stepkey_Data E_Keyrouter_Stepkey_Data;

typedef struct _E_Keyrouter_Dbus E_Keyrouter_Dbus;

#define TIZEN_KEYROUTER_MODE_PRESSED        TIZEN_KEYROUTER_MODE_REGISTERED+1

extern E_KeyrouterPtr krt;

#define MAX_LEN 64
#define DBUS_PATH "/com/burtonini/dbus/ping"
#define DBUS_IFACE "keyrouter.dbus.Signal"
#define DBUS_MSG_NAME "KEY_COMBINATION"
#define COMBINATION_TIME_OUT 4000

struct _E_Keyrouter_Dbus
{
   char path[MAX_LEN];
   char interface[MAX_LEN];
   char msg[MAX_LEN];
   DBusError error;
   DBusConnection * conn;
};

struct _E_Keyrouter_Modkey_Data
{
   int *keycode;
   int num_keys;
   Eina_Bool press_only;
   unsigned int pressed;
};

struct _E_Keyrouter_Stepkey_Data
{
   int *keycode;
   int num_keys;
   int idx;
   int action;
};

struct _E_Keyrouter_Modkey_Name
{
   char *name;
};

struct _E_Keyrouter_Modkey
{
   Eina_List *ModKeys;
   int num_modkeys;
   Eina_Bool combination;
   Eina_Bool press_only;
   int action;
};

struct _E_Keyrouter_Conf_Edd
{
   int num_keycode;
   int max_keycode;
   Eina_List *KeyList;
   Eina_List *ModifierList;
   int num_modifier_keys;
};

struct _E_Keyrouter_Config_Data
{
   E_Module *module;
   E_Config_DD *conf_edd;
   E_Config_DD *conf_hwkeys_edd;
   E_Config_DD *conf_modkeys_edd;
   E_Config_DD *conf_modkeys_list_edd;
   E_Keyrouter_Conf_Edd *conf;
};

struct _E_Keyrouter_Registered_Window_Info
{
   struct wl_resource *surface;
   Eina_List *keys;
};

struct _E_Keyrouter_Key_List_Node
{
   struct wl_resource *surface;
   struct wl_client *wc;
   Eina_Bool focused;
};

struct _E_Keyrouter_Tizen_HWKey
{
   char *name;
   int keycode;
   int no_privcheck;
   int repeat;
};

struct _E_Keyrouter_Grabbed_Key
{
   int keycode;
   char* keyname;
   Eina_Bool no_privcheck;
   Eina_Bool repeat;

   Eina_List *excl_ptr;
   Eina_List *or_excl_ptr;
   Eina_List *top_ptr;
   Eina_List *shared_ptr;
   Eina_List *press_ptr;
   E_Keyrouter_Key_List_Node *registered_ptr;
};

struct _E_Keyrouter
{
   struct wl_global *global;
   Ecore_Event_Filter *ef_handler;
   Eina_List *handlers;

   E_Keyrouter_Config_Data *conf;

   E_Keyrouter_Grabbed_Key *HardKeys;
   Eina_List *grab_surface_list;
   Eina_List *grab_client_list;

   Eina_List *registered_window_list;

   Eina_Bool isWindowStackChanged;
   int numTizenHWKeys;
   int max_tizen_hwkeys;
#ifdef ENABLE_CYNARA
   cynara *p_cynara;
#endif
   Eina_List *modkey_list;
   Ecore_Timer *modkey_timer;
   double modkey_delay;
   double modkey_duration;

   Eina_List *stepkey_list;
   Ecore_Timer *stepkey_timer;
   double stepkey_delay;

   E_Keyrouter_Dbus dbus;
};

struct _E_Keyrouter_Grab_Request {
   int key;
   int mode;
};

struct _E_Keyrouter_Grab_Result {
   E_Keyrouter_Grab_Request request_data;
   int err;
};


/* E Module */
E_API extern E_Module_Api e_modapi;
E_API void *e_modapi_init(E_Module *m);
E_API int   e_modapi_shutdown(E_Module *m);
E_API int   e_modapi_save(E_Module *m);

int e_keyrouter_set_keygrab_in_list(struct wl_resource *surface, struct wl_client *client, uint32_t key, uint32_t mode);
int e_keyrouter_prepend_to_keylist(struct wl_resource *surface, struct wl_client *wc, uint32_t key, uint32_t mode, Eina_Bool focused);
void e_keyrouter_find_and_remove_client_from_list(struct wl_resource *surface, struct wl_client *wc, uint32_t key, uint32_t mode);
void e_keyrouter_remove_client_from_list(struct wl_resource *surface, struct wl_client *wc);
int e_keyrouter_find_key_in_list(struct wl_resource *surface, struct wl_client *wc, uint32_t key);

int e_keyrouter_add_client_destroy_listener(struct wl_client *client);
int e_keyrouter_add_surface_destroy_listener(struct wl_resource *surface);

Eina_Bool e_keyrouter_process_key_event(void *event, int type);

int e_keyrouter_set_keyregister(struct wl_client *client, struct wl_resource *surface, uint32_t key);
int e_keyrouter_unset_keyregister(struct wl_resource *surface, struct wl_client *client, uint32_t key);
Eina_Bool e_keyrouter_is_registered_window(struct wl_resource *surface);
void e_keyrouter_clear_registered_window(void);

void e_keyrouter_conf_init(E_Keyrouter_Config_Data *kconfig);
void e_keyrouter_conf_deinit(E_Keyrouter_Config_Data *kconfig);

void e_keyrouter_modkey_init(void);
void e_keyrouter_modkey_check(Ecore_Event_Key *ev);
void e_keyrouter_stepkey_check(Ecore_Event_Key *ev);
void e_keyrouter_modkey_mod_clean_up(void);

struct wl_resource *e_keyrouter_util_get_surface_from_eclient(E_Client *client);
int e_keyrouter_util_get_pid(struct wl_client *client, struct wl_resource *surface);
int e_keyrouter_util_keycode_get_from_string(char *name);

#endif
