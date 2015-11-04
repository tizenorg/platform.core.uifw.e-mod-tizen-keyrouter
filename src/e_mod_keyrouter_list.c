#define E_COMP_WL
#include "e.h"
#include "e_mod_main_wl.h"
#include <string.h>

static int _e_keyrouter_find_duplicated_client(struct wl_resource *surface, struct wl_client *wc, uint32_t key, uint32_t mode);
static const char *_mode_str_get(uint32_t mode);
static Eina_Bool _e_keyrouter_find_key_in_list(struct wl_resource *surface, struct wl_client *wc, int key, int mode);
static Eina_List **_e_keyrouter_get_list(int mode, int key);

/* add a new key grab info to the list */
int
e_keyrouter_set_keygrab_in_list(struct wl_resource *surface, struct wl_client *client, uint32_t key, uint32_t mode)
{
   int res = TIZEN_KEYROUTER_ERROR_NONE;

   EINA_SAFETY_ON_FALSE_RETURN_VAL
     (((mode == TIZEN_KEYROUTER_MODE_EXCLUSIVE) ||
       (mode == TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE) ||
       (mode == TIZEN_KEYROUTER_MODE_TOPMOST) ||
       (mode == TIZEN_KEYROUTER_MODE_SHARED)),
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

   res = e_keyrouter_prepend_to_keylist(surface,
                                        surface ? NULL : client,
                                        key,
                                        mode);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(res == TIZEN_KEYROUTER_ERROR_NONE, res);

   KLDBG("Succeed to set keygrab info surface: %p, client: %p key: %d mode: %s\n",
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
         KLDBG("Unknown key(%d) and grab mode(%d)\n", key, mode);
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
e_keyrouter_prepend_to_keylist(struct wl_resource *surface, struct wl_client *wc, uint32_t key, uint32_t mode)
{
   int res = TIZEN_KEYROUTER_ERROR_NONE;

   res = _e_keyrouter_find_duplicated_client(surface, wc, key, mode);
   CHECK_ERR_VAL(res);

   E_Keyrouter_Key_List_NodePtr new_keyptr = E_NEW(E_Keyrouter_Key_List_Node, 1);

   if (!new_keyptr)
     {
        KLDBG("Failled to allocate memory for new_keyptr\n");
        return TIZEN_KEYROUTER_ERROR_NO_SYSTEM_RESOURCES;
     }

   new_keyptr->surface = surface;
   new_keyptr->wc = wc;

   if (surface)
     {
        KLDBG("Now it's going to add a key(%d) mode(%d) for surface(%p), wc(NULL)\n", key, mode, surface);
     }
   else
     {
        KLDBG("Now it's going to add a key(%d) mode(%d) for surface(NULL), wc(%p)\n", key, mode, wc);
     }

   switch(mode)
     {
      case TIZEN_KEYROUTER_MODE_EXCLUSIVE:
         krt->HardKeys[key].excl_ptr = eina_list_prepend(krt->HardKeys[key].excl_ptr, new_keyptr);

         if (surface)
           {
              KLDBG("Succeed to set keygrab information (surface:%p, wl_client:NULL, key:%d, mode:EXCLUSIVE)\n", surface, key);
           }
         else
           {
              KLDBG("Succeed to set keygrab information (surface:NULL, wl_client:%p, key:%d, mode:EXCLUSIVE)\n", wc, key);
           }
         break;

      case TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE:
         krt->HardKeys[key].or_excl_ptr= eina_list_prepend(krt->HardKeys[key].or_excl_ptr, new_keyptr);

         if (surface)
           {
              KLDBG("TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE, key=%d, surface(%p), wl_client(NULL) has been set !\n", key, surface);
           }
         else
           {
              KLDBG("TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE, key=%d, surface(NULL), wl_client(%p) has been set !\n", key, wc);
           }
         break;

      case TIZEN_KEYROUTER_MODE_TOPMOST:
         krt->HardKeys[key].top_ptr = eina_list_prepend(krt->HardKeys[key].top_ptr, new_keyptr);

         if (surface)
           {
              KLDBG("TIZEN_KEYROUTER_MODE_TOPMOST, key=%d, surface(%p), wl_client(NULL) has been set !\n", key, surface);
           }
         else
           {
              KLDBG("TIZEN_KEYROUTER_MODE_TOPMOST, key=%d, surface(NULL), wl_client(%p) has been set !\n", key, wc);
           }
         break;

      case TIZEN_KEYROUTER_MODE_SHARED:
         krt->HardKeys[key].shared_ptr= eina_list_prepend(krt->HardKeys[key].shared_ptr, new_keyptr);

         if (surface)
           {
              KLDBG("TIZEN_KEYROUTER_MODE_SHARED, key=%d, surface(%p), wl_client(NULL) has been set !\n", key, surface);
           }
         else
           {
              KLDBG("TIZEN_KEYROUTER_MODE_SHARED, key=%d, surface(NULL), wl_client(%p) has been set !\n", key, wc);
           }
         break;

      case TIZEN_KEYROUTER_MODE_PRESSED:
         krt->HardKeys[key].press_ptr = eina_list_prepend(krt->HardKeys[key].press_ptr, new_keyptr);

         if (surface)
           {
              KLDBG("TIZEN_KEYROUTER_MODE_PRESSED, key=%d, surface(%p), wl_client(NULL) has been set !\n", key, surface);
           }
         else
           {
              KLDBG("TIZEN_KEYROUTER_MODE_PRESSED, key=%d, surface(NULL), wl_client(%p) has been set !\n", key, wc);
           }
         break;

      default:
         KLDBG("Unknown key(%d) and grab mode(%d)\n", key, mode);
         E_FREE(new_keyptr);
         return TIZEN_KEYROUTER_ERROR_INVALID_MODE;
     }

   if (TIZEN_KEYROUTER_MODE_PRESSED != mode)
     {
        if (surface)
          {
             KLDBG("Add a surface(%p) destory listener\n", surface);
             e_keyrouter_add_surface_destroy_listener(surface);
             /* TODO: if failed add surface_destory_listener, remove keygrabs */
          }
        else if (wc)
          {
             KLDBG("Add a client(%p) destory listener\n", wc);
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

   for (i = 0; i < MAX_HWKEYS; i++)
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
                       KLDBG("Remove a Exclusive Mode Grabbed key(%d) by surface(%p)\n", i, surface);
                    }
               }
             else if ((wc == key_node_data->wc))
               {
                  krt->HardKeys[i].excl_ptr = eina_list_remove_list(krt->HardKeys[i].excl_ptr, l);
                  E_FREE(key_node_data);
                  KLDBG("Remove a Exclusive Mode Grabbed key(%d) by wc(%p)\n", i, wc);
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
                       KLDBG("Remove a Overridable_Exclusive Mode Grabbed key(%d) by surface(%p)\n", i, surface);
                    }
               }
             else if ((wc == key_node_data->wc))
               {
                  krt->HardKeys[i].or_excl_ptr = eina_list_remove_list(krt->HardKeys[i].or_excl_ptr, l);
                  E_FREE(key_node_data);
                  KLDBG("Remove a Overridable_Exclusive Mode Grabbed key(%d) by wc(%p)\n", i, wc);
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
                       KLDBG("Remove a Topmost Mode Grabbed key(%d) by surface(%p)\n", i, surface);
                    }
               }
             else if ((wc == key_node_data->wc))
               {
                  krt->HardKeys[i].top_ptr = eina_list_remove_list(krt->HardKeys[i].top_ptr, l);
                  E_FREE(key_node_data);
                  KLDBG("Remove a Topmost Mode Grabbed key(%d) by wc(%p)\n", i, wc);
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
                       KLDBG("Remove a Shared Mode Grabbed key(%d) by surface(%p)\n", i, surface);
                    }
               }
             else if ((wc == key_node_data->wc))
               {
                  krt->HardKeys[i].shared_ptr = eina_list_remove_list(krt->HardKeys[i].shared_ptr, l);
                  E_FREE(key_node_data);
                  KLDBG("Remove a Shared Mode Grabbed key(%d) by wc(%p)\n", i, wc);
               }
          }
     }
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

   KLDBG("%d key is not grabbed by (surface: %p, wl_client: %p)\n",
            key, surface, wc);
   return TIZEN_KEYROUTER_MODE_NONE;

finish:
   KLDBG("Find %d key grabbed by (surface: %p, wl_client: %p) in %s mode\n",
         key, surface, wc, _mode_str_get(mode));
   return mode;
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
