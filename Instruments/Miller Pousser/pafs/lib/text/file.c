/* Copyright (c) 2002-2003 krzYszcz and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* The three uses of the 'krzyszfile' proxy class are:
   1. providing `embedding' facility -- storing master object's state
   in a .pd file,
   2. encapsulating openpanel/savepanel management,
   3. extending the gui of Pd with a simple text editor window.

   A master class which needs embedding feature (like coll), passes
   a nonzero flag to the krzyszfile setup routine, and a nonzero embedfn
   function pointer to the krzyszfile constructor.  If a master needs
   access to the panels (like collcommon), then it passes nonzero readfn
   and/or writefn callback pointers to the constructor.  A master which has
   an associated text editor, AND wants to update object's state after
   edits, passes a nonzero updatefn callback in a call to the constructor.
*/

#include <stdio.h>
#include <string.h>
#include "m_pd.h"
#include "g_canvas.h"
#include "file.h"

char *class_gethelpdir(t_class *c);

static t_class *krzyszfile_class = 0;
static t_krzyszfile *krzyszfile_proxies;
static t_symbol *ps__C;

static t_krzyszfile *krzyszfile_getproxy(t_pd *master)
{
    t_krzyszfile *f;
    for (f = krzyszfile_proxies; f; f = f->f_next)
        if (f->f_master == master)
            return (f);
    return (0);
}


void krzyszeditor_open(t_krzyszfile *f, char *title)
{
    if (!title) title = class_getname(*f->f_master);
    sys_vgui("krzyszeditor_open .%x %dx%d {%s}\n", (unsigned long)f,
        600, 340, title);
}

static void krzyszeditor_tick(t_krzyszfile *f)
{
    sys_vgui("krzyszeditor_close .%x %d\n", (unsigned long)f, 1);
}

void krzyszeditor_close(t_krzyszfile *f, int ask)
{
    if (ask)
        /* hack: deferring modal dialog creation in order to allow for
           a message box redraw to happen -- LATER investigate */
        clock_delay(f->f_editorclock, 0);
    else
        sys_vgui("krzyszeditor_close .%x %d\n", (unsigned long)f, 0);
}

void krzyszeditor_append(t_krzyszfile *f, char *contents)
{
    if (!contents) contents = "";
    sys_vgui("krzyszeditor_append .%x {%s}\n", (unsigned long)f, contents);
}

static void krzyszeditor_clear(t_krzyszfile *f)
{
    if (f->f_editorfn)
    {
        if (f->f_binbuf)
            binbuf_clear(f->f_binbuf);
        else
            f->f_binbuf = binbuf_new();
    }
}

static void krzyszeditor_addline(t_krzyszfile *f,
                                 t_symbol *s, int ac, t_atom *av)
{
    if (f->f_editorfn)
    {
        int i;
        t_atom *ap;
        for (i = 0, ap = av; i < ac; i++, ap++)
        {
            if (ap->a_type == A_SYMBOL)
            {
                /* LATER rethink semi/comma mapping */
                if (!strcmp(ap->a_w.w_symbol->s_name, "_semi_"))
                    SETSEMI(ap);
                else if (!strcmp(ap->a_w.w_symbol->s_name, "_comma_"))
                    SETCOMMA(ap);
            }
        }
        binbuf_add(f->f_binbuf, ac, av);
    }
}

static void krzyszeditor_end(t_krzyszfile *f)
{
    if (f->f_editorfn)
    {
        (*f->f_editorfn)(f->f_master, 0, binbuf_getnatom(f->f_binbuf),
                         binbuf_getvec(f->f_binbuf));
        binbuf_clear(f->f_binbuf);
    }
}

static void krzyszpanel_symbol(t_krzyszfile *f, t_symbol *s)
{
    if (s && s != &s_ && f->f_panelfn)
        (*f->f_panelfn)(f->f_master, s, 0, 0);
}

static void krzyszpanel_tick(t_krzyszfile *f)
{
    if (f->f_savepanel)
        sys_vgui("krzyszpanel_open %s {%s}\n", f->f_bindname->s_name,
                 f->f_inidir->s_name);
    else
        sys_vgui("krzyszpanel_save %s {%s} {%s}\n", f->f_bindname->s_name,
                 f->f_inidir->s_name, f->f_inifile->s_name);
}

/* these are hacks: deferring modal dialog creation in order to allow for
   a message box redraw to happen -- LATER investigate */
void krzyszpanel_open(t_krzyszfile *f, t_symbol *inidir)
{
    f->f_inidir = (inidir ? inidir : &s_);
    clock_delay(f->f_panelclock, 0);
}

void krzyszpanel_save(t_krzyszfile *f, t_symbol *inidir, t_symbol *inifile)
{
    /* LATER ask if we can rely on s_ pointing to "" */
    f->f_savepanel->f_inidir = (inidir ? inidir : &s_);
    f->f_savepanel->f_inifile = (inifile ? inifile : &s_);
    clock_delay(f->f_savepanel->f_panelclock, 0);
}

/* Currently embeddable krzysz classes do not use the 'saveto' method.
   In order to use it, any embeddable class would have to add a creation
   method to pd_canvasmaker -- then saving could be done with a 'proper'
   sequence:  #N <master> <args>; #X <whatever>; ...; #X restore <x> <y>;
   However, this works only for -lib externals.  So, we choose a sequence:
   #X obj <x> <y> <master> <args>; #C <whatever>; ...; #C restore;
   Since the first message in this sequence is a valid creation message
   on its own, we have to distinguish loading from a .pd file, and other
   cases (editing). */

static void krzyszembed_gc(t_pd *x, t_symbol *s, int expected)
{
    t_pd *garbage;
    int count = 0;
    while (garbage = pd_findbyclass(s, *x)) pd_unbind(garbage, s), count++;
    if (count != expected)
        bug("krzyszembed_gc (%d garbage bindings)", count);
}

static void krzyszembed_restore(t_pd *master)
{
    krzyszembed_gc(master, ps__C, 1);
}

void krzyszembed_save(t_gobj *master, t_binbuf *bb)
{
    t_krzyszfile *f = krzyszfile_getproxy((t_pd *)master);
    t_text *t = (t_text *)master;
    binbuf_addv(bb, "ssii", &s__X, gensym("obj"),
                (int)t->te_xpix, (int)t->te_ypix);
    binbuf_addbinbuf(bb, t->te_binbuf);
    binbuf_addsemi(bb);
    if (f && f->f_embedfn)
        (*f->f_embedfn)(f->f_master, bb, ps__C);
    binbuf_addv(bb, "ss;", ps__C, gensym("restore"));
}

int krzyszfile_ismapped(t_krzyszfile *f)
{
    return (f->f_canvas->gl_mapped);
}

int krzyszfile_isloading(t_krzyszfile *f)
{
    return (f->f_canvas->gl_loading);
}

/* LATER find a better way */
int krzyszfile_ispasting(t_krzyszfile *f)
{
    int result = 0;
    t_canvas *cv = f->f_canvas;
    if (!cv->gl_loading)
    {
        t_pd *z = s__X.s_thing;
        if (z == (t_pd *)cv)
        {
            pd_popsym(z);
            if (s__X.s_thing == (t_pd *)cv) result = 1;
            pd_pushsym(z);
        }
        else if (z) result = 1;
    }
#if 0
    if (result) post("pasting");
#endif
    return (result);
}

void krzyszfile_free(t_krzyszfile *f)
{
    t_krzyszfile *prev, *next;
    krzyszeditor_close(f, 0);
    if (f->f_embedfn)
        /* just in case of missing 'restore' */
        krzyszembed_gc(f->f_master, ps__C, 0);
    if (f->f_savepanel)
    {
        pd_unbind((t_pd *)f->f_savepanel, f->f_savepanel->f_bindname);
        pd_free((t_pd *)f->f_savepanel);
    }
    if (f->f_bindname) pd_unbind((t_pd *)f, f->f_bindname);
    if (f->f_panelclock) clock_free(f->f_panelclock);
    if (f->f_editorclock) clock_free(f->f_editorclock);
    for (prev = 0, next = krzyszfile_proxies;
         next; prev = next, next = next->f_next)
        if (next == f)
            break;
    if (prev)
        prev->f_next = f->f_next;
    else if (f == krzyszfile_proxies)
        krzyszfile_proxies = f->f_next;
    pd_free((t_pd *)f);
}

t_krzyszfile *krzyszfile_new(t_pd *master, t_krzyszembedfn embedfn,
                             t_krzyszfilefn readfn, t_krzyszfilefn writefn,
                             t_krzyszfilefn updatefn)
{
    t_krzyszfile *result = (t_krzyszfile *)pd_new(krzyszfile_class);
    result->f_master = master;
    result->f_next = krzyszfile_proxies;
    krzyszfile_proxies = result;
    if (!(result->f_canvas = canvas_getcurrent()))
    {
        bug("krzyszfile_new: out of context");
        return (result);
    }

    /* 1. embedding */
    if (result->f_embedfn = embedfn)
    {
        /* just in case of missing 'restore' */
        krzyszembed_gc(master, ps__C, 0);
        if (krzyszfile_isloading(result) || krzyszfile_ispasting(result))
            pd_bind(master, ps__C);
    }

    /* 2. the panels */
    if (readfn || writefn)
    {
        t_krzyszfile *f;
        char buf[64];
        sprintf(buf, "miXed.%lx", (unsigned long)result);
        result->f_bindname = gensym(buf);
        pd_bind((t_pd *)result, result->f_bindname);
        result->f_panelfn = readfn;
        result->f_panelclock = clock_new(result, (t_method)krzyszpanel_tick);
        f = (t_krzyszfile *)pd_new(krzyszfile_class);
        f->f_master = master;
        sprintf(buf, "miXed.%lx", (unsigned long)f);
        f->f_bindname = gensym(buf);
        pd_bind((t_pd *)f, f->f_bindname);
        f->f_panelfn = writefn; 
        f->f_panelclock = clock_new(f, (t_method)krzyszpanel_tick);
        result->f_savepanel = f;
    }
    else result->f_savepanel = 0;

    /* 3. editor */
    if (result->f_editorfn = updatefn)
    {
        result->f_editorclock = clock_new(result, (t_method)krzyszeditor_tick);
        if (!result->f_bindname)
        {
            char buf[64];
            sprintf(buf, "miXed.%lx", (unsigned long)result);
            result->f_bindname = gensym(buf);
            pd_bind((t_pd *)result, result->f_bindname);
        }
    }
    return (result);
}

void krzyszfile_setup(t_class *c, int embeddable)
{
    char buf[MAXPDSTRING];
    if (embeddable)
    {
        class_setsavefn(c, krzyszembed_save);
        class_addmethod(c, (t_method)krzyszembed_restore,
                        gensym("restore"), 0);
    }
    if (!krzyszfile_class)
    {
        ps__C = gensym("#C");
        krzyszfile_class = class_new(gensym("_krzyszfile"), 0, 0,
                                     sizeof(t_krzyszfile),
                                     CLASS_PD | CLASS_NOINLET, 0);
        class_addsymbol(krzyszfile_class, krzyszpanel_symbol);
        class_addmethod(krzyszfile_class, (t_method)krzyszeditor_clear,
                        gensym("clear"), 0);
        class_addmethod(krzyszfile_class, (t_method)krzyszeditor_addline,
                        gensym("addline"), A_GIMME, 0);
        class_addmethod(krzyszfile_class, (t_method)krzyszeditor_end,
                        gensym("end"), 0);
        /* LATER find a way of ensuring that these are not defined yet... */
        snprintf(buf, MAXPDSTRING, "source %s/file.tk\n",
            class_gethelpdir(krzyszfile_class));
        sys_gui(buf);
    }
}
