/*

peaktracker.c - complicated frame-to-frame peak matching
copyright 2006 Miller Puckette.
Available under the BSD license (see "LICENCE.txt" in this distribution.)

*/

#include "m_pd.h"
/* #include <stdlib.h>
#include <stdio.h>
#include <string.h> */
#include <math.h>

#define MAXDIM 100
#define DEFAULTDIM 10

typedef struct _peak
{
    float p_freq;       /* frequency */
    float p_amp;        /* amplitude */
    int p_age;          /* zero if free; >0 if valid; <0 to count down  */
    float p_value;      /* length of note */
} t_peak;

typedef struct _peaktracker
{
    	    /* pd stuff */
    t_object x_ob;
    t_outlet *x_out2;
    	    /* state */
    int x_dim;              /* dimension of arrays below */                             
    t_peak *x_peakin;       /* incoming peaks */                              
    t_peak *x_peakout;      /* outgoing peaks */
    int x_gotnew;           /* number of new peaks received */
    	    /* parameters */
    float x_slewhz;
    float x_slewhalftones;
    int x_ntracks;          /* number of tracks to maintain and output */
    int x_rematch;          /* flag to enable "rematch" step */
    float x_stealdb;        /* dB new peaks exceed current ones to steal */
    float x_preferlofreq;   /* lowest frequency we want */
    float x_preferhifreq;   /* highest frequency we want */
    float x_lodbperoct;     /* dB per octave rolloff at low end */
    float x_hidbperoct;     /* dB per octave rolloff at high end */
    float x_inter1value;    /* value to add for partials within an interval */
    float x_inter1tones;    /* halftones in the interval */
    float x_inter1tolerance;/* tolerance in cents for the interval */
} t_peaktracker;

static t_class *peaktracker_class;

static void peaktracker_peakin(t_peaktracker *x, float freq, float amp)
{
    if (x->x_gotnew >= x->x_dim)
        pd_error(x, "peaktracker: too many partials");
    else
    {
        x->x_peakin[x->x_gotnew].p_freq = freq;
        x->x_peakin[x->x_gotnew].p_amp = amp;
        x->x_peakin[x->x_gotnew].p_age = 1;
        x->x_gotnew++;
    }
}

static void peaktracker_list(t_peaktracker *x, t_symbol *s,
    int argc, t_atom *argv)
{
    if (argc == 2)
        peaktracker_peakin(x, atom_getfloat(argv), atom_getfloat(argv+1));
    else if (argc == 3)
    {
        int n = atom_getfloat(argv);
        if (n != x->x_gotnew)
            pd_error(x, "peaktracker: got peaks out of order");
        else peaktracker_peakin(x, atom_getfloat(argv+1),
            atom_getfloat(argv+2));
    }
    else pd_error(x, "wrong number of args to list");
}

void peaktracker_clear(t_peaktracker *x)
{
    x->x_gotnew = 0;
}

static void peaktracker_spit(t_peaktracker *x)
{
    int i;
    t_atom at[4*MAXDIM], *ap;
    for (i = 0, ap = at; i < x->x_ntracks; i++, ap+=4)
    {
        SETFLOAT(ap, i);
        SETFLOAT(ap+1, x->x_peakout[i].p_age);
        SETFLOAT(ap+2, x->x_peakout[i].p_freq);
        SETFLOAT(ap+3, x->x_peakout[i].p_amp);
    }
    outlet_list(x->x_out2, 0, 4 * x->x_ntracks, at);
    for (i = 0; i < x->x_ntracks; i++)
    {
        SETFLOAT(at, i);
        SETFLOAT(at+1, x->x_peakout[i].p_age);
        SETFLOAT(at+2, x->x_peakout[i].p_freq);
        SETFLOAT(at+3, x->x_peakout[i].p_amp);
        outlet_list(x->x_ob.ob_outlet, 0, 4, at);
    }
}

static void peaktracker_snapshot(t_peaktracker *x)
{
    int i, j;
    float lopit = ftom(x->x_preferlofreq);
    float hipit = ftom(x->x_preferhifreq);
    for (i = 0; i < x->x_gotnew; i++)
    {
        float pit = ftom(x->x_peakin[i].p_freq);
        x->x_peakin[i].p_value = rmstodb(x->x_peakin[i].p_amp);
        if (pit < lopit)
            x->x_peakin[i].p_value -= 0.0833*(lopit-pit)*x->x_lodbperoct;
        if (pit > hipit)
            x->x_peakin[i].p_value -= 0.0833*(pit-hipit)*x->x_hidbperoct;
        x->x_peakin[i].p_age = 1;
    }
        /* enhance values for pairs of harmonics at a certain interval */
    if (x->x_inter1value != 0)
    {
        float midinterval = exp(0.057762 * x->x_inter1tones);
        float inter1tolerance = (x->x_inter1tolerance > 1 ? 
             x->x_inter1tolerance : 1);
        float tolerance = exp(0.057762 * 0.01 * inter1tolerance) - 1;
        float mininterval = midinterval * (1-tolerance);
        float maxinterval = midinterval * (1+tolerance);

        for (i = 0; i < x->x_gotnew; i++)
            for (j = 0; j < x->x_gotnew; j++)
        {
            float enhancement, interval;
            if (i >= j)
                continue;
            interval = (x->x_peakin[i].p_freq > x->x_peakin[j].p_freq ?
                x->x_peakin[i].p_freq / x->x_peakin[j].p_freq : 
                x->x_peakin[j].p_freq / x->x_peakin[i].p_freq);
            if (interval > maxinterval || interval < mininterval)
                continue;
            if (interval > midinterval)
                enhancement =
                    (maxinterval - interval)/(maxinterval-midinterval);
            else enhancement =
                    (mininterval - interval)/(mininterval-midinterval);
            if (enhancement > 1)
                enhancement = 1;
            else if (enhancement < 0)
                enhancement = 0;
            x->x_peakin[i].p_value += x->x_inter1value * enhancement;
            x->x_peakin[j].p_value += x->x_inter1value * enhancement;
            /* post("f %.2f value %.2f f %.2f value %.2f enhance %.2f",
                x->x_peakin[i].p_freq, x->x_peakin[i].p_value,
                x->x_peakin[j].p_freq, x->x_peakin[j].p_value,
                    enhancement); */
        }
    }
    for (j = 0; j < x->x_ntracks; j++)
    {
        float bestvalue = -1e20;
        int bestindex = -1;
        for (i = 0; i < x->x_gotnew; i++)
        {
            if (!x->x_peakin[i].p_age)
                continue;
            if (x->x_peakin[i].p_value > bestvalue)
                bestvalue = x->x_peakin[i].p_value, bestindex = i;
        }
        if (bestindex < 0)
            break;
        x->x_peakout[j].p_freq = x->x_peakin[bestindex].p_freq;
        x->x_peakout[j].p_amp = x->x_peakin[bestindex].p_amp;
        x->x_peakout[j].p_age = 1;
        x->x_peakin[bestindex].p_value = -1e21;
    }
    for (; j < x->x_ntracks; j++)
    {
        x->x_peakout[j].p_freq = x->x_peakout[j].p_amp = 0;
        x->x_peakout[j].p_age = 0;
    }
    peaktracker_spit(x);
}


static void peaktracker_start(t_peaktracker *x)
{
    peaktracker_snapshot(x);
    x->x_gotnew = 0;
}

static void peaktracker_doit(t_peaktracker *x)
{
    int i, j;
    float lopit = ftom(x->x_preferlofreq);
    float hipit = ftom(x->x_preferhifreq);
    for (j = 0; j < x->x_ntracks; j++)
    {
    }
    for (i = 0; i < x->x_gotnew; i++)
    {
        float pit = ftom(x->x_peakin[i].p_freq);
        float besterror = 1e20;
        int bestindex = -1;
        x->x_peakin[i].p_value = dbtorms(x->x_peakin[i].p_amp);
        if (pit < lopit)
            x->x_peakin[i].p_value -= 0.0833*(lopit-pit)*x->x_lodbperoct;
        if (pit > hipit)
            x->x_peakin[i].p_value -= 0.0833*(pit-hipit)*x->x_hidbperoct;
        
            /* go through the old peaks to find the best match */
        for (j = 0; j < x->x_ntracks; j++)
        {
            float distance;
            if (x->x_peakout[j].p_freq * x->x_peakin[i].p_freq <= 0)
                continue;
            distance = (x->x_peakout[j].p_freq > x->x_peakin[i].p_freq ?
                x->x_peakout[j].p_freq / x->x_peakin[i].p_freq :
                x->x_peakin[i].p_freq / x->x_peakout[j].p_freq);
            if (distance < besterror)
                besterror = distance, bestindex = j;
        }
            /* cancel the match if the distance is too great */
        if (bestindex >= 0)
        {
            float lo, hi;
            if (x->x_peakout[bestindex].p_freq < x->x_peakin[i].p_freq)
                lo=x->x_peakout[bestindex].p_freq, hi=x->x_peakin[i].p_freq;
            else lo=x->x_peakin[i].p_freq, hi=x->x_peakout[bestindex].p_freq;
            if ((lo + x->x_slewhz) * exp(0.057762 * x->x_slewhalftones)
                < hi)
                    bestindex = -1;
        }
        x->x_peakin[i].p_age = bestindex;
    }
#if 0
    for (i = 0; i < x->x_gotnew; i++)
        post("in a %d %f", x->x_peakin[i].p_age, x->x_peakin[i].p_freq);
#endif
        /* For each old peak keep only the best new one pointing to it */
    for (j = 0; j < x->x_ntracks; j++)
    {
        float besterror = 1e20;
        int bestindex = -1;
        for (i = 0; i < x->x_gotnew; i++)
        {
            float distance;
            if (x->x_peakin[i].p_age != j)
                continue;
            distance = (x->x_peakout[j].p_freq > x->x_peakin[i].p_freq ?
                x->x_peakout[j].p_freq / x->x_peakin[i].p_freq :
                x->x_peakin[i].p_freq / x->x_peakout[j].p_freq);
            if (distance < besterror)
                besterror = distance, bestindex = i;
        }
        if (bestindex < 0)
            continue;
        for (i = 0; i < x->x_gotnew; i++)
            if (x->x_peakin[i].p_age == j && i != bestindex)
                x->x_peakin[i].p_age = -1;        
    }
        /* use value field (inappropriately) as an "occupied" flag */
    for (j = 0; j < x->x_ntracks; j++)
        x->x_peakout[j].p_value = 0;
        /* copy new peaks onto old ones */
    for (i = 0; i < x->x_gotnew; i++)
    {
        if (x->x_peakin[i].p_age >= 0)
        {
            t_peak *p = &x->x_peakout[x->x_peakin[i].p_age];
            p->p_age++;
            p->p_freq = x->x_peakin[i].p_freq;
            p->p_amp = x->x_peakin[i].p_amp;
            p->p_value = 1;
        }
    }
        /* bring in unmatched peaks, using value to sort them */
    for (j = 0; j < x->x_ntracks; j++)
        if (x->x_peakout[j].p_value == 0)
    {
        float bestvalue = -1e20;
        int bestindex = -1;
        for (i = 0; i < x->x_gotnew; i++)
        {
                /* not if already matched */
            if (x->x_peakin[i].p_age >= 0)
                continue;
            if (x->x_peakin[i].p_value > bestvalue)
                bestvalue = x->x_peakin[i].p_value, bestindex = i;
        }
        if (bestindex >= 0)
        {
            x->x_peakout[j].p_value = 1;
            x->x_peakout[j].p_freq = x->x_peakin[bestindex].p_freq;
            x->x_peakout[j].p_amp = x->x_peakin[bestindex].p_amp;
            x->x_peakout[j].p_age = 1;
            x->x_peakin[bestindex].p_age = 1;
        }
        else
        {
            x->x_peakout[j].p_value = 0;
            x->x_peakout[j].p_freq = x->x_peakout[j].p_amp = 0;
            x->x_peakout[j].p_age = 0;
        }
    }
    peaktracker_spit(x);
    x->x_gotnew = 0;
}

static void peaktracker_slewhz(t_peaktracker *x, t_float f)
{
    x->x_slewhz = (f >= 0 ? f : 0);
}

static void peaktracker_slewhalftones(t_peaktracker *x, t_float f)
{
    x->x_slewhalftones = (f >= 0 ? f : 0);
}

static void peaktracker_stealdb(t_peaktracker *x, t_float f)
{
    x->x_stealdb = (f >= 0 ? f : 0);
}

static void peaktracker_rematch(t_peaktracker *x, t_float f)
{
    x->x_rematch = (f != 0);
    post("warning: rematch flag not implemented");
}

static void peaktracker_preferlofreq(t_peaktracker *x, t_float f)
{
    x->x_preferlofreq = (f >= 1 ? f : 1);
}

static void peaktracker_preferhifreq(t_peaktracker *x, t_float f)
{
    x->x_preferhifreq = (f >= 1 ? f : 1);
}

static void peaktracker_lodbperoct(t_peaktracker *x, t_float f)
{
    x->x_lodbperoct = (f >= 0 ? f : 0);
}

static void peaktracker_hidbperoct(t_peaktracker *x, t_float f)
{
    x->x_hidbperoct = (f >= 0 ? f : 0);
}

static void peaktracker_ntracks(t_peaktracker *x, t_float f)
{
    x->x_ntracks = (f >= 0 ? f : 0);
    if (x->x_ntracks > x->x_dim)
        x->x_ntracks = x->x_dim;
}

static void peaktracker_inter1value(t_peaktracker *x, t_float f)
{
    x->x_inter1value = f;
}

static void peaktracker_inter1halftones(t_peaktracker *x, t_float f)
{
    x->x_inter1tones = (f >= 0 ? f : 0);
}

static void peaktracker_inter1tolerance(t_peaktracker *x, t_float f)
{
    x->x_inter1tolerance = (f >= 0 ? f : 0);
}

static void peaktracker_print(t_peaktracker *x)
{
    post(
     "ntracks %d, slewhalftones %.2f, stealdb %.2f, slewhz %.2f, rematch %d,",
    	x->x_ntracks, x->x_slewhalftones, x->x_stealdb,
    	x->x_slewhz, x->x_rematch);
    post(
     "preferlofreq %.2f, lodbperoct %.2f, preferhifreq %.2f, hidbperoct %.2f",
    	x->x_preferlofreq, x->x_lodbperoct,
    	x->x_preferhifreq, x->x_hidbperoct);
    post(
     "inter1value %.2f, inter1tones %.2f, inter1tolerance %.2f",
    	x->x_inter1value, x->x_inter1tones, x->x_inter1tolerance);
}

static void peaktracker_free(t_peaktracker *x)
{
    freebytes(x->x_peakin, x->x_dim * sizeof(t_peak));
    freebytes(x->x_peakout, x->x_dim * sizeof(t_peak));
}

static void *peaktracker_new(t_symbol *s, t_floatarg dim)
{
    t_peaktracker *x = (t_peaktracker *)pd_new(peaktracker_class);
    outlet_new(&x->x_ob, &s_list);
    x->x_out2 = outlet_new(&x->x_ob, &s_list);
    x->x_dim = (dim < 1 ? DEFAULTDIM : (dim > MAXDIM ? MAXDIM: dim));
    x->x_peakin = (t_peak *)getbytes(x->x_dim * sizeof(t_peak));
    x->x_peakout = (t_peak *)getbytes(x->x_dim * sizeof(t_peak));
    x->x_slewhalftones = 1;
    x->x_slewhz = 0;
    x->x_stealdb = 50;
    x->x_preferlofreq = 1;
    x->x_preferhifreq = 1e6;
    x->x_lodbperoct = 6;
    x->x_hidbperoct = 6;
    x->x_ntracks = x->x_dim;
    x->x_inter1value = 0;
    x->x_inter1tones = 0;
    x->x_inter1tolerance = 1;
    return (x);
}

void peaktracker_setup(void)
{
    peaktracker_class = class_new(gensym("peaktracker"),
        (t_newmethod)peaktracker_new, (t_method)peaktracker_free,
	sizeof(t_peaktracker), 0, A_DEFFLOAT, A_DEFFLOAT, A_DEFSYMBOL, 0);
    class_addmethod(peaktracker_class, (t_method)peaktracker_start,
        gensym("start"), 0);
    class_addmethod(peaktracker_class, (t_method)peaktracker_snapshot,
        gensym("snapshot"), 0);
    class_addmethod(peaktracker_class, (t_method)peaktracker_doit,
        gensym("doit"), 0);
    class_addmethod(peaktracker_class, (t_method)peaktracker_print,
        gensym("print"), 0);
    class_addlist(peaktracker_class, peaktracker_list);
    class_addmethod(peaktracker_class, (t_method)peaktracker_clear,
        gensym("clear"), 0);
        
            /* methods to set parameters */
    class_addmethod(peaktracker_class, (t_method)peaktracker_slewhz,
        gensym("slewhz"), A_FLOAT, 0);
    class_addmethod(peaktracker_class, (t_method)peaktracker_slewhalftones,
        gensym("slewhalftones"), A_FLOAT, 0);
    class_addmethod(peaktracker_class, (t_method)peaktracker_stealdb,
        gensym("stealdb"), A_FLOAT, 0);
    class_addmethod(peaktracker_class, (t_method)peaktracker_rematch,
        gensym("rematch"), A_FLOAT, 0);
    class_addmethod(peaktracker_class, (t_method)peaktracker_preferlofreq,
        gensym("preferlofreq"), A_FLOAT, 0);
    class_addmethod(peaktracker_class, (t_method)peaktracker_preferhifreq,
        gensym("preferhifreq"), A_FLOAT, 0);
    class_addmethod(peaktracker_class, (t_method)peaktracker_lodbperoct,
        gensym("lodbperoct"), A_FLOAT, 0);
    class_addmethod(peaktracker_class, (t_method)peaktracker_hidbperoct,
        gensym("hidbperoct"), A_FLOAT, 0);
    class_addmethod(peaktracker_class, (t_method)peaktracker_ntracks,
        gensym("ntracks"), A_FLOAT, 0);
    class_addmethod(peaktracker_class, (t_method)peaktracker_inter1value,
        gensym("inter1value"), A_FLOAT, 0);
    class_addmethod(peaktracker_class, (t_method)peaktracker_inter1halftones,
        gensym("inter1halftones"), A_FLOAT, 0);
    class_addmethod(peaktracker_class, (t_method)peaktracker_inter1tolerance,
        gensym("inter1tolerance"), A_FLOAT, 0);
}

