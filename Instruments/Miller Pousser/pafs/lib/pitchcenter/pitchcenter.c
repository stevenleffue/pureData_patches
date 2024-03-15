#include "m_pd.h"
#include <stdlib.h>

/* pitchcenter -- pitchcenter incoming numbers toward a given set of numbers */

static t_class *pitchcenter_class;

typedef struct _pitchcenter
{
    t_object x_obj;
    t_outlet *x_outlet;
    int x_n;
    t_float *x_vec;
    float x_tolerance;
} t_pitchcenter;

static void *pitchcenter_new(void)
{
    t_pitchcenter *x = (t_pitchcenter *)pd_new(pitchcenter_class);
    x->x_outlet = outlet_new(&x->x_obj, &s_float);
    x->x_n = 0;
    x->x_vec = (t_float *)getbytes(0);
    x->x_tolerance = 0;
    return (x);
}

static void pitchcenter_float(t_pitchcenter *x, t_floatarg f)
{
    int i, bestindex = -1;
    float besterror = 1e10;
    for (i = 0; i < x->x_n; i++)
    {
        float thiserror = (f < x->x_vec[i] ? x->x_vec[i] - f :
            f - x->x_vec[i]);
        if (thiserror < besterror)
            besterror = thiserror, bestindex = i;
    }
    besterror -= x->x_tolerance;
    post("besterr %f", besterror);
    if (besterror < 0)
        goto bash;
    else if (besterror < 1)
    {
        int r = random();
        if (r < RAND_MAX * (1-besterror))
            goto bash;
    }
    outlet_float(x->x_outlet, f);
    return;
bash:
    outlet_float(x->x_outlet, x->x_vec[bestindex]);
}

static void pitchcenter_tolerance(t_pitchcenter *x, t_floatarg f)
{
    x->x_tolerance = f;
}

static void pitchcenter_set(t_pitchcenter *x, t_symbol *s, int argc,
    t_atom *argv)
{
    int i;
    x->x_vec = (float *)t_resizebytes(x->x_vec,
        x->x_n*sizeof(*x->x_vec), argc*sizeof(*x->x_vec));
    for (i = 0; i < argc; i++)
        x->x_vec[i] = atom_getfloat(&argv[i]);
    x->x_n = argc;
}

static void pitchcenter_free(t_pitchcenter *x)
{
    t_freebytes(x->x_vec, x->x_n * sizeof(*x->x_vec));
}


void pitchcenter_setup(void)
{
    pitchcenter_class = class_new(gensym("pitchcenter"), pitchcenter_new, 
        (t_method)pitchcenter_free, sizeof(t_pitchcenter), 0, 0);
    class_addfloat(pitchcenter_class, (t_method)pitchcenter_float);
    class_addmethod(pitchcenter_class, (t_method)pitchcenter_set,
        gensym("set"), A_GIMME, 0);
    class_addmethod(pitchcenter_class, (t_method)pitchcenter_tolerance,
        gensym("tolerance"), A_FLOAT, 0);
}
