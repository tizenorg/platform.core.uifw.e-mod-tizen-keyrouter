#define E_COMP_WL
#include "e_mod_main_wl.h"

static void _e_keyrouter_dbus_msg_send(DBusConnection *bus, int action);

static unsigned int
_calc_pressed_keys(int max)
{
   int i;
   unsigned int res = 0x0;

   for (i = 0; i < max; i++)
     {
        res |= (1 << i);
     }
   return res;
}

void
e_keyrouter_modkey_mod_clean_up(void)
{
   Eina_List *l;
   E_Keyrouter_Modkey_Data *mdata;

   if (krt->modkey_timer) ecore_timer_del(krt->modkey_timer);
   krt->modkey_timer = NULL;

   EINA_LIST_FOREACH(krt->modkey_list, l, mdata)
     {
        mdata->pressed = 0x0;
     }
}

static void
_e_keyrouter_modkey_step_clean_up(void)
{
   Eina_List *l;
   E_Keyrouter_Stepkey_Data *sdata;

   if (krt->stepkey_timer) ecore_timer_del(krt->stepkey_timer);
   krt->stepkey_timer = NULL;

   EINA_LIST_FOREACH(krt->stepkey_list, l, sdata)
     {
        sdata->idx = 0;
     }
}

static Eina_Bool
_e_keyrouter_modkey_mod_timer(void *data)
{
   e_keyrouter_modkey_mod_clean_up();
   return ECORE_CALLBACK_CANCEL;
}

static Eina_Bool
_e_keyrouter_modkey_step_timer(void *data)
{
   _e_keyrouter_modkey_step_clean_up();
   return ECORE_CALLBACK_CANCEL;
}

void
e_keyrouter_modkey_check(Ecore_Event_Key *ev)
{
   Eina_List *l;
   E_Keyrouter_Modkey_Data *data;
   int i;

   EINA_LIST_FOREACH(krt->modkey_list, l, data)
     {
        for (i = 0; i < data->num_keys; i++)
          {
             if (ev->keycode == data->keycode[i])
               {
                  data->pressed |= (1 << i);
                  if (krt->modkey_timer) ecore_timer_del(krt->modkey_timer);
                  krt->modkey_timer = ecore_timer_add(krt->modkey_delay, _e_keyrouter_modkey_mod_timer, NULL);
                  break;
               }
          }

        if (data->pressed == _calc_pressed_keys(data->num_keys))
          {
             if (krt->modkey_timer) ecore_timer_del(krt->modkey_timer);
             krt->modkey_timer = NULL;
          }
     }
}

void
e_keyrouter_stepkey_check(Ecore_Event_Key *ev)
{
   Eina_List *l;
   E_Keyrouter_Stepkey_Data *data;

   EINA_LIST_FOREACH(krt->stepkey_list, l, data)
     {
        if (data->keycode[data->idx] == ev->keycode)
          {
             data->idx++;
             if (data->idx == data->num_keys)
               {
                  _e_keyrouter_dbus_msg_send(krt->dbus.conn, data->action);
               }
          }
        else
          {
             data->idx = 0;
          }
     }

   if (krt->stepkey_timer) ecore_timer_del(krt->stepkey_timer);
   krt->stepkey_timer = ecore_timer_add(krt->stepkey_delay, _e_keyrouter_modkey_step_timer, NULL);
}

static void
_e_keyrouter_modkey_mod_init(Eina_List *list_keys, int num_keys, Eina_Bool press_only)
{
   E_Keyrouter_Modkey_Data *data;
   Eina_List *l;
   E_Keyrouter_Modkey_Name *ndata;
   int i = 0;

   EINA_SAFETY_ON_NULL_RETURN(list_keys);
   EINA_SAFETY_ON_TRUE_RETURN(num_keys == 0);

   data = E_NEW(E_Keyrouter_Modkey_Data, 1);
   EINA_SAFETY_ON_NULL_RETURN(data);

   data->keycode = E_NEW(int, num_keys);

   EINA_LIST_FOREACH(list_keys, l, ndata)
     {
        data->keycode[i++] = e_keyrouter_util_keycode_get_from_string(ndata->name);
        if (i >= num_keys) break;
     }
   data->num_keys = num_keys;
   data->press_only = press_only;

   krt->modkey_list = eina_list_append(krt->modkey_list, data);
}

static void
_e_keyrouter_modkey_step_init(Eina_List *list_keys, int num_keys, int action)
{
   E_Keyrouter_Stepkey_Data *data;
   Eina_List *l;
   E_Keyrouter_Modkey_Name *ndata;
   int i = 0;

   EINA_SAFETY_ON_NULL_RETURN(list_keys);
   EINA_SAFETY_ON_TRUE_RETURN(num_keys == 0);

   data = E_NEW(E_Keyrouter_Stepkey_Data, 1);
   EINA_SAFETY_ON_NULL_RETURN(data);

   data->keycode = E_NEW(int, num_keys);

   EINA_LIST_FOREACH(list_keys, l, ndata)
     {
        data->keycode[i++] = e_keyrouter_util_keycode_get_from_string(ndata->name);
        if (i >= num_keys) break;
     }
   data->num_keys = num_keys;
   data->action = action;

   krt->stepkey_list= eina_list_append(krt->stepkey_list, data);
}

static void
_e_keyrouter_dbus_msg_send(DBusConnection *bus, int action)
{
    DBusMessage *msg = NULL;
    int input = action;

   EINA_SAFETY_ON_NULL_RETURN(bus);
    
    msg = dbus_message_new_signal(krt->dbus.path, krt->dbus.interface, krt->dbus.msg);
    dbus_message_append_args(msg, DBUS_TYPE_INT32, &input, DBUS_TYPE_INVALID);

    if (!dbus_connection_send(bus, msg, NULL))
      {
         KLWRN("Failed to send dbus message!\n");
      }
    KLINF("Send dbus msg: action: %d\n", action);
    dbus_message_unref(msg);
}

static void
_e_keyrouter_dbus_init(void)
{
   DBusError dbus_error;

   snprintf(krt->dbus.path, strlen(DBUS_PATH)+1, DBUS_PATH);
   snprintf(krt->dbus.interface, strlen(DBUS_IFACE)+1, DBUS_IFACE);
   snprintf(krt->dbus.msg, strlen(DBUS_MSG_NAME)+1, DBUS_MSG_NAME);

   dbus_error_init(&dbus_error);
   krt->dbus.conn = dbus_bus_get(DBUS_BUS_SYSTEM, &dbus_error);
   if(dbus_error_is_set(&dbus_error))
     {
        KLWRN("Failed to set dbus error: %s \n",dbus_error.message);
        goto error;
     }
   if (!krt->dbus.conn)
     {
        KLWRN("Failed to connect DBus\n");
        goto error;
     }

   return;
error:
   dbus_error_free(&dbus_error);
}

void
e_keyrouter_modkey_init(void)
{
   E_Keyrouter_Modkey *mdata;
   Eina_List *l;
   E_Keyrouter_Conf_Edd *kconf = krt->conf->conf;

   EINA_LIST_FOREACH(kconf->ModifierList, l, mdata)
     {
        if (mdata->combination == EINA_TRUE)
          {
             _e_keyrouter_modkey_step_init(mdata->ModKeys, mdata->num_modkeys, mdata->action);
          }
        else
          {
             _e_keyrouter_modkey_mod_init(mdata->ModKeys, mdata->num_modkeys, mdata->press_only);
          }
     }

   krt->modkey_delay = 0.5;
   krt->modkey_duration = 2;
   krt->stepkey_delay = 1;

   _e_keyrouter_dbus_init();
}

