#include <e.h>

typedef struct _E_Keyrouter_Module E_Keyrouter_Module;

struct _E_Keyrouter_Module
{
   E_Module_Api        *api;

   Eina_Stringshare    *name;
   Eina_Stringshare    *dir;
   void                *handle;

   struct {
      void * (*init)        (E_Keyrouter_Module *m);
      void    (*shutdown)    (E_Keyrouter_Module *m);
      int    (*hook)        (int type, void *event);
   } func;

   Eina_Bool        enabled : 1;
   Eina_Bool        error : 1;

   /* the module is allowed to modify these */
   void                *data;
};
