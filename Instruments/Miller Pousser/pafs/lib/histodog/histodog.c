#include "m_pd.h"
#include <stdlib.h>

/* histodog -- do a by-power histogram to find important pitches in 
recent history */

static t_class *histodog_class;

#define HISTORY 1000
#define LOPITCH 0
#define NPITCH 128
#define MAXOUT 20

typedef struct _snap
{
    float s_pit;
    float s_weight;
} t_snap;

typedef struct _histodog
{
    t_object x_obj;
    t_outlet *x_outlet;
    t_float x_f;
    t_snap x_snap[HISTORY];
    int x_histphase;
} t_histodog;

static void *histodog_new(void)
{
    int i;
    t_histodog *x = (t_histodog *)pd_new(histodog_class);
    x->x_outlet = outlet_new(&x->x_obj, &s_list);
    floatinlet_new(&x->x_obj, &x->x_f);
    x->x_histphase = 0;
    for (i = 0; i <HISTORY; i++)
        x->x_snap[i].s_pit = x->x_snap[i].s_weight = 0;
    return (x);
}

static void histodog_float(t_histodog *x, t_floatarg f)
{
    if (x->x_f > 0)
    {
        x->x_snap[x->x_histphase].s_weight = x->x_f;
        x->x_snap[x->x_histphase].s_pit = f;
    }
    else x->x_snap[x->x_histphase].s_weight = 
        x->x_snap[x->x_histphase].s_pit = 0;
    x->x_histphase++;
    if (x->x_histphase >= HISTORY)
        x->x_histphase = 0;
}

static void histodog_wtf(t_histodog *x, t_floatarg fnhist,
    t_floatarg fminout, t_floatarg fmaxout, t_floatarg minfrac)
{
    float histo[2*NPITCH+1], sum = 0;
    int nhist = fnhist;
    int i, j, argc;
    int histphase;
    int minout = fminout, maxout = fmaxout;
    t_atom argv[MAXOUT];
    if (maxout > MAXOUT)
        maxout = MAXOUT;
    if (maxout <= 0)
        return;
    if (minout > maxout)
        minout = maxout;
    if (minout < 0)
        minout = 0;
    if (nhist <= 0)
    {
        bug("histodog");
        return;
    }
    else if (nhist > HISTORY)
    {
        post("histodog: history %d truncated to %d", nhist, HISTORY);
        nhist = HISTORY;
    }
    for (i = 0; i < 2*NPITCH+1; i++)
        histo[i] = 0;
    for (i = 0, histphase = x->x_histphase; i < nhist; i++)
    {
        int bin;
        if (--histphase < 0)
            histphase = HISTORY-1;
        if (x->x_snap[histphase].s_weight <= 0 ||
            (bin = 2*(x->x_snap[histphase].s_pit-LOPITCH) + 1) < 1 ||
                bin >= 2*NPITCH+1)
                continue;
        sum += x->x_snap[histphase].s_weight;
        histo[bin] += x->x_snap[histphase].s_weight;
    }
    /* for (i = 0; i < 2*NPITCH+1; i++)
        post("%.3d %f", i, histo[i]); */
    for (argc = 0; argc < maxout; argc++)
    {
        float bestval = 0, out = 0;
        int bestindex = -1;
        for (j = 0; j < 2*NPITCH-1; j++)
        {
            float myval = 0.7*histo[j] + histo[j+1] + 0.7*histo[j+2];
            if (myval > bestval)
                bestval = myval, bestindex = j;
        }
        /* post("bestindex %d, bestval %f, sum %f, minfrac %f",
            bestindex, bestval, sum, minfrac); */
        if (bestindex < 0)
            break;
        if (bestval < minfrac * sum)
            break;
        if (bestindex & 1)
        {
            if (histo[bestindex] > histo[bestindex+2])
                 out = bestindex/2;
            else out = bestindex/2+1;
        }
        else out = bestindex/2;
        histo[bestindex] = histo[bestindex+1] = histo[bestindex+2] = 0;
        SETFLOAT(argv+argc, out);
    }
    outlet_list(x->x_outlet, &s_list, argc, argv);
}

static void histodog_clear(t_histodog *x)
{
    int i;
    for (i = 0; i <HISTORY; i++)
        x->x_snap[i].s_pit = x->x_snap[i].s_weight = 0;
    
}

#if 0
static void histodog_tolerance(t_histodog *x, t_floatarg f)
{
}

static void histodog_set(t_histodog *x, t_symbol *s, int argc,
    t_atom *argv)
{
}
#endif

void histodog_setup(void)
{
    histodog_class = class_new(gensym("histodog"), histodog_new, 
        0, sizeof(t_histodog), 0, 0);
    class_addfloat(histodog_class, histodog_float);
    class_addmethod(histodog_class, (t_method)histodog_wtf,
        gensym("wtf"), A_FLOAT, A_FLOAT, A_FLOAT, A_FLOAT, 0);
    class_addmethod(histodog_class, (t_method)histodog_clear,
        gensym("clear"), 0);
#if 0
    class_addmethod(histodog_class, (t_method)histodog_set,
        gensym("set"), A_GIMME, 0);
    class_addmethod(histodog_class, (t_method)histodog_tolerance,
        gensym("tolerance"), A_FLOAT, 0);
#endif
}
