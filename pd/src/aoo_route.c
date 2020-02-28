#include "m_pd.h"
#include "aoo/aoo.h"
#include <string.h>

static t_class *aoo_route_class;

typedef struct _aoo_route
{
    t_object x_obj;
    int x_n;
    t_outlet **x_outlets;
    int *x_ids;
    t_outlet *x_rejectout;
} t_aoo_route;

static void aoo_route_list(t_aoo_route *x, t_symbol *s, int argc, t_atom *argv)
{
    // copy address pattern string
    char buf[64];
    int success = 0;
    int i = 0;
    for (; i < argc && i < 63; ++i){
        char c = (int)atom_getfloat(argv + i);
        if (c){
            buf[i] = c;
        } else {
            break; // end of address pattern
        }
    }
    buf[i] = 0;

    // parse address pattern
    int32_t id = 0;
    if (aoo_parsepattern(buf, i, &id)){
        for (int j = 0; j < x->x_n; ++j){
            if (id == AOO_ID_WILDCARD || id == x->x_ids[j]){
                outlet_list(x->x_outlets[j], s, argc, argv);
                success = 1;
            }
        }
        if (success){
            return;
        }
    }
    // reject
    outlet_list(x->x_rejectout, s, argc, argv);
}

static void aoo_route_set(t_aoo_route *x, t_floatarg f)
{
    x->x_ids[0] = f;
}

static void * aoo_route_new(t_symbol *s, int argc, t_atom *argv)
{
    t_aoo_route *x = (t_aoo_route *)pd_new(aoo_route_class);

    x->x_n = (argc > 1) ? argc : 1;
    x->x_outlets = (t_outlet **)getbytes(sizeof(t_outlet *) * x->x_n);
    x->x_ids = (int *)getbytes(sizeof(int) * x->x_n);
    for (int i = 0; i < x->x_n; ++i){
        x->x_outlets[i] = outlet_new(&x->x_obj, 0);
        x->x_ids[i] = atom_getfloat(argv + i);
    }
    if (x->x_n == 1){
        inlet_new(&x->x_obj, &x->x_obj.ob_pd, gensym("float"), gensym("set"));
        if (!argc){
            x->x_ids[0] = AOO_ID_WILDCARD; // initially disabled
        }
    }

    x->x_rejectout = outlet_new(&x->x_obj, 0);

    return x;
}

static void aoo_route_free(t_aoo_route *x)
{
    freebytes(x->x_outlets, sizeof(t_outlet *) * x->x_n);
    freebytes(x->x_ids, sizeof(int) * x->x_n);
}

void aoo_route_setup(void)
{
    aoo_route_class = class_new(gensym("aoo_route"), (t_newmethod)(void *)aoo_route_new,
        (t_method)aoo_route_free, sizeof(t_aoo_route), 0, A_GIMME, A_NULL);
    class_addlist(aoo_route_class, (t_method)aoo_route_list);
    class_addmethod(aoo_route_class, (t_method)aoo_route_set, gensym("set"), A_FLOAT, A_NULL);

    aoo_setup();
}
