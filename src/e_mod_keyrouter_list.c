#define E_COMP_WL
#include "e.h"
#include "e_mod_main_wl.h"
#include <string.h>

static int _e_keyrouter_find_duplicated_client(E_Client *ec, struct wl_client *wc, uint32_t key, uint32_t mode);

/* Function for adding a new key grab information into the keyrouting list */
int
e_keyrouter_set_keygrab_in_list(struct wl_resource *surface, struct wl_client *client, uint32_t key, uint32_t mode)
{
   E_Pixmap *ep = NULL;
   E_Client *ec = NULL;

   int res = TIZEN_KEYROUTER_ERROR_NONE;

   if (surface)
     {
        if (!(ep = wl_resource_get_user_data(surface)))
          {
             KLDBG("Surface is valid and e_pixmap pointer from the surface is invalid ! Return error !\n");
             return TIZEN_KEYROUTER_ERROR_INVALID_SURFACE;
          }

        if (!(ec = e_pixmap_client_get(ep)))
          {
             KLDBG("e_client pointer from e_pixmap pointer  from surface is invalid ! Return error !\n");
             return TIZEN_KEYROUTER_ERROR_INVALID_SURFACE;
          }
     }

   switch(mode)
     {
        case TIZEN_KEYROUTER_MODE_EXCLUSIVE:
           if (krt->HardKeys[key].excl_ptr)
             {
                KLDBG("key(%d) is already exclusive grabbed\n", key);
                return TIZEN_KEYROUTER_ERROR_GRABBED_ALREADY;
             }
           if (ec)
             {
                res = e_keyrouter_prepend_to_keylist(ec, NULL, key, mode);
             }
           else
             {
                res = e_keyrouter_prepend_to_keylist(NULL, client, key, mode);
             }
           CHECK_ERR_VAL(res);

           break;

        case TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE:
           if (ec)
             {
                res = e_keyrouter_prepend_to_keylist(ec, NULL, key, mode);
             }
           else
             {
                res = e_keyrouter_prepend_to_keylist(NULL, client, key, mode);
             }
           CHECK_ERR_VAL(res);

           KLDBG("Succeed to set keygrab information (E_Client:%p, key:%d, mode:OR_EXCLUSIVE)\n", ec, key);
           break;

        case TIZEN_KEYROUTER_MODE_TOPMOST:
           res = e_keyrouter_prepend_to_keylist(ec, NULL, key, mode);
           CHECK_ERR_VAL(res);

           KLDBG("Succeed to set keygrab information (E_Client:%p, key:%d, mode:TOPMOST)\n", ec, key);
           break;

        case TIZEN_KEYROUTER_MODE_SHARED:
           if (ec)
             {
                res = e_keyrouter_prepend_to_keylist(ec, NULL, key, mode);
             }
           else
             {
                res = e_keyrouter_prepend_to_keylist(NULL, client, key, mode);
             }
           CHECK_ERR_VAL(res);

           KLDBG("Succeed to set keygrab information (E_Client:%p, key:%d, mode:SHARED)\n", ec, key);
           break;

        default:
           KLDBG("Unknown key(%d) grab mode(%d)\n", key, mode);
           return TIZEN_KEYROUTER_ERROR_INVALID_MODE;
     }

   //KLDBG("krt->HardKeys[%d].keycode: %d\n", key, krt->HardKeys[key].keycode);
   return TIZEN_KEYROUTER_ERROR_NONE;
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
        if (key_node_data)
          {
             if (ec)
               {
                  if (key_node_data->ec == ec)
                    {
                       KLDBG("The key(%d) is already grabbed same mode(%d) on the same E_Client(%p)\n", key, mode, ec);
                       return TIZEN_KEYROUTER_ERROR_GRABBED_ALREADY;
                    }
               }
             else
               {
                  if (key_node_data->wc == wc)
                    {
                       KLDBG("The key(%d) is already grabbed same mode(%d) on the same Wl_Client(%p)\n", key, mode, wc);
                       return TIZEN_KEYROUTER_ERROR_GRABBED_ALREADY;
                    }
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

   if ( (wc) && (mode != TIZEN_KEYROUTER_MODE_PRESSED) )
     {
        KLDBG("Add a client(%p) destory listener\n", wc);
        e_keyrouter_add_client_destroy_listener(wc);
        /* TODO: if failed add client_destory_listener, remove keygrabs */
     }

   return TIZEN_KEYROUTER_ERROR_NONE;
}

#define E_KEYROUTER_REMOVE_KEY_NODE_IN_LIST(list, ec, wc, l, l_next, key_node_data, key, mode_str) \
         EINA_LIST_FOREACH_SAFE(list, l, l_next, key_node_data) \
             { \
                if (key_node_data) \
                  { \
                     if (ec) \
                       { \
                          if (ec == key_node_data->ec) \
                            { \
                               list = eina_list_remove_list(list, l); \
                               E_FREE(key_node_data); \
                               KLDBG("Remove a %s Mode Grabbed key(%d) by ec(%p) wc(NULL)\n", mode_str, key, ec); \
                            } \
                       } \
                     else \
                       { \
                          if (wc == key_node_data->wc) \
                            { \
                               list = eina_list_remove_list(list, l); \
                               E_FREE(key_node_data); \
                               KLDBG("Remove a %s Mode Grabbed key(%d) by ec(NULL) wc(%p)\n", mode_str, key, wc); \
                            } \
                       } \
                  } \
             }

/* Function for removing the existing key grab information from the list */
void
e_keyrouter_find_and_remove_client_from_list(E_Client *ec, struct wl_client *wc, uint32_t key, uint32_t mode)
{
   Eina_List *l = NULL, *l_next = NULL;
   E_Keyrouter_Key_List_NodePtr key_node_data = NULL;

   switch (mode)
     {
        case TIZEN_KEYROUTER_MODE_EXCLUSIVE:
           E_KEYROUTER_REMOVE_KEY_NODE_IN_LIST(krt->HardKeys[key].excl_ptr, ec, wc, l, l_next, key_node_data, key, "Exclusive");
           break;

        case TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE:
           E_KEYROUTER_REMOVE_KEY_NODE_IN_LIST(krt->HardKeys[key].or_excl_ptr, ec, wc, l, l_next, key_node_data, key, "OR_Exclusive");
           break;

        case TIZEN_KEYROUTER_MODE_TOPMOST:
           E_KEYROUTER_REMOVE_KEY_NODE_IN_LIST(krt->HardKeys[key].top_ptr, ec, wc, l, l_next, key_node_data, key, "Top Position");
           break;

        case TIZEN_KEYROUTER_MODE_SHARED:
           E_KEYROUTER_REMOVE_KEY_NODE_IN_LIST(krt->HardKeys[key].shared_ptr, ec, wc, l, l_next, key_node_data, key, "Shared");
           break;

        default:
           KLDBG("Unknown key(%d) and grab mode(%d)\n", key, mode);
           return;
     }
}

void
e_keyrouter_remove_client_from_list(E_Client *ec, struct wl_client *wc)
{
   int index = 0;
   if (!ec && !wc)
     {
        return;
     }

   for (index = 0; index < MAX_HWKEYS; index++)
     {
        if (0 == krt->HardKeys[index].keycode)
          {
             continue;
          }

        /* exclusive grab */
        e_keyrouter_find_and_remove_client_from_list(ec, wc, index, TIZEN_KEYROUTER_MODE_EXCLUSIVE);

        /* or exclusive grab */
        e_keyrouter_find_and_remove_client_from_list(ec, wc, index, TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE);

        /* top position grab */
        e_keyrouter_find_and_remove_client_from_list(ec, wc, index, TIZEN_KEYROUTER_MODE_TOPMOST);

        /* shared grab */
        e_keyrouter_find_and_remove_client_from_list(ec, wc, index, TIZEN_KEYROUTER_MODE_SHARED);
     }
}
