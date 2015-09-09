#define E_COMP_WL
#include "e.h"
#include "e_mod_main_wl.h"
#include <string.h>

static int _e_keyrouter_find_duplicated_client(E_Client *ec, struct wl_client *wc, uint32_t key, uint32_t mode);
static const char *_mode_str_get(uint32_t mode);

/* add a new key grab info to the list */
int
e_keyrouter_set_keygrab_in_list(struct wl_resource *surface, struct wl_client *client, uint32_t key, uint32_t mode)
{
   E_Pixmap *cp = NULL;
   E_Client *ec = NULL;
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

   if (surface)
     {
        cp = wl_resource_get_user_data(surface);
        EINA_SAFETY_ON_NULL_RETURN_VAL
          (cp, TIZEN_KEYROUTER_ERROR_INVALID_SURFACE);

        ec = e_pixmap_client_get(cp);
        EINA_SAFETY_ON_NULL_RETURN_VAL
          (cp, TIZEN_KEYROUTER_ERROR_INVALID_SURFACE);
     }

   if (mode == TIZEN_KEYROUTER_MODE_TOPMOST)
     {
        EINA_SAFETY_ON_NULL_RETURN_VAL
          (ec, TIZEN_KEYROUTER_ERROR_INVALID_SURFACE);
     }

   res = e_keyrouter_prepend_to_keylist(ec,
                                        ec ? NULL : client,
                                        key,
                                        mode);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(res == TIZEN_KEYROUTER_ERROR_NONE, res);

   KLDBG("Succeed to set keygrab info ec:%p key:%d mode:%s\n",
         ec, key, _mode_str_get(mode));

   return res;
}

/* Function for checking whether the key has been grabbed already by the same wl_surface or not */
static int
_e_keyrouter_find_duplicated_client(E_Client *ec, struct wl_client *wc, uint32_t key, uint32_t mode)
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

        if (ec)
          {
             if (key_node_data->ec == ec)
               {
                  KLDBG("The key(%d) is already grabbed same mode(%d) on the same ec %p\n",
                        key, mode, ec);
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

/* Function for prepending a new key grab information in the keyrouting list */
int
e_keyrouter_prepend_to_keylist(E_Client *ec, struct wl_client *wc, uint32_t key, uint32_t mode)
{
   int res = TIZEN_KEYROUTER_ERROR_NONE;

   res = _e_keyrouter_find_duplicated_client(ec, wc, key, mode);
   CHECK_ERR_VAL(res);

   E_Keyrouter_Key_List_NodePtr new_keyptr = E_NEW(E_Keyrouter_Key_List_Node, 1);

   if (!new_keyptr)
     {
        KLDBG("Failled to allocate memory for new_keyptr\n");
        return TIZEN_KEYROUTER_ERROR_NO_SYSTEM_RESOURCES;
     }

   new_keyptr->ec = ec;
   new_keyptr->wc = wc;

   if (ec)
     {
        KLDBG("Now it's going to add a key(%d) mode(%d) for ec(%p), wc(NULL)\n", key, mode, ec);
     }
   else
     {
        KLDBG("Now it's going to add a key(%d) mode(%d) for ec(NULL), wc(%p)\n", key, mode, wc);
     }

   switch(mode)
     {
      case TIZEN_KEYROUTER_MODE_EXCLUSIVE:
         krt->HardKeys[key].excl_ptr = eina_list_prepend(krt->HardKeys[key].excl_ptr, new_keyptr);

         if (ec)
           {
              KLDBG("Succeed to set keygrab information (e_client:%p, wl_client:NULL, key:%d, mode:EXCLUSIVE)\n", ec, key);
           }
         else
           {
              KLDBG("Succeed to set keygrab information (e_client:NULL, wl_client:%p, key:%d, mode:EXCLUSIVE)\n", wc, key);
           }
         break;

      case TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE:
         krt->HardKeys[key].or_excl_ptr= eina_list_prepend(krt->HardKeys[key].or_excl_ptr, new_keyptr);

         if (ec)
           {
              KLDBG("TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE, key=%d, e_client(%p), wl_client(NULL) has been set !\n", key, ec);
           }
         else
           {
              KLDBG("TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE, key=%d, e_client(NULL), wl_client(%p) has been set !\n", key, wc);
           }
         break;

      case TIZEN_KEYROUTER_MODE_TOPMOST:
         krt->HardKeys[key].top_ptr = eina_list_prepend(krt->HardKeys[key].top_ptr, new_keyptr);

         if (ec)
           {
              KLDBG("TIZEN_KEYROUTER_MODE_TOPMOST, key=%d, e_client(%p), wl_client(NULL) has been set !\n", key, ec);
           }
         else
           {
              KLDBG("TIZEN_KEYROUTER_MODE_TOPMOST, key=%d, e_client(NULL), wl_client(%p) has been set !\n", key, wc);
           }
         break;

      case TIZEN_KEYROUTER_MODE_SHARED:
         krt->HardKeys[key].shared_ptr= eina_list_prepend(krt->HardKeys[key].shared_ptr, new_keyptr);

         if (ec)
           {
              KLDBG("TIZEN_KEYROUTER_MODE_SHARED, key=%d, e_client(%p), wl_client(NULL) has been set !\n", key, ec);
           }
         else
           {
              KLDBG("TIZEN_KEYROUTER_MODE_SHARED, key=%d, e_client(NULL), wl_client(%p) has been set !\n", key, wc);
           }
         break;

      case TIZEN_KEYROUTER_MODE_PRESSED:
         krt->HardKeys[key].press_ptr = eina_list_prepend(krt->HardKeys[key].press_ptr, new_keyptr);

         if (ec)
           {
              KLDBG("TIZEN_KEYROUTER_MODE_PRESSED, key=%d, e_client(%p), wl_client(NULL) has been set !\n", key, ec);
           }
         else
           {
              KLDBG("TIZEN_KEYROUTER_MODE_PRESSED, key=%d, e_client(NULL), wl_client(%p) has been set !\n", key, wc);
           }
         break;

      default:
         KLDBG("Unknown key(%d) and grab mode(%d)\n", key, mode);
         E_FREE(new_keyptr);
         return TIZEN_KEYROUTER_ERROR_INVALID_MODE;
     }

   if (wc)
     {
        KLDBG("Add a client(%p) destory listener\n", wc);
        e_keyrouter_add_client_destroy_listener(wc);
        /* TODO: if failed add client_destory_listener, remove keygrabs */
     }

   return TIZEN_KEYROUTER_ERROR_NONE;
}

/* remove key grab info from the list */
void
e_keyrouter_find_and_remove_client_from_list(E_Client *ec, struct wl_client *wc, uint32_t key, uint32_t mode)
{
   Eina_List **list = NULL;
   Eina_List *l = NULL, *l_next = NULL;
   E_Keyrouter_Key_List_NodePtr key_node_data = NULL;
   Eina_Bool removed;

   switch (mode)
     {
      case TIZEN_KEYROUTER_MODE_EXCLUSIVE:             list = &krt->HardKeys[key].excl_ptr;    break;
      case TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE: list = &krt->HardKeys[key].or_excl_ptr; break;
      case TIZEN_KEYROUTER_MODE_TOPMOST:               list = &krt->HardKeys[key].top_ptr;     break;
      case TIZEN_KEYROUTER_MODE_SHARED:                list = &krt->HardKeys[key].shared_ptr;  break;
      default:
         KLDBG("Unknown key(%d) and grab mode(%d)\n", key, mode);
         return;
     }

   EINA_LIST_FOREACH_SAFE(*list, l, l_next, key_node_data)
     {
        if (!key_node_data) continue;

        removed = EINA_FALSE;

        if (ec)
          {
             if (ec == key_node_data->ec)
               {
                  *list = eina_list_remove_list(*list, l);
                  E_FREE(key_node_data);
                  removed = EINA_TRUE;
               }
          }
        else
          {
             if (wc == key_node_data->wc)
               {
                  *list = eina_list_remove_list(*list, l);
                  E_FREE(key_node_data);
                  removed = EINA_TRUE;
               }
          }

        if (removed)
          {
             KLDBG("Remove a %s Mode Grabbed key(%d) by ec(%p) wc(NULL)\n",
                   _mode_str_get(mode), key, ec);
          }
     }
}

void
e_keyrouter_remove_client_from_list(E_Client *ec, struct wl_client *wc)
{
   int i = 0;

   EINA_SAFETY_ON_TRUE_RETURN(((!ec) && (!wc)));

   for (i = 0; i < MAX_HWKEYS; i++)
     {
        if (0 == krt->HardKeys[i].keycode) continue;

        e_keyrouter_find_and_remove_client_from_list(ec, wc, i, TIZEN_KEYROUTER_MODE_EXCLUSIVE);
        e_keyrouter_find_and_remove_client_from_list(ec, wc, i, TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE);
        e_keyrouter_find_and_remove_client_from_list(ec, wc, i, TIZEN_KEYROUTER_MODE_TOPMOST);
        e_keyrouter_find_and_remove_client_from_list(ec, wc, i, TIZEN_KEYROUTER_MODE_SHARED);
     }
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
