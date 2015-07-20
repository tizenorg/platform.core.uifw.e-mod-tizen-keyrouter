#ifndef E_MOD_MAIN_H
#define E_MOD_MAIN_H

/* Temporary value of maximum number of HWKeys */
#define MAX_HWKEYS 512

#define CHECK_ERR(val) if (WL_KEYROUTER_ERROR_NONE != val) return;
#define CHECK_ERR_VAL(val) if (WL_KEYROUTER_ERROR_NONE != val) return val;
#define CHECK_NULL(val) if (!val) return;
#define CHECK_NULL_VAL(val) if (!val) return val;

#define KLDBG(msg, ARG...) DBG("[wl_keyrouter][%s:%d] "msg, __FUNCTION__, __LINE__, ##ARG)
#define WL_KEYGRAB_NOTIFY_WITH_VAL(resource,surface,key,mode,err) \
        wl_keyrouter_send_keygrab_notify(resource, surface, key, mode, err); \
        return

typedef struct _E_Keyrouter E_Keyrouter;
typedef struct _E_Keyrouter* E_KeyrouterPtr;
typedef struct _E_Keyrouter_Key_List_Node E_Keyrouter_Key_List_Node;
typedef struct _E_Keyrouter_Key_List_Node* E_Keyrouter_Key_List_NodePtr;
typedef struct _E_Keyrouter_Tizen_HWKey E_Keyrouter_Tizen_HWKey;
typedef struct _E_Keyrouter_Grabbed_Key E_Keyrouter_Grabbed_Key;

#define WL_KEYROUTER_MODE_PRESSED WL_KEYROUTER_MODE_EXCLUSIVE+1

extern E_KeyrouterPtr krt;

struct _E_Keyrouter_Key_List_Node
{
   E_Client *ec;
   struct wl_client *wc;
};

struct _E_Keyrouter_Tizen_HWKey
{
   char *name;
   int keycode;
};

struct _E_Keyrouter_Grabbed_Key
{
   int keycode;
   char* keyname;

   Eina_List *excl_ptr;
   Eina_List *or_excl_ptr;
   Eina_List *top_ptr;
   Eina_List *shared_ptr;
   Eina_List *press_ptr;
};

struct _E_Keyrouter
{
   E_Comp_Data *cdata;
   struct wl_global *global;
   Ecore_Event_Filter *ef_handler;
   Eina_List *handlers;

   E_Keyrouter_Grabbed_Key HardKeys[MAX_HWKEYS];
   E_Keyrouter_Tizen_HWKey *TizenHWKeys;
   Eina_List *none_surface_grab_client;

   Eina_Bool isWindowStackChanged;
   int numTizenHWKeys;
};

/* E Module */
EAPI extern E_Module_Api e_modapi;
EAPI void *e_modapi_init(E_Module *m);
EAPI int   e_modapi_shutdown(E_Module *m);
EAPI int   e_modapi_save(E_Module *m);

int e_keyrouter_set_keygrab_in_list(struct wl_resource *surface, struct wl_client *client, uint32_t key, uint32_t mode);
int e_keyrouter_prepend_to_keylist(E_Client *ec, struct wl_client *wc, uint32_t key, uint32_t mode);
void e_keyrouter_find_and_remove_client_from_list(E_Client *ec, struct wl_client *wc, uint32_t key, uint32_t mode);
void e_keyrouter_remove_client_from_list(E_Client *ec, struct wl_client *wc);

int e_keyrouter_add_client_destroy_listener(struct wl_client *client);

#endif
