/* Copyright (c) 1997-1999 Miller Puckette and others.
* For information on usage and redistribution, and for a DISCLAIMER OF ALL
* WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* The text object, an expanded qlist that uses Czaja's editor window support.
    dolist:
        reentrancy protection again
        embedding and read/write functions for krzyszfile_new()
 */


#include "m_pd.h"
#include "file.h"
#include <string.h>
#ifdef UNISTD
#include <unistd.h>
#endif
#ifdef MSW
#include <io.h>
#endif

typedef struct _txt
{
    t_object x_ob;
    t_outlet *x_bangout;
    void *x_binbuf;
    int x_onset;                /* playback position */
    t_clock *x_clock;
    t_float x_tempo;
    double x_whenclockset;
    t_float x_clockdelay;
    int x_automatic;
    t_symbol *x_dir;
    t_canvas *x_canvas;
    t_krzyszfile *x_krzyszfile;
} t_txt;

static void txt_tick(t_txt *x);
static void txt_hammerupdate(t_pd *z, t_symbol *s, int argc, t_atom *argv);

static t_class *txt_class;

static void *txt_new( void)
{
    t_symbol *name, *filename = 0;
    t_txt *x = (t_txt *)pd_new(txt_class);
    x->x_binbuf = binbuf_new();
    x->x_clock = clock_new(x, (t_method)txt_tick);
    outlet_new(&x->x_ob, &s_list);
    x->x_bangout = outlet_new(&x->x_ob, &s_bang);
    x->x_onset = 0;
    x->x_tempo = 1;
    x->x_whenclockset = 0;
    x->x_clockdelay = 0;
    x->x_automatic = 0;
    x->x_canvas = canvas_getcurrent();
    x->x_krzyszfile = krzyszfile_new((t_pd *)x, 0, 0, 0, txt_hammerupdate);
    return (x);
}

static void txt_rewind(t_txt *x)
{
    x->x_onset = 0;
    clock_unset(x->x_clock);
    x->x_whenclockset = 0;
}

static void txt_donext(t_txt *x, int drop)
{
    t_pd *target = 0;
    while (1)
    {
        int argc = binbuf_getnatom(x->x_binbuf),
            count, onset = x->x_onset, onset2;
        t_atom *argv = binbuf_getvec(x->x_binbuf);
        t_atom *ap = argv + onset, *ap2;
        if (onset >= argc) goto end;
        while (ap->a_type == A_SEMI || ap->a_type == A_COMMA)
        {
            if (ap->a_type == A_SEMI) target = 0;
            onset++, ap++;
            if (onset >= argc) goto end;
        }

        if (!target && ap->a_type == A_FLOAT)
        {
            ap2 = ap + 1;
            onset2 = onset + 1;
            while (onset2 < argc && ap2->a_type == A_FLOAT)
                onset2++, ap2++;
            x->x_onset = onset2;
            if (x->x_automatic)
            {
                clock_delay(x->x_clock,
                    x->x_clockdelay = ap->a_w.w_float * x->x_tempo);
                x->x_whenclockset = clock_getsystime();
            }
            else outlet_list(x->x_ob.ob_outlet, 0, onset2-onset, ap);
            return;
        }
        ap2 = ap + 1;
        onset2 = onset + 1;
        while (onset2 < argc &&
            (ap2->a_type == A_FLOAT || ap2->a_type == A_SYMBOL))
                onset2++, ap2++;
        x->x_onset = onset2;
        count = onset2 - onset;
        if (!target)
        {
            if (ap->a_type != A_SYMBOL) continue;
            else if (!(target = ap->a_w.w_symbol->s_thing))
            {
                pd_error(x, "txt: %s: no such object",
                    ap->a_w.w_symbol->s_name);
                continue;
            }
            ap++;
            onset++;
            count--;
            if (!count) 
            {
                x->x_onset = onset2;
                continue;
            }
        }
        if (!drop)
        {   
            if (ap->a_type == A_FLOAT)
                typedmess(target, &s_list, count, ap);
            else if (ap->a_type == A_SYMBOL)
                typedmess(target, ap->a_w.w_symbol, count-1, ap+1);
        }
    }  /* while (1); never falls through */

end:
    outlet_bang(x->x_bangout);
    x->x_whenclockset = 0;
}

static void txt_next(t_txt *x, t_floatarg drop)
{
    x->x_automatic = 0;
    txt_donext(x, drop != 0);
}

static void txt_start(t_txt *x)
{
    txt_rewind(x);
    x->x_automatic = 1;
    txt_donext(x, 0);
}

static void txt_tick(t_txt *x)
{
    x->x_automatic = 1;
    x->x_whenclockset = 0;
    txt_donext(x, 0);
}

static void txt_add(t_txt *x, t_symbol *s, int ac, t_atom *av)
{
    t_atom a;
    SETSEMI(&a);
    binbuf_add(x->x_binbuf, ac, av);
    binbuf_add(x->x_binbuf, 1, &a);
}

static void txt_add2(t_txt *x, t_symbol *s, int ac, t_atom *av)
{
    binbuf_add(x->x_binbuf, ac, av);
}

static void txt_clear(t_txt *x)
{
    txt_rewind(x);
    binbuf_clear(x->x_binbuf);
}

static void txt_set(t_txt *x, t_symbol *s, int ac, t_atom *av)
{
    txt_clear(x);
    txt_add(x, s, ac, av);
}

static void txt_read(t_txt *x, t_symbol *filename, t_symbol *format)
{
    int cr = 0;
    if (!strcmp(format->s_name, "cr"))
        cr = 1;
    else if (*format->s_name)
        pd_error(x, "txt_read: unknown flag: %s", format->s_name);

    if (binbuf_read_via_canvas(x->x_binbuf, filename->s_name, x->x_canvas, cr))
            pd_error(x, "%s: read failed", filename->s_name);
    txt_rewind(x);
}

static void txt_write(t_txt *x, t_symbol *filename, t_symbol *format)
{
    int cr = 0;
    char buf[MAXPDSTRING];
    canvas_makefilename(x->x_canvas, filename->s_name,
        buf, MAXPDSTRING);
    if (!strcmp(format->s_name, "cr"))
        cr = 1;
    else if (*format->s_name)
        pd_error(x, "txt_read: unknown flag: %s", format->s_name);
    if (binbuf_write(x->x_binbuf, buf, "", cr))
            pd_error(x, "%s: write failed", filename->s_name);
}

static void txt_print(t_txt *x)
{
    post("--------- textfile or txt contents: -----------");
    binbuf_print(x->x_binbuf);
}

static void txt_tempo(t_txt *x, t_float f)
{
    t_float newtempo;
    if (f < 1e-20) f = 1e-20;
    else if (f > 1e20) f = 1e20;
    newtempo = 1./f;
    if (x->x_whenclockset != 0)
    {
        t_float elapsed = clock_gettimesince(x->x_whenclockset);
        t_float left = x->x_clockdelay - elapsed;
        if (left < 0) left = 0;
        left *= newtempo / x->x_tempo;
        clock_delay(x->x_clock, left);
    }
    x->x_tempo = newtempo;
}

static void txt_bang(t_txt *x)
{
    int argc = binbuf_getnatom(x->x_binbuf),
        count, onset = x->x_onset, onset2;
    t_atom *argv = binbuf_getvec(x->x_binbuf);
    t_atom *ap = argv + onset, *ap2;
    while (onset < argc &&
        (ap->a_type == A_SEMI || ap->a_type == A_COMMA))
            onset++, ap++;
    onset2 = onset;
    ap2 = ap;
    while (onset2 < argc &&
        (ap2->a_type != A_SEMI && ap2->a_type != A_COMMA))
            onset2++, ap2++;
    if (onset2 > onset)
    {
        x->x_onset = onset2;
        if (ap->a_type == A_SYMBOL)
            outlet_anything(x->x_ob.ob_outlet, ap->a_w.w_symbol,
                onset2-onset-1, ap+1);
        else outlet_list(x->x_ob.ob_outlet, 0, onset2-onset, ap);
    }
    else
    {
        x->x_onset = 0x7fffffff;
        outlet_bang(x->x_bangout);
    }
}

/* interface with Hammer text editor */
 
static void txt_open(t_txt *x)
{
    char *buf;
    int size;
    krzyszeditor_open(x->x_krzyszfile, "Text");
    binbuf_gettext(x->x_binbuf, &buf, &size);
    buf = t_resizebytes(buf, size, size+1);
    buf[size] = 0;
    krzyszeditor_append(x->x_krzyszfile, buf);
    t_freebytes(buf, size+1);
}

static void txt_close(t_txt *x)
{
    krzyszeditor_close(x->x_krzyszfile, 1);
}

static void txt_hammerupdate(t_pd *z, t_symbol *s, int argc, t_atom *argv)
{
    t_txt *x = (t_txt *)z;
    binbuf_clear(x->x_binbuf);
    binbuf_add(x->x_binbuf, argc, argv);
    txt_rewind(x);
}

static void txt_free(t_txt *x)
{
    krzyszfile_free(x->x_krzyszfile);
    binbuf_free(x->x_binbuf);
    if (x->x_clock) clock_free(x->x_clock);
}

/* ---------------- global setup function -------------------- */
void krzyszfile_setup(t_class *c, int embeddable);

void text_setup(void )
{
    txt_class = class_new(gensym("text"), (t_newmethod)txt_new,
        (t_method)txt_free, sizeof(t_txt), 0, 0);
    class_addmethod(txt_class, (t_method)txt_rewind, gensym("rewind"), 0);
    class_addmethod(txt_class, (t_method)txt_next,
        gensym("next"), A_DEFFLOAT, 0);  
    class_addmethod(txt_class, (t_method)txt_set, gensym("set"),
        A_GIMME, 0);
    class_addmethod(txt_class, (t_method)txt_clear, gensym("clear"), 0);
    class_addmethod(txt_class, (t_method)txt_add, gensym("add"),
        A_GIMME, 0);
    class_addmethod(txt_class, (t_method)txt_add2, gensym("add2"),
        A_GIMME, 0);
    class_addmethod(txt_class, (t_method)txt_add, gensym("append"),
        A_GIMME, 0);
    class_addmethod(txt_class, (t_method)txt_read, gensym("read"),
        A_SYMBOL, A_DEFSYM, 0);
    class_addmethod(txt_class, (t_method)txt_write, gensym("write"),
        A_SYMBOL, A_DEFSYM, 0);
    class_addmethod(txt_class, (t_method)txt_print, gensym("print"),
        A_DEFSYM, 0);
    class_addmethod(txt_class, (t_method)txt_start, gensym("start"), 0);
    class_addmethod(txt_class, (t_method)txt_tempo,
        gensym("tempo"), A_FLOAT, 0);
    class_addbang(txt_class, txt_bang);

    class_addmethod(txt_class, (t_method)txt_open, gensym("open"), 0);
    class_addmethod(txt_class, (t_method)txt_close, gensym("close"), 0);
    krzyszfile_setup(txt_class, 0);
}

