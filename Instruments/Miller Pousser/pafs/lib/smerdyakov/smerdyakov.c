/*

smerdyakov.c - complicated Markov chain recording and playback
copyright 2006 Miller Puckette.
Available under the BSD license (see "LICENCE.txt" in this distribution.)


Each instance of smerdyakov can maintain one sequence of 'elements' (notes).
Notes have a pitch, velocity, length, duration, and relative probability.

RECORDING:
Play a new sequence in.  smerdysakov expects separate note-on and note-off
messages, and keeps track of hanging notes to fill in durations.  (If
no note-off is given, the duration is set to 1000.)

PLAYBACK:
Plays back from one, or a mixture of two, sequences of 'notes'. 

The 'resttime' parameter controls the maximum inter-onset interval that will
be used in playback (transititions with longer ones will be suppressed.)
*/

#include "m_pd.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct _element
{
    int e_pit;          /* token number (i.e., pitch) */
    float e_prob;       /* relative a priori probability */
    float e_vel;        /* velocity */
    float e_dur;        /* length of note */
    float e_delay;      /* delay since past note */
} t_element;

typedef struct _hang
{
    int h_index;
    double h_time;
} t_hang;

typedef struct _smerdyakov
{
    	    /* pd stuff */
    t_object x_ob;
    t_clock *x_clock;
    t_outlet *x_pitchout;
    t_outlet *x_velout;
    t_outlet *x_durout;
    t_canvas *x_canvas;
    float x_vel;                /* velo inlet */
    float x_prob;               /* relative probability inlet */
    t_symbol *x_symbol;
    	    /* matrix stuff */
    int x_n;                    /* number of elements */
    int x_dim;                  /* dimension */
    int x_maxdepth;             /* 1+maximum depth (to include 0-order) */
    float x_playdepth;          /* depth now playing, up to maximum depth */
    int x_playnext;          	/* next index played */
    t_element *x_seq;
    t_hang *x_hang;             /* elements which have no duration yet */
    int x_nextindex;  	        /* index of next element to play back */
    int x_record;   	        /* true if "record" is on */
    double x_rectime;	        /* time of previous note recorded */
    int x_chordlength;	        /* number of notes so far played as chord */
    t_symbol *x_mixsym1;        /* name of first chain to play from */
    t_symbol *x_mixsym2;        /* and second one */
    t_symbol *x_lastusedsym;    /* name of previously used chain */
    float x_mix;                /* mix factor, 0-1 */
    	    /* parameters */
    float x_resttime;
    float x_maxdiff;
    float x_uniformize;
    int x_norestart;
    float x_tempo;
} t_smerdyakov;

static t_class *smerdyakov_class;

static void smerdyakov_float(t_smerdyakov *x, t_float f)
{
    int pit = f;
    int lastrec;

    if (!x->x_record || pit < 0 || pit >= x->x_dim)
        return;

    if (x->x_vel != 0)
    {
        float timediff = clock_gettimesince(x->x_rectime);
	t_element *t;
        x->x_seq = (t_element *)resizebytes(x->x_seq,
            x->x_n * sizeof(t_element), (x->x_n + 1) * sizeof(t_element));
	t = &x->x_seq[x->x_n];
	t->e_pit = pit;
	t->e_vel = x->x_vel;
	t->e_prob = (x->x_prob < 0 ? 0 : x->x_prob);
	t->e_dur = 1000;
	t->e_delay = timediff;
	x->x_hang[pit].h_time = clock_getlogicaltime();
	x->x_hang[pit].h_index = x->x_n;
	x->x_rectime = clock_getsystime();
        x->x_n = x->x_n + 1;
    }
    else    /* note off: find corresponding note on and fill in duration */
    {
	t_hang *h = &x->x_hang[pit];
	if (h->h_index >= 0 && h->h_index < x->x_n) 
	{
	    float del = clock_gettimesince(h->h_time);
	    if (del < 0)
                del = 0;
	    if (h->h_index >= 0 && h->h_index < x->x_n)
		x->x_seq[h->h_index].e_dur = del;
	    h->h_index = -1;
	}
	return;
    }
}

void smerdyakov_clear(t_smerdyakov *x)
{
    int i, j;
    x->x_seq = (t_element *)resizebytes(x->x_seq, x->x_n * sizeof(t_element), 0);
    x->x_record = 0;
    x->x_n = 0;
    for (i=0; i<x->x_dim; i++)
    	x->x_hang[i].h_index = -1;
    x->x_rectime = 0;
    x->x_chordlength = 0;
    clock_unset(x->x_clock);
}

static void smerdyakov_record(t_smerdyakov *x, t_float f)
{
    x->x_record = (f != 0);
}

static void smerdyakov_play(t_smerdyakov *x, t_float f)
{
    if (f != 0)
    {
        if (x->x_n <= 0)
        {
	    pd_error(x, "smerdyakov: no sequence");
	    return;
        }
        x->x_playnext = 0;
        clock_delay(x->x_clock, 0);
    }
    else clock_unset(x->x_clock);
}

static void smerdyakov_tick(t_smerdyakov *x)
{
    int stacksize = 2 * (x->x_dim + 1) * x->x_maxdepth;
    float *stackspace = (float *)alloca(stacksize * sizeof(float));
    float *pvec, fracdepth, randx, mix = 0, *probsum1 = stackspace,
	*probsum2 = probsum1;
    float probdursum[2], averagedur;
    t_element *e, *e2, *e3, *eplay;
    int i, wantdepth, nout = -1, chainout = 0, nchain = 1, chainindex;
    t_smerdyakov *chainvec[2], *prevchain;
    if (*x->x_mixsym1->s_name)
    {
        if (!(chainvec[0] = 
            (t_smerdyakov *)pd_findbyclass(x->x_mixsym1, smerdyakov_class))
                || chainvec[0]->x_dim != x->x_dim)
        {
            pd_error("%s: no such smerdyakov or wrong dimension",
                x->x_mixsym1->s_name);
            goto fallback;
        }
        if (x->x_mix > 0)
        {
            if (!(chainvec[1] =
              (t_smerdyakov *)pd_findbyclass(x->x_mixsym2, smerdyakov_class))
                || chainvec[1]->x_dim != x->x_dim)
                    pd_error("%s: no such smerdyakov or wrong dimension",
                        x->x_mixsym2->s_name);
            else
            {
                nchain = 2;
                mix = (x->x_mix > 1 ? 1 : (x->x_mix < 0 ? 0 : x->x_mix));
                probsum2 = probsum1 + (x->x_dim + 1) * x->x_maxdepth;
            }
        }
    }
    else
    {
    fallback:
        chainvec[0] = x;
    }

    if ((!*x->x_lastusedsym->s_name) ||
      !(prevchain = 
         (t_smerdyakov *)pd_findbyclass(x->x_lastusedsym, smerdyakov_class)))
            prevchain = x;
        /* if next note to play is out of range, restart */
    if (x->x_playnext < 0 || x->x_playnext >= prevchain->x_n)
    {
        post("oops: restart, playnext = %d", x->x_playnext);
        goto restart;
    }
    eplay = &prevchain->x_seq[x->x_playnext];
    outlet_float(x->x_durout, eplay->e_dur);           
    outlet_float(x->x_velout, eplay->e_vel);              
    outlet_float(x->x_pitchout, eplay->e_pit);
    memset(stackspace, 0, stacksize * sizeof(float));
        /* 1. collect statistics: total probs for each depth, and
        probability distribution for each depth */
    for (chainindex = 0; chainindex < nchain; chainindex++)
    {
        t_smerdyakov *y = chainvec[chainindex];
        float *probsum = stackspace + chainindex*(x->x_dim + 1) * x->x_maxdepth; 
        float *probvec = probsum + x->x_maxdepth;
        float tmpprobdursum = 0;
        for (i = 0, e = y->x_seq; i < y->x_n-1; i++, e++)
        {
            int depth = 0;
            if (i > 0 && e->e_delay > x->x_resttime)
                continue;
            pvec = probvec;
            tmpprobdursum += e->e_prob * e->e_delay;
            for (e2 = e, e3 = eplay; ; e2--, e3--)
            {
                pvec[e->e_pit] += e->e_prob;
                probsum[depth] += e->e_prob;
                pvec += x->x_dim;
                if (e2->e_pit != e3->e_pit || depth >= (x->x_maxdepth-1)
                    || depth > i || depth > x->x_playnext)
                        break;
	        depth++;
            }
        }
        probdursum[chainindex] = tmpprobdursum;
    }
    
        /* 1b. figure out average duration */
    if (nchain > 1)
        averagedur = (probdursum[0] + mix * (probdursum[1] - probdursum[0]))
            / (1e-20 + probsum1[0] + mix * (probsum2[0] - probsum1[0]));
    else averagedur = probdursum[0] / (1e-20 + probsum1[0]);

        /* 2. choose the depth to use.  LATER introduce entropy requirement */

    wantdepth = x->x_playdepth;
    fracdepth = x->x_playdepth - wantdepth;
    if (random() < fracdepth * RAND_MAX)
        wantdepth++;
    if (wantdepth >= x->x_maxdepth)
	wantdepth = x->x_maxdepth-1;
    if (wantdepth > x->x_playnext+1)
	wantdepth = x->x_playnext+1;
    while (wantdepth > 0 && (probsum1[wantdepth]
    	+ mix * (probsum2[wantdepth] - probsum1[wantdepth])) <= 0)
    {
        wantdepth--;
    }

    if (wantdepth == 0 &&
    	(probsum1[0] + mix * (probsum2[0] - probsum1[0])) <= 0)
            goto restart;
        /* 3. choose new index */
    randx = random() * (probsum1[wantdepth]
    	+ mix * (probsum2[wantdepth] - probsum1[wantdepth]))
	    * (1. / (float)RAND_MAX);
    for (chainindex = 0; chainindex < nchain; chainindex++)
    {
    	float weight = (chainindex ? mix : 1-mix);
        float *probsum = stackspace + chainindex * (x->x_dim + 1) * x->x_maxdepth; 
        float *probvec = probsum + x->x_maxdepth;
        t_smerdyakov *y = chainvec[chainindex];
	pvec = probvec + wantdepth * x->x_dim;
	for (i = wantdepth, e = y->x_seq + wantdepth-1; i < y->x_n; i++, e++)
	{
            int j, k;
            if (i > 0 && e->e_delay > x->x_resttime)
                continue;
            for (k = 0, e2 = e, e3=eplay; k < wantdepth; k++, e2--, e3--)
            {
        	if (e2->e_pit != e3->e_pit)
                    goto nofit;
            }
            if (randx >= 0)
	    {
        	nout = i;
		chainout = chainindex;
            }
	    randx -= weight * (e+1)->e_prob;
	nofit:;
	}
    }
    if (nout >= 0)
    {
        float newdel = chainvec[chainout]->x_seq[nout].e_delay;
        newdel += (x->x_uniformize * (averagedur - newdel));
#if 0
	post("averagedur %d", (int)averagedur);
#endif
        x->x_playnext = nout;
	clock_delay(x->x_clock, newdel * x->x_tempo);
	x->x_lastusedsym = (chainout ? x->x_mixsym2 : x->x_mixsym1);
	return;
    }
restart:
    post("smerdyakov: restart");
    x->x_playnext = 0;
    x->x_lastusedsym = &s_;
    clock_delay(x->x_clock, 500);
}

    /* a hack for a specific musical problem.  Suggest from outside
    what the next pitch should be.  This can't be called reentrantly from
    within the tick routine. */
static void smerdyakov_override(t_smerdyakov *x, t_float f)
{
    int i;
    int jumpto = f + 0.5;
    t_smerdyakov *chain = x, *chain2;
    if (x->x_mixsym1->s_name &&
        (chain2 = (t_smerdyakov *)pd_findbyclass(x->x_mixsym1, smerdyakov_class))
            && chain2->x_dim == x->x_dim)
                    chain = chain2;
    for (i = 0; i < chain->x_n; i++)
        if (chain->x_seq[i].e_pit == jumpto)
    {
        x->x_playnext = i;
        x->x_lastusedsym = (chain == x ? &s_ : x->x_mixsym1);
        return;
    }
    for (i = 0; i < chain->x_n; i++)
        if (chain->x_seq[i].e_pit == jumpto+1 ||
            chain->x_seq[i].e_pit == jumpto-1)
    {
        x->x_playnext = i;
        x->x_lastusedsym = (chain == x ? &s_ : x->x_mixsym1);
        return;
    }
    post("smerdyakov_override: pitch %d not found", jumpto);
}

static void smerdyakov_resttime(t_smerdyakov *x, t_float f)
{
    x->x_resttime = (f != 0);
}

static void smerdyakov_norestart(t_smerdyakov *x, t_float f)
{
    x->x_norestart = (f != 0);
}

static void smerdyakov_maxdiff(t_smerdyakov *x, t_float f)
{
    x->x_maxdiff = (f != 0);
}

static void smerdyakov_uniformize(t_smerdyakov *x, t_float f)
{
    x->x_uniformize = f;
    if (x->x_uniformize > 1)
        x->x_uniformize = 1;
    else if (x->x_uniformize < 0)
        x->x_uniformize = 0;
}

static void smerdyakov_depth(t_smerdyakov *x, t_float f)
{
    x->x_playdepth = (f >= 0 ? f : 0);
}

static void smerdyakov_tempo(t_smerdyakov *x, t_float f)
{
    x->x_tempo = (f >= 0 ? 1./f : 1);
}

static void smerdyakov_write(t_smerdyakov *x, t_symbol *filename)
{
    FILE *fd;
    char buf[MAXPDSTRING];
    int i, j;
    canvas_makefilename(x->x_canvas, filename->s_name, buf, MAXPDSTRING);
    sys_bashfilename(buf, buf);
    if (!(fd = fopen(buf, "w")))
    {
    	error("%s: can't create", buf);
    	return;
    }
    for (i = 0; i < x->x_n; i++)
    {
    	    if (fprintf(fd, "%5d %g %g %g %g\n",
    		x->x_seq[i].e_pit, 
    		x->x_seq[i].e_delay, 
    		x->x_seq[i].e_dur, 
    		x->x_seq[i].e_prob, 
    		x->x_seq[i].e_vel) < 5) 
    	    {
    		error("%s: write error", filename->s_name);
    		goto fail;
    	    }
	        post("%5d %g %g %g %g\n",
    		x->x_seq[i].e_pit, 
    		x->x_seq[i].e_delay, 
    		x->x_seq[i].e_dur, 
    		x->x_seq[i].e_prob, 
    		x->x_seq[i].e_vel);
    }
fail:
    fclose(fd);
}

static void smerdyakov_read(t_smerdyakov *x, t_symbol *filename, t_symbol *format)
{
    int filedesc, i, j;
    FILE *fd;
    char buf[MAXPDSTRING], *bufptr;
    smerdyakov_clear(x);
    if ((filedesc = open_via_path(
    	canvas_getdir(x->x_canvas)->s_name,
    	    filename->s_name, "", buf, &bufptr, MAXPDSTRING, 0)) < 0)
    {
    	error("%s: can't open", filename->s_name);
    	return;
    }
    fd = fdopen(filedesc, "r");
    
    while (1)
    {
	int pit, n = x->x_n;
	float delay, dur, prob, vel;
    	if (fscanf(fd, "%d%f%f%f%f", &pit, &delay, &dur, &prob, &vel) < 4) 
    	    break;
        x->x_seq = (t_element *)resizebytes(x->x_seq,
            n * sizeof(t_element), (n + 1) * sizeof(t_element));
	
    	    x->x_seq[n].e_pit = pit; 
    	    x->x_seq[n].e_delay = delay; 
    	    x->x_seq[n].e_dur = dur;
    	    x->x_seq[n].e_prob = prob; 
    	    x->x_seq[n].e_vel = vel; 
	x->x_n = n+1;
    }
    /* post("%s:  read %d elements", filename->s_name, x->x_n); */
    fclose(fd);
}

static void smerdyakov_print(t_smerdyakov *x)
{
    post("record %d norestart %d maxdiff %f resttime %f",
    	x->x_record, x->x_norestart, x->x_maxdiff,
    	x->x_resttime);
}

static void smerdyakov_mix(t_smerdyakov *x, t_symbol *s1, t_symbol *s2,
    t_floatarg f)
{
    x->x_mixsym1 = s1;
    x->x_mixsym2 = s2;
    x->x_mix = f;
}

static void smerdyakov_free(t_smerdyakov *x)
{
    clock_free(x->x_clock);
    if (*x->x_symbol->s_name)
        pd_unbind(&x->x_ob.ob_pd, x->x_symbol);
    freebytes(x->x_hang, x->x_dim * sizeof(t_hang));
    freebytes(x->x_seq, x->x_n * sizeof(t_element));
}

static void *smerdyakov_new(t_symbol *s, t_floatarg dim, t_floatarg depth)
{
    t_smerdyakov *x = (t_smerdyakov *)pd_new(smerdyakov_class);
    x->x_dim = (dim < 1 ? 1 : dim);
    x->x_maxdepth = 1+(depth < 1 ? 1 : depth);
    x->x_clock = clock_new(x, (t_method)smerdyakov_tick);
    floatinlet_new(&x->x_ob, &x->x_vel);
    floatinlet_new(&x->x_ob, &x->x_prob);
    x->x_pitchout = outlet_new(&x->x_ob, &s_float);
    x->x_velout = outlet_new(&x->x_ob, &s_float);
    x->x_durout = outlet_new(&x->x_ob, &s_float);
    x->x_norestart = 0;
    x->x_resttime = 1000000;
    x->x_maxdiff = 50;
    x->x_vel = 0;
    x->x_canvas = canvas_getcurrent();
    x->x_seq = getbytes(0);
    x->x_hang = (t_hang *)getbytes(x->x_dim * sizeof(t_hang));
    x->x_n = 0;
    x->x_playdepth = 1;
    x->x_tempo = 1;
    x->x_symbol = s;
    if (*s->s_name)
        pd_bind(&x->x_ob.ob_pd, s);
    x->x_mixsym1 = &s_;
    x->x_mixsym2 = &s_;
    x->x_lastusedsym = &s_;
    return (x);
}

void smerdyakov_setup(void)
{
    smerdyakov_class = class_new(gensym("smerdyakov"),
        (t_newmethod)smerdyakov_new, (t_method)smerdyakov_free,
	sizeof(t_smerdyakov), 0, A_DEFFLOAT, A_DEFFLOAT, A_DEFSYMBOL, 0);
    class_addfloat(smerdyakov_class, smerdyakov_float);
    class_addmethod(smerdyakov_class, (t_method)smerdyakov_print,
        gensym("print"), 0);
    class_addmethod(smerdyakov_class, (t_method)smerdyakov_clear,
        gensym("clear"), 0);
    class_addmethod(smerdyakov_class, (t_method)smerdyakov_record,
        gensym("record"), A_FLOAT, 0);
    class_addmethod(smerdyakov_class, (t_method)smerdyakov_play,
        gensym("play"), A_FLOAT, 0);
    class_addmethod(smerdyakov_class, (t_method)smerdyakov_depth,
        gensym("depth"), A_FLOAT, 0);
    class_addmethod(smerdyakov_class, (t_method)smerdyakov_tempo,
        gensym("tempo"), A_FLOAT, 0);
    class_addmethod(smerdyakov_class, (t_method)smerdyakov_override,
        gensym("override"), A_FLOAT, 0);
    class_addmethod(smerdyakov_class, (t_method)smerdyakov_resttime,
        gensym("resttime"), A_FLOAT, 0);
    class_addmethod(smerdyakov_class, (t_method)smerdyakov_maxdiff,
        gensym("maxdiff"), A_FLOAT, 0);
    class_addmethod(smerdyakov_class, (t_method)smerdyakov_uniformize,
        gensym("uniformize"), A_FLOAT, 0);
    class_addmethod(smerdyakov_class, (t_method)smerdyakov_norestart,
        gensym("norestart"), A_FLOAT, 0);
    class_addmethod(smerdyakov_class, (t_method)smerdyakov_write,
        gensym("write"), A_SYMBOL, 0);
    class_addmethod(smerdyakov_class, (t_method)smerdyakov_read,
        gensym("read"), A_SYMBOL, 0);
    class_addmethod(smerdyakov_class, (t_method)smerdyakov_mix,
        gensym("mix"), A_DEFSYMBOL, A_DEFSYMBOL, A_DEFFLOAT, 0);
}

