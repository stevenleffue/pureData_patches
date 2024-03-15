/* Copyright (c) 2002-2003 krzYszcz and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#ifndef __KRZYSZFILE_H__
#define __KRZYSZFILE_H__

typedef void (*t_krzyszfilefn)(t_pd *, t_symbol *, int, t_atom *);
typedef void (*t_krzyszembedfn)(t_pd *, t_binbuf *, t_symbol *);

typedef struct _krzyszfile
{
    t_pd                 f_pd;
    t_pd                *f_master;
    t_canvas            *f_canvas;
    t_symbol            *f_bindname;
    t_symbol            *f_inidir;
    t_symbol            *f_inifile;
    t_krzyszfilefn       f_panelfn;
    t_krzyszfilefn       f_editorfn;
    t_krzyszembedfn      f_embedfn;
    t_binbuf            *f_binbuf;
    t_clock             *f_panelclock;
    t_clock             *f_editorclock;
    struct _krzyszfile  *f_savepanel;
    struct _krzyszfile  *f_next;
} t_krzyszfile;

void krzyszeditor_open(t_krzyszfile *f, char *title);
void krzyszeditor_close(t_krzyszfile *f, int ask);
void krzyszeditor_append(t_krzyszfile *f, char *contents);
void krzyszpanel_open(t_krzyszfile *f, t_symbol *inidir);
void krzyszpanel_save(t_krzyszfile *f, t_symbol *inidir, t_symbol *inifile);
int krzyszfile_ismapped(t_krzyszfile *f);
int krzyszfile_isloading(t_krzyszfile *f);
int krzyszfile_ispasting(t_krzyszfile *f);
void krzyszfile_free(t_krzyszfile *f);
t_krzyszfile *krzyszfile_new(t_pd *master, t_krzyszembedfn embedfn,
			     t_krzyszfilefn readfn, t_krzyszfilefn writefn,
			     t_krzyszfilefn updatefn);
void krzyszfile_setup(t_class *c, int embeddable);

#endif
