#include <stdio.h>
#include "m_pd.h"
/* dog -- open a dialog with fields for a name and number */

static t_class *dog_class;

typedef struct _dog
{
    t_object x_obj;
    t_symbol *x_binding;
    t_symbol *x_name;
    t_symbol **x_fields;
    int x_nfields;
} t_dog;

static void *dog_new(t_symbol *s, int argc, t_atom *argv)
{
    char buf[80];
    int i;
    t_dog *x = (t_dog *)pd_new(dog_class);
    outlet_new(&x->x_obj, gensym("list"));
    if (!*(x->x_name = atom_getsymbolarg(0, argc, argv))->s_name)
        x->x_name = gensym("dialog-widow");
    if (argc > 1)
    {
        x->x_nfields = argc-1;
        x->x_fields = (t_symbol **)getbytes(x->x_nfields * sizeof(*x->x_fields));
        for (i = 0; i < argc-1; i++)
        {
            if (!(x->x_fields[i] = atom_getsymbolarg(i+1, argc, argv)))
                x->x_fields[i] = gensym("field");   
        }
    }
    else x->x_nfields = 0, x->x_fields = (t_symbol **)getbytes(0);  
    sprintf(buf, "dog%lx", (t_int)x);
    x->x_binding = gensym(buf);
    pd_bind(&x->x_obj.ob_pd, x->x_binding);
    return (x);
}

static void dog_done(t_dog *x, t_symbol *s, int argc, t_atom *argv)
{
    outlet_list(x->x_obj.ob_outlet, 0, argc, argv);
}

static void dog_list(t_dog *x, t_symbol *s, int argc, t_atom *argv)
{
    int i;
    gfxstub_deleteforkey(x);
    char cmdbuf[MAXPDSTRING];
    sys_gui("global ddd_fields; set ddd_fields {}\n");
    for (i = 0; i < x->x_nfields; i++)
    {
        if (i >= argc || argv[i].a_type != A_FLOAT)
            sys_vgui("lappend ddd_fields {%s {%s}}\n",
                x->x_fields[i]->s_name, atom_getsymbolarg(i, argc, argv)->s_name);
        else sys_vgui("lappend ddd_fields {%s {%g}}\n",
                x->x_fields[i]->s_name, atom_getfloatarg(i, argc, argv));
    }
    sprintf(cmdbuf, "ddd_dialog %%s %s\n", x->x_name->s_name);
    gfxstub_new(&x->x_obj.ob_pd, x, cmdbuf);
}

static void dog_free(t_dog *x)
{
    pd_unbind(&x->x_obj.ob_pd, x->x_binding);
    gfxstub_deleteforkey(x);
    freebytes(x->x_fields, x->x_nfields * sizeof(*x->x_fields));
}

void dog_setup(void)
{
    dog_class = class_new(gensym("dog"), (t_newmethod)dog_new,
    	(t_method)dog_free, sizeof(t_dog), 0, A_GIMME, 0);
    class_addlist(dog_class, dog_list);
    class_addmethod(dog_class, (t_method)dog_done, gensym("done"), A_GIMME, 0);
}

