/*unblob.c - PD external
Copyleft 2009-2010 Wolfgang Jaeger (wolfgang DOT jaeger AT student DOT kug DOT ac DOT at
For information on usage and redistribution, and for a DISCLAIMER OF ALL
WARRANTIES, see the file, "LICENSE.txt," in this distribution.*/

#include "m_pd.h"

static t_class *unblob_class;

typedef struct _unblob{
t_object x_obj;

t_atom *buffer;
t_int anzahlbits;
t_int blocksize;
}t_unblob;


void unblob_blocksize(t_unblob *x, t_floatarg f)
{
	x->blocksize = (t_float)f;
}

void unblob_resolution(t_unblob *x, t_floatarg f)
{
	x->anzahlbits = (t_float)f;
}

void unblob_list(t_unblob *x, t_symbol *s, int argc, t_atom *argv)
{
	int groeszeint;
	int Konstante;
		
	groeszeint = sizeof(int)*8;
	Konstante = (1<<(groeszeint-1))-1;
	
	unsigned char *temp;
			
	temp=getbytes(argc*sizeof(unsigned char));
	
	if(x->anzahlbits == 0)
	{
		int i;
		int j;
		
		x->buffer=getbytes((argc/4)*sizeof(t_atom));	
	
		j=0;
		i=0;
		while(i<argc)
		{
			temp[i] = (unsigned char)atom_getfloatarg(i, argc, argv);
			i++;
			if(!(i%4))
			{
				j=((i-1)/4);
				SETFLOAT(&x->buffer[j], ((t_float *)temp)[j]);
			}
		}
		
		outlet_list(x->x_obj.ob_outlet, &s_list, argc/4, x->buffer);
		
		if(x->buffer != (t_atom *)0)
			   freebytes(x->buffer, (argc/4)*sizeof(t_atom));
		if(temp != (unsigned char *)0)
			   freebytes(temp, argc*sizeof(unsigned char));
		return;
	}
	else if(x->anzahlbits == groeszeint)
	{
		int i;
		int j;
				
		x->buffer=getbytes((argc/4)*sizeof(t_atom));	
	
		j=0;
		i=0;
		while(i<argc)
		{
			temp[i] = (unsigned char)atom_getfloatarg(i, argc, argv);
			i++;
			if(!(i%4))
			{
				j=((i-1)/4);
				SETFLOAT(&x->buffer[j], (((t_float)(((t_int *)temp)[j])) / ((t_float)Konstante)));
			}
		}
		
		outlet_list(x->x_obj.ob_outlet, &s_list, argc/4, x->buffer);
		
		if(x->buffer != (t_atom *)0)
			   freebytes(x->buffer, (argc/4)*sizeof(t_atom));
		if(temp != (unsigned char *)0)
			   freebytes(temp, argc*sizeof(unsigned char));
		return;
	}
	else{
		int i;
		int j;
		int k;
		int lauf;
		int MASK;
		int mask;
		int mask1;
		int temporaer;
		
		x->buffer=getbytes(((argc/4)*groeszeint/x->anzahlbits)*sizeof(t_atom));	
		
		MASK = (1<<x->anzahlbits)-1;
		lauf = groeszeint;
		
		k=0;
		j=0;
		i=0;
		while(i<argc)
		{
			temp[i] = (unsigned char)atom_getfloatarg(i, argc, argv);
			i++;
			if(!(i%4))
			{
				if(lauf < 0) 
				{
					mask = (1<<(lauf+x->anzahlbits))-1;
					mask1 = ~((1<<((lauf%groeszeint)+groeszeint))-1);
					temporaer = (((mask & (((t_int *)temp)[j]))<<(-lauf)) | (((mask1 & (((t_int *)temp)[j+1]))>>((lauf%groeszeint)+groeszeint)) & ((1<<(-lauf))-1)))<<(groeszeint-x->anzahlbits);
					lauf = (lauf%groeszeint)+groeszeint;
					j++;
					SETFLOAT(&x->buffer[k], ((t_float)temporaer / (t_float)Konstante));
					k++;
				}
				while(lauf > 0)
				{
					lauf = (lauf - x->anzahlbits);
					if(lauf >= 0) 
					{
						temporaer = (((MASK<<lauf) & (((t_int *)temp)[j]))<<((groeszeint-x->anzahlbits)-lauf));
						SETFLOAT(&x->buffer[k], ((t_float)temporaer / (t_float)Konstante));
						k++;
					}
				}
				if(lauf==0)
				{
					lauf=groeszeint;
					j++;
				}	
			}	
		}	
		
		outlet_list(x->x_obj.ob_outlet, &s_list, x->blocksize, x->buffer);
		
		if(x->buffer != (t_atom *)0)
			   freebytes(x->buffer,((argc/4)*groeszeint/x->anzahlbits)*sizeof(t_atom));
		if(temp != (unsigned char *)0)
			   freebytes(temp, argc*sizeof(unsigned char));
		return;
	}
}

void *unblob_new(void)
{
	t_unblob *x = (t_unblob *)pd_new(unblob_class);
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, gensym("float"), gensym("resolution"));
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, gensym("float"), gensym("blocksize"));
	outlet_new(&x->x_obj, &s_list);

	x->buffer = (t_atom *) 0;
	x->anzahlbits = 0;
	x->blocksize = 0;

	return (void *)x;
}

void unblob_setup(void)
{
	unblob_class = class_new(gensym("unblob"),
		(t_newmethod)unblob_new, 0,
		sizeof(t_unblob),
		CLASS_DEFAULT, 0);
	class_addlist(unblob_class, unblob_list);
	class_addmethod(unblob_class, (t_method)unblob_resolution, gensym("resolution"), A_DEFFLOAT, 0);
	class_addmethod(unblob_class, (t_method)unblob_blocksize, gensym("blocksize"), A_DEFFLOAT, 0);
}
