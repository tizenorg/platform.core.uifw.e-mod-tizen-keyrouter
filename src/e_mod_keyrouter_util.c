#define E_COMP_WL
#include "e_mod_main_wl.h"

struct wl_resource *
e_keyrouter_util_get_surface_from_eclient(E_Client *client)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL
     (client, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL
     (client->comp_data, NULL);

   return client->comp_data->wl_surface;
}

int
e_keyrouter_util_get_pid(struct wl_client *client, struct wl_resource *surface)
{
   pid_t pid = 0;
   uid_t uid = 0;
   gid_t gid = 0;
   struct wl_client *cur_client = NULL;

   if (client) cur_client = client;
   else if (surface) cur_client = wl_resource_get_client(surface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cur_client, 0);

   wl_client_get_credentials(cur_client, &pid, &uid, &gid);

   return pid;
}

typedef struct _keycode_map{
    xkb_keysym_t keysym;
    xkb_keycode_t keycode;
}keycode_map;

static void                                                                   
find_keycode(struct xkb_keymap *keymap, xkb_keycode_t key, void *data)
{
   keycode_map *found_keycodes = (keycode_map *)data;
   xkb_keysym_t keysym = found_keycodes->keysym;
   int nsyms = 0;
   const xkb_keysym_t *syms_out = NULL;

   nsyms = xkb_keymap_key_get_syms_by_level(keymap, key, 0, 0, &syms_out);
   if (nsyms && syms_out)
     {
        if (*syms_out == keysym)
          {
             found_keycodes->keycode = key;
          }
     }
}

int                                                                           
_e_keyrouter_keycode_get_from_keysym(struct xkb_keymap *keymap, xkb_keysym_t keysym)
{
   keycode_map found_keycodes = {0,};
   found_keycodes.keysym = keysym;
   xkb_keymap_key_for_each(keymap, find_keycode, &found_keycodes);

   return found_keycodes.keycode;
}

int
e_keyrouter_util_keycode_get_from_string(char * name)
{
   struct xkb_keymap *keymap = NULL;
   xkb_keysym_t keysym = 0x0;
   int keycode = 0;

   keymap = e_comp_wl->xkb.keymap;
   EINA_SAFETY_ON_NULL_GOTO(keymap, finish);

   keysym = xkb_keysym_from_name(name, XKB_KEYSYM_NO_FLAGS);
   EINA_SAFETY_ON_FALSE_GOTO(keysym != XKB_KEY_NoSymbol, finish);

   keycode = _e_keyrouter_keycode_get_from_keysym(keymap, keysym);

   KLERR("request name: %s, return value: %d\n", name, keycode);

   return keycode;

finish:
   return 0;
}