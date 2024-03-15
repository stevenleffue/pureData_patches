/* code for system pd class */

#include "m_pd.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef NT
#pragma warning(disable:4244)
#endif

typedef struct system
{
    t_object x_ob;
} t_system;

t_class *system_class;

static void *system_new(t_symbol *unused)
{
    t_system *x = (t_system *)pd_new(system_class);

    return (void *)x;
}

static void system_anything(t_system *x, t_symbol *s, int argc, t_atom *argv)
{
    char cmdbuf[1000];
    char atombuf[1000];
    int i;
    strcpy(cmdbuf, s->s_name);
    for (i = 0; i < argc; i++)
    {
    	strcat(cmdbuf, " ");
    	atom_string(&argv[i], atombuf, 1000);
    	strcat(cmdbuf, atombuf);
    }
    strcat(cmdbuf, "\n");
    fprintf(stderr, "%s", cmdbuf);
    system(cmdbuf);
    fprintf(stderr, "done.");
}

void system_setup(void)
{
    post("system v0.1 msp");
    system_class = class_new(gensym("system"), (t_newmethod)system_new,
    	0, sizeof(t_system), 0, 0);
    class_addanything(system_class, system_anything);
}
