#define E_COMP_WL
#include "e_mod_main_wl.h"
#include <string.h>

static int _e_keyrouter_find_duplicated_client(struct wl_resource *surface, struct wl_client *wc, uint32_t key, uint32_t mode);
static const char *_mode_str_get(uint32_t mode);
static Eina_Bool _e_keyrouter_find_key_in_list(struct wl_resource *surface, struct wl_client *wc, int key, int mode);
static Eina_List **_e_keyrouter_get_list(int mode, int key);
static void _e_keyrouter_add_registered_surface_in_list(struct wl_resource *surface, int key);
static void _e_keyrouter_remove_registered_surface_in_list(struct wl_resource *surface);
static void _e_keyrouter_clean_register_list(void);
static void _e_keyrouter_build_register_list(void);

/* add a new key grab info to the list */
int
e_keyrouter_set_keygrab_in_list(struct wl_resource *surface, struct wl_client *client, uint32_t key, uint32_t mode)
{
   int res = TIZEN_KEYROUTER_ERROR_NONE;

   EINA_SAFETY_ON_FALSE_RETURN_VAL
     (((mode == TIZEN_KEYROUTER_MODE_EXCLUSIVE) ||
       (mode == TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE) ||
       (mode == TIZEN_KEYROUTER_MODE_TOPMOST) ||
       (mode == TIZEN_KEYROUTER_MODE_SHARED) ||
       (mode == TIZEN_KEYROUTER_MODE_REGISTERED)),
      TIZEN_KEYROUTER_ERROR_INVALID_MODE);

   if (mode == TIZEN_KEYROUTER_MODE_EXCLUSIVE)
     {
        EINA_SAFETY_ON_TRUE_RETURN_VAL
          ((krt->HardKeys[key].excl_ptr != NULL),
           TIZEN_KEYROUTER_ERROR_GRABBED_ALREADY);
     }

   if (mode == TIZEN_KEYROUTER_MODE_TOPMOST)
     {
        EINA_SAFETY_ON_NULL_RETURN_VAL
          (surface, TIZEN_KEYROUTER_ERROR_INVALID_SURFACE);
     }

   if (TIZEN_KEYROUTER_MODE_REGISTERED == mode)
     {
        res = e_keyrouter_set_keyregister(client, surface, key);
     }
   else
     {
        res = e_keyrouter_prepend_to_keylist(surface,
                                        surface ? NULL : client,
                                        key,
                                        mode,
                                        EINA_FALSE);
     }

   EINA_SAFETY_ON_FALSE_RETURN_VAL(res == TIZEN_KEYROUTER_ERROR_NONE, res);

   KLINF("Succeed to set keygrab info surface: %p, client: %p key: %d mode: %s\n",
         surface, client, key, _mode_str_get(mode));

   return res;
}

/* Function for checking whether the key has been grabbed already by the same wl_surface or not */
static int
_e_keyrouter_find_duplicated_client(struct wl_resource *surface, struct wl_client *wc, uint32_t key, uint32_t mode)
{
   Eina_List *keylist_ptr = NULL, *l = NULL;
   E_Keyrouter_Key_List_NodePtr key_node_data = NULL;

   switch(mode)
     {
      case TIZEN_KEYROUTER_MODE_EXCLUSIVE:
         return TIZEN_KEYROUTER_ERROR_NONE;

      case TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE:
         keylist_ptr = krt->HardKeys[key].or_excl_ptr;
         break;

      case TIZEN_KEYROUTER_MODE_TOPMOST:
         keylist_ptr = krt->HardKeys[key].top_ptr;
         break;

      case TIZEN_KEYROUTER_MODE_SHARED:
         keylist_ptr = krt->HardKeys[key].shared_ptr;
         break;

      case TIZEN_KEYROUTER_MODE_PRESSED:
         keylist_ptr = krt->HardKeys[key].press_ptr;
         break;

      default:
         KLWRN("Unknown key(%d) and grab mode(%d)\n", key, mode);
         return TIZEN_KEYROUTER_ERROR_INVALID_MODE;
     }

   EINA_LIST_FOREACH(keylist_ptr, l, key_node_data)
     {
        if (!key_node_data) continue;

        if (surface)
          {
             if (key_node_data->surface == surface)
               {
                  KLDBG("The key(%d) is already grabbed same mode(%d) on the same surface %p\n",
                        key, mode, surface);
                  return TIZEN_KEYROUTER_ERROR_GRABBED_ALREADY;
               }
          }
        else
          {
             if (key_node_data->wc == wc)
               {
                  KLDBG("The key(%d) is already grabbed same mode(%d) on the same wl_client %p\n",
                        key, mode, wc);
                  return TIZEN_KEYROUTER_ERROR_GRABBED_ALREADY;
               }
          }
     }

   return TIZEN_KEYROUTER_ERROR_NONE;
}

static Eina_Bool
_e_keyrouter_find_key_in_list(struct wl_resource *surface, struct wl_client *wc, int key, int mode)
{
   Eina_List **list = NULL;
   Eina_List *l = NULL, *l_next = NULL;
   E_Keyrouter_Key_List_NodePtr key_node_data = NULL;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(((!surface) && (!wc)), EINA_FALSE);

   list = _e_keyrouter_get_list(mode, key);
   EINA_SAFETY_ON_NULL_RETURN_VAL(list, EINA_FALSE);

   EINA_LIST_FOREACH_SAFE(*list, l, l_next, key_node_data)
     {
        if (!key_node_data) continue;

        if ((surface) && (surface == key_node_data->surface)) return EINA_TRUE;
        else if ((wc == key_node_data->wc)) return EINA_TRUE;
     }

   return EINA_FALSE;
}


/* Function for prepending a new key grab information in the keyrouting list */
int
e_keyrouter_prepend_to_keylist(struct wl_resource *surface, struct wl_client *wc, uint32_t key, uint32_t mode, Eina_Bool focused)
{
   int res = TIZEN_KEYROUTER_ERROR_NONE;

   res = _e_keyrouter_find_duplicated_client(surface, wc, key, mode);
   CHECK_ERR_VAL(res);

   E_Keyrouter_Key_List_NodePtr new_keyptr = E_NEW(E_Keyrouter_Key_List_Node, 1);

   if (!new_keyptr)
     {
        KLERR("Failled to allocate memory for new_keyptr\n");
        return TIZEN_KEYROUTER_ERROR_NO_SYSTEM_RESOURCES;
     }

   new_keyptr->surface = surface;
   new_keyptr->wc = wc;
   new_keyptr->focused = focused;

   switch(mode)
     {
      case TIZEN_KEYROUTER_MODE_EXCLUSIVE:
         krt->HardKeys[key].excl_ptr = eina_list_prepend(krt->HardKeys[key].excl_ptr, new_keyptr);
         break;

      case TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE:
         krt->HardKeys[key].or_excl_ptr= eina_list_prepend(krt->HardKeys[key].or_excl_ptr, new_keyptr);
         break;

      case TIZEN_KEYROUTER_MODE_TOPMOST:
         krt->HardKeys[key].top_ptr = eina_list_prepend(krt->HardKeys[key].top_ptr, new_keyptr);
         break;

      case TIZEN_KEYROUTER_MODE_SHARED:
         krt->HardKeys[key].shared_ptr= eina_list_prepend(krt->HardKeys[key].shared_ptr, new_keyptr);
         break;

      case TIZEN_KEYROUTER_MODE_PRESSED:
         krt->HardKeys[key].press_ptr = eina_list_prepend(krt->HardKeys[key].press_ptr, new_keyptr);
         break;

      default:
         KLWRN("Unknown key(%d) and grab mode(%d)\n", key, mode);
         E_FREE(new_keyptr);
         return TIZEN_KEYROUTER_ERROR_INVALID_MODE;
     }

   if (TIZEN_KEYROUTER_MODE_PRESSED != mode)
     {
        if (surface)
          {
             e_keyrouter_add_surface_destroy_listener(surface);
             /* TODO: if failed add surface_destory_listener, remove keygrabs */
          }
        /* Add a client destroy listener if cynara is not enabled.
           If cynara is enabled, client destroy listener is added at privilege checking time */
#ifdef ENABLE_CYNARA
        else if (!krt->p_cynara && wc)
#else
        else if (wc)
#endif
          {
             e_keyrouter_add_client_destroy_listener(wc);
             /* TODO: if failed add client_destory_listener, remove keygrabs */
          }
     }

   return TIZEN_KEYROUTER_ERROR_NONE;
}

/* remove key grab info from the list */
void
e_keyrouter_find_and_remove_client_from_list(struct wl_resource *surface, struct wl_client *wc, uint32_t key, uint32_t mode)
{
   Eina_List **list = NULL;
   Eina_List *l = NULL, *l_next = NULL;
   E_Keyrouter_Key_List_NodePtr key_node_data = NULL;

   list = _e_keyrouter_get_list(mode, key);
   EINA_SAFETY_ON_NULL_RETURN(list);

   EINA_LIST_FOREACH_SAFE(*list, l, l_next, key_node_data)
     {
        if (!key_node_data) continue;

        if (surface)
          {
             if (surface == key_node_data->surface)
               {
                  *list = eina_list_remove_list(*list, l);
                  E_FREE(key_node_data);
                  KLDBG("Remove a %s Mode Grabbed key(%d) by surface(%p)\n", _mode_str_get(mode), key, surface);
               }
          }
        else if ((wc == key_node_data->wc))
          {
             *list = eina_list_remove_list(*list, l);
             E_FREE(key_node_data);
             KLDBG("Remove a %s Mode Grabbed key(%d) by wc(%p)\n", _mode_str_get(mode), key, wc);
          }
     }
}

void
e_keyrouter_remove_client_from_list(struct wl_resource *surface, struct wl_client *wc)
{
   int i = 0;
   Eina_List *l = NULL, *l_next = NULL;
   E_Keyrouter_Key_List_NodePtr key_node_data = NULL;

   EINA_SAFETY_ON_TRUE_RETURN(((!surface) && (!wc)));

   for (i = 0; i < krt->max_tizen_hwkeys; i++)
     {
        if (0 == krt->HardKeys[i].keycode) continue;

        EINA_LIST_FOREACH_SAFE(krt->HardKeys[i].excl_ptr, l, l_next, key_node_data)
          {
             if (!key_node_data) continue;

             if (surface)
               {
                  if (surface == key_node_data->surface)
                    {
                       krt->HardKeys[i].excl_ptr = eina_list_remove_list(krt->HardKeys[i].excl_ptr, l);
                       E_FREE(key_node_data);
                       KLINF("Remove a Exclusive Mode Grabbed key(%d) by surface(%p)\n", i, surface);
                    }
               }
             else if ((wc == key_node_data->wc))
               {
                  krt->HardKeys[i].excl_ptr = eina_list_remove_list(krt->HardKeys[i].excl_ptr, l);
                  E_FREE(key_node_data);
                  KLINF("Remove a Exclusive Mode Grabbed key(%d) by wc(%p)\n", i, wc);
               }
          }
        EINA_LIST_FOREACH_SAFE(krt->HardKeys[i].or_excl_ptr, l, l_next, key_node_data)
          {
             if (!key_node_data) continue;

             if (surface)
               {
                  if (surface == key_node_data->surface)
                    {
                       krt->HardKeys[i].or_excl_ptr = eina_list_remove_list(krt->HardKeys[i].or_excl_ptr, l);
                       E_FREE(key_node_data);
                       KLINF("Remove a Overridable_Exclusive Mode Grabbed key(%d) by surface(%p)\n", i, surface);
                    }
               }
             else if ((wc == key_node_data->wc))
               {
                  krt->HardKeys[i].or_excl_ptr = eina_list_remove_list(krt->HardKeys[i].or_excl_ptr, l);
                  E_FREE(key_node_data);
                  KLINF("Remove a Overridable_Exclusive Mode Grabbed key(%d) by wc(%p)\n", i, wc);
               }
          }
        EINA_LIST_FOREACH_SAFE(krt->HardKeys[i].top_ptr, l, l_next, key_node_data)
          {
             if (!key_node_data) continue;

             if (surface)
               {
                  if (surface == key_node_data->surface)
                    {
                       krt->HardKeys[i].top_ptr = eina_list_remove_list(krt->HardKeys[i].top_ptr, l);
                       E_FREE(key_node_data);
                       KLINF("Remove a Topmost Mode Grabbed key(%d) by surface(%p)\n", i, surface);
                    }
               }
             else if ((wc == key_node_data->wc))
               {
                  krt->HardKeys[i].top_ptr = eina_list_remove_list(krt->HardKeys[i].top_ptr, l);
                  E_FREE(key_node_data);
                  KLINF("Remove a Topmost Mode Grabbed key(%d) by wc(%p)\n", i, wc);
               }
          }
        EINA_LIST_FOREACH_SAFE(krt->HardKeys[i].shared_ptr, l, l_next, key_node_data)
          {
             if (!key_node_data) continue;

             if (surface)
               {
                  if (surface == key_node_data->surface)
                    {
                       krt->HardKeys[i].shared_ptr = eina_list_remove_list(krt->HardKeys[i].shared_ptr, l);
                       E_FREE(key_node_data);
                       KLINF("Remove a Shared Mode Grabbed key(%d) by surface(%p)\n", i, surface);
                    }
               }
             else if ((wc == key_node_data->wc))
               {
                  krt->HardKeys[i].shared_ptr = eina_list_remove_list(krt->HardKeys[i].shared_ptr, l);
                  E_FREE(key_node_data);
                  KLINF("Remove a Shared Mode Grabbed key(%d) by wc(%p)\n", i, wc);
               }
          }
     }

   _e_keyrouter_remove_registered_surface_in_list(surface);
}

int
e_keyrouter_find_key_in_list(struct wl_resource *surface, struct wl_client *wc, uint32_t key)
{
   int mode = TIZEN_KEYROUTER_MODE_NONE;
   Eina_Bool found = EINA_FALSE;

   mode = TIZEN_KEYROUTER_MODE_EXCLUSIVE;
   found = _e_keyrouter_find_key_in_list(surface, wc, key, mode);
   if (found) goto finish;

   mode = TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE;
   found = _e_keyrouter_find_key_in_list(surface, wc, key, mode);
   if (found) goto finish;

   mode = TIZEN_KEYROUTER_MODE_TOPMOST;
   found = _e_keyrouter_find_key_in_list(surface, wc, key, mode);
   if (found) goto finish;

   mode = TIZEN_KEYROUTER_MODE_SHARED;
   found = _e_keyrouter_find_key_in_list(surface, wc, key, mode);
   if (found) goto finish;

   return TIZEN_KEYROUTER_MODE_NONE;

finish:
   KLDBG("Find %d key grabbed by (surface: %p, wl_client: %p) in %s mode\n",
         key, surface, wc, _mode_str_get(mode));
   return mode;
}

static void
_e_keyrouter_clean_register_list(void)
{
   Eina_List *l, *ll;
   E_Keyrouter_Registered_Window_Info *data;
   int *ddata;

   EINA_LIST_FOREACH(krt->registered_window_list, l, data)
     {
        if (!data) continue;

        EINA_LIST_FOREACH(data->keys, ll, ddata)
          {
             if (!ddata) continue;

             E_FREE(krt->HardKeys[*ddata].registered_ptr);
          }
     }
}

static void
_e_keyrouter_build_register_list(void)
{
   E_Client *ec_top = NULL, *ec_focus = NULL;
   Eina_List *l = NULL, *ll = NULL, *l_next = NULL, *register_list_reordered = NULL;
   E_Keyrouter_Registered_Window_Info *data = NULL;
   int *ddata = NULL;
   E_Keyrouter_Key_List_Node *node = NULL;
   struct wl_resource *surface = NULL;
   Eina_Bool below_focus = EINA_FALSE;

   ec_top = e_client_top_get();
   ec_focus = e_client_focused_get();

   while (ec_top)
     {
        surface = e_keyrouter_util_get_surface_from_eclient(ec_top);
        if (ec_top == ec_focus)
          {
             below_focus = EINA_TRUE;
          }

        if (EINA_TRUE == below_focus && !e_keyrouter_is_registered_window(surface))
          {
             KLDBG("%p is none registered window, below focus surface\n", surface);
             break;
          }

        EINA_LIST_FOREACH_SAFE(krt->registered_window_list, l, l_next, data)
          {
             if (!data) continue;

             if (data->surface != surface) continue;

             EINA_LIST_FOREACH(data->keys, ll, ddata)
               {
                  if (!ddata) continue;

                  if (!krt->HardKeys[*ddata].registered_ptr)
                    {
                       node = E_NEW(E_Keyrouter_Key_List_Node, 1);
                       node->surface = surface;
                       node->wc = NULL;
                       node->focused = EINA_FALSE;
                       krt->HardKeys[*ddata].registered_ptr = node;

                       KLDBG("%d key's register surface is %p\n", *ddata, surface);
                    }
               }

             register_list_reordered = eina_list_append(register_list_reordered, data);
             krt->registered_window_list = eina_list_remove(krt->registered_window_list, data);
          }

        ec_top = e_client_below_get(ec_top);
     }

   EINA_LIST_FOREACH_SAFE(krt->registered_window_list, l, l_next, data)
     {
        if (!data) continue;

        register_list_reordered = eina_list_append(register_list_reordered, data);
        krt->registered_window_list = eina_list_remove(krt->registered_window_list, data);
     }

   krt->registered_window_list = register_list_reordered;
}


int
e_keyrouter_set_keyregister(struct wl_client *client, struct wl_resource *surface, uint32_t key)
{
   int res = TIZEN_KEYROUTER_ERROR_NONE;

   _e_keyrouter_add_registered_surface_in_list(surface, key);

   if (surface)
     {
        e_keyrouter_add_surface_destroy_listener(surface);
        /* TODO: if failed add surface_destory_listener, remove keygrabs */
     }

   _e_keyrouter_clean_register_list();
   _e_keyrouter_build_register_list();

   return res;
}

int
e_keyrouter_unset_keyregister(struct wl_resource *surface, struct wl_client *client, uint32_t key)
{
   int res = TIZEN_KEYROUTER_ERROR_NONE;
   Eina_List *l, *ll, *ll_next;
   E_Keyrouter_Registered_Window_Info *data;
   int *ddata;

   _e_keyrouter_clean_register_list();

   EINA_LIST_FOREACH(krt->registered_window_list, l, data)
     {
        if (!data) continue;

        if (data->surface == surface)
          {
             EINA_LIST_FOREACH_SAFE(data->keys, ll, ll_next, ddata)
               {
                  if (!ddata) continue;

                  if (*ddata == key)
                    {
                       data->keys = eina_list_remove_list(data->keys, ll);
                       E_FREE(ddata);
                    }
               }
          }
     }

   _e_keyrouter_build_register_list();

   KLDBG("Succeed to set keyregister info surface: %p, client: %p key: %d\n",
         surface, client, key);

   return res;
}

static void
_e_keyrouter_add_registered_surface_in_list(struct wl_resource *surface, int key)
{
   E_Keyrouter_Registered_Window_Info *rwin_info, *rwin_added;
   Eina_List *l, *ll;
   int *key_data, *key_added;
   Eina_Bool key_finded = EINA_FALSE, surface_finded = EINA_FALSE;

   EINA_LIST_FOREACH(krt->registered_window_list, l, rwin_info)
     {
        if (!rwin_info) continue;

        if (rwin_info->surface == surface)
          {
             EINA_LIST_FOREACH(rwin_info->keys, ll, key_data)
               {
                  if (!key_data) continue;

                  if (*key_data == key)
                    {
                       KLDBG("Registered Key(%d) already registered by surface(%p)\n", key, surface);
                       key_finded = EINA_TRUE;
                       break;
                    }
               }
             if (EINA_TRUE == key_finded) break;

             key_added = E_NEW(int, 1);
             *key_added = key;
             rwin_info->keys = eina_list_append(rwin_info->keys, key_added);
             surface_finded = EINA_TRUE;

             KLINF("Registered Key(%d) is added to surface (%p)\n", key, surface);
             break;
          }
     }

   if (EINA_FALSE == surface_finded)
     {
        rwin_added = E_NEW(E_Keyrouter_Registered_Window_Info, 1);
        rwin_added->surface = surface;
        key_added = E_NEW(int, 1);
        *key_added = key;
        rwin_added->keys = eina_list_append(rwin_added->keys, key_added);
        krt->registered_window_list = eina_list_append(krt->registered_window_list, rwin_added);

        KLINF("Surface(%p) and key(%d) is added list\n", surface, key);
     }
}

static void
_e_keyrouter_remove_registered_surface_in_list(struct wl_resource *surface)
{
   Eina_List *l, *l_next;
   E_Keyrouter_Registered_Window_Info *data;
   int *ddata;

   _e_keyrouter_clean_register_list();

   EINA_LIST_FOREACH_SAFE(krt->registered_window_list, l, l_next, data)
     {
        if (!data) continue;

        if (data->surface == surface)
          {
             EINA_LIST_FREE(data->keys, ddata)
               {
                  E_FREE(ddata);
               }
             krt->registered_window_list = eina_list_remove_list(krt->registered_window_list, l);
             E_FREE(data);
             KLDBG("Remove a %p surface in register list\n", surface);
             break;
          }
     }

   _e_keyrouter_build_register_list();
}

Eina_Bool
e_keyrouter_is_registered_window(struct wl_resource *surface)
{
   Eina_List *l;
   E_Keyrouter_Registered_Window_Info *data;

   EINA_LIST_FOREACH(krt->registered_window_list, l, data)
     {
        if (!data) continue;

        if (data->surface == surface)
          {
             KLDBG("Surface %p is registered window\n", surface);
             return EINA_TRUE;
          }
     }

   return EINA_FALSE;
}

void
e_keyrouter_clear_registered_window(void)
{
   if (!krt->registered_window_list)
     {
        return;
     }
   _e_keyrouter_clean_register_list();
   _e_keyrouter_build_register_list();
}

static const char *
_mode_str_get(uint32_t mode)
{
   const char *str = NULL;

   switch (mode)
     {
      case TIZEN_KEYROUTER_MODE_EXCLUSIVE:             str = "Exclusive";             break;
      case TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE: str = "Overridable_Exclusive"; break;
      case TIZEN_KEYROUTER_MODE_TOPMOST:               str = "Topmost";               break;
      case TIZEN_KEYROUTER_MODE_SHARED:                str = "Shared";                break;
      case TIZEN_KEYROUTER_MODE_REGISTERED:            str = "Registered";            break;
      default: str = "UnknownMode"; break;
     }

   return str;
}

static Eina_List **
_e_keyrouter_get_list(int mode, int key)
{
   Eina_List **list = NULL;

   switch (mode)
     {
        case TIZEN_KEYROUTER_MODE_EXCLUSIVE:             list = &krt->HardKeys[key].excl_ptr;    break;
        case TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE: list = &krt->HardKeys[key].or_excl_ptr; break;
        case TIZEN_KEYROUTER_MODE_TOPMOST:               list = &krt->HardKeys[key].top_ptr;     break;
        case TIZEN_KEYROUTER_MODE_SHARED:                list = &krt->HardKeys[key].shared_ptr;  break;
        default: break;
     }

   return list;
}
