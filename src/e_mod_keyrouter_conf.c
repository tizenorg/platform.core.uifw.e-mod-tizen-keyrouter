#define E_COMP_WL
#include "e_mod_main_wl.h"

void
e_keyrouter_conf_init(E_Keyrouter_Config_Data *kconfig)
{
   kconfig->conf_hwkeys_edd = E_CONFIG_DD_NEW("E_Keyrouter_Config_Key",
                                               E_Keyrouter_Tizen_HWKey);
#undef T
#undef D
#define T E_Keyrouter_Tizen_HWKey
#define D kconfig->conf_hwkeys_edd
   E_CONFIG_VAL(D, T, name, STR);
   E_CONFIG_VAL(D, T, keycode, INT);
   E_CONFIG_VAL(D, T, no_privcheck, INT);
   E_CONFIG_VAL(D, T, repeat, INT);
   E_CONFIG_VAL(D, T, display_filter, INT);

   kconfig->conf_modkeys_list_edd = E_CONFIG_DD_NEW("E_Keyrouter_Modkey_Name",
                                          E_Keyrouter_Modkey_Name);
#undef T
#undef D
#define T E_Keyrouter_Modkey_Name
#define D kconfig->conf_modkeys_list_edd
   E_CONFIG_VAL(D, T, name, STR);

   kconfig->conf_modkeys_edd = E_CONFIG_DD_NEW("E_Keyrouter_Modifier_Key",
                                               E_Keyrouter_Modkey);
#undef T
#undef D
#define T E_Keyrouter_Modkey
#define D kconfig->conf_modkeys_edd
   E_CONFIG_LIST(D, T, ModKeys, kconfig->conf_modkeys_list_edd);
   E_CONFIG_VAL(D, T, num_modkeys, INT);
   E_CONFIG_VAL(D, T, combination, UCHAR);
   E_CONFIG_VAL(D, T, press_only, UCHAR);
   E_CONFIG_VAL(D, T, action, INT);

   kconfig->conf_edd = E_CONFIG_DD_NEW("Keyrouter_Config", E_Keyrouter_Conf_Edd);
#undef T
#undef D
#define T E_Keyrouter_Conf_Edd
#define D kconfig->conf_edd
   E_CONFIG_VAL(D, T, num_keycode, INT);
   E_CONFIG_VAL(D, T, max_keycode, INT);
   E_CONFIG_LIST(D, T, KeyList, kconfig->conf_hwkeys_edd);
   E_CONFIG_LIST(D, T, ModifierList, kconfig->conf_modkeys_edd);
   E_CONFIG_VAL(D, T, num_modifier_keys, INT);
   E_CONFIG_VAL(D, T, display_key_filter_mode, INT);

#undef T
#undef D
   kconfig->conf = e_config_domain_load("module.keyrouter", kconfig->conf_edd);

   if (!kconfig->conf)
     {
        KLDBG("Failed to find module.keyrouter config file.\n");
     }
}

void
e_keyrouter_conf_deinit(E_Keyrouter_Config_Data *kconfig)
{
   if (kconfig->conf)
     {
        E_FREE_LIST(kconfig->conf->KeyList, free);
        free(kconfig->conf);
     }

   E_CONFIG_DD_FREE(kconfig->conf_hwkeys_edd);
   E_CONFIG_DD_FREE(kconfig->conf_edd);
}
