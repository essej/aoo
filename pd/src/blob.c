/*blob.c - PD external
Copyleft 2009-2010 Wolfgang Jaeger (wolfgang DOT jaeger AT student DOT kug DOT ac DOT at
For information on usage and redistribution, and for a DISCLAIMER OF ALL
WARRANTIES, see the file, "LICENSE.txt," in this distribution.*/

#include "m_pd.h"

static t_class *blob_class;

typedef struct _blob{
t_object x_obj;
t_outlet *datalist_out, *resolution_out, *blocksize_out;

t_atom *buffer;
t_int anzahlbits;
}t_blob;

void blob_format(t_blob *x, t_symbol *s, int argc, t_atom *argv)
{
	if(argc == 1 || argc == 2)
	{
		if((&argv[0])->a_type == A_SYMBOL) 
		{
			if(*((&argv[0])->a_w.w_symbol->s_name) == 'f')
			{
				x->anzahlbits = 0;
			}
			else if(*((&argv[0])->a_w.w_symbol->s_name) == 'i')
			{
				if((&argv[1])->a_type == A_FLOAT) 
				{
					x->anzahlbits = (t_int)atom_getfloat( &argv[1] );
				}
				else {
					post("no resolution given");
				}
			}
			else{
				post("possible types are float and integer");
			}
		}
		else {
			post("first type type (float or integer)");
		}
	}
	else
	{
		post("neither type nor resolution given or too many arguments");
	}
}

void blob_list(t_blob *x, t_symbol *s, int argc, t_atom *argv)
{	
	int groeszeint;
	int Konstante;

	groeszeint = sizeof(int)*8;
	Konstante = (1<<(groeszeint-1))-1;
	
	if(x->anzahlbits == 0)
	{
		int i;
		int j;
		t_float *temp;
		
		x->buffer=getbytes((4*argc)*sizeof(t_atom));	
		temp=getbytes(argc*sizeof(t_float));
		
		for(i=0; i<argc; i++)
		{
			temp[i] = atom_getfloatarg(i, argc, argv);
			j=4*i;
			SETFLOAT(&x->buffer[j], ((unsigned char *)temp)[j]);
			j++;
			SETFLOAT(&x->buffer[j], ((unsigned char *)temp)[j]);
			j++;
			SETFLOAT(&x->buffer[j], ((unsigned char *)temp)[j]);
			j++;
			SETFLOAT(&x->buffer[j], ((unsigned char *)temp)[j]);
		}
		
		outlet_float(x->blocksize_out, argc);
		outlet_float(x->resolution_out, x->anzahlbits);
		outlet_list(x->datalist_out, &s_list, (4*argc), x->buffer);
		
		if(x->buffer != (t_atom *)0)
			   freebytes(x->buffer, (4*argc)*sizeof(t_atom));
		if(temp != (t_float *)0)
			   freebytes(temp, argc*sizeof(t_float));
		return;
	}
	else if(x->anzahlbits >= groeszeint)
	{
		int i;
		int j;
		int *temp;
		
		x->anzahlbits = groeszeint;
		x->buffer=getbytes((4*argc)*sizeof(t_atom));	
		temp=getbytes(argc*sizeof(int));
		
		for(i=0; i<argc; i++)
		{
			temp[i] = (int)(atom_getfloatarg(i, argc, argv) * Konstante);
			j=4*i;
			SETFLOAT(&x->buffer[j], ((unsigned char *)temp)[j]);
			j++;
			SETFLOAT(&x->buffer[j], ((unsigned char *)temp)[j]);
			j++;
			SETFLOAT(&x->buffer[j], ((unsigned char *)temp)[j]);
			j++;
			SETFLOAT(&x->buffer[j], ((unsigned char *)temp)[j]);
		}
		
		outlet_float(x->blocksize_out, argc);
		outlet_float(x->resolution_out, x->anzahlbits);
		outlet_list(x->datalist_out, &s_list, (4*argc), x->buffer);
		
		if(x->buffer != (t_atom *)0)
			   freebytes(x->buffer, (4*argc)*sizeof(t_atom));
		if(temp != (int *)0)
			   freebytes(temp, argc*sizeof(int));
		return;
	}		
	else{
		if(x->anzahlbits < 1) x->anzahlbits=1;
		
		int i;
		int j;
		int k;
		int help;
		int fertig;
		int temporaer;
		int *temp;
		
		x->buffer=getbytes(( (4*(((int)(argc*x->anzahlbits/groeszeint))+1)) )*sizeof(t_atom));
		temp=getbytes(( (((int)(argc*x->anzahlbits/groeszeint))+1) )*sizeof(int));
			
		int MASK;
		int MASK1;
		int lauf;
		int mask;
		int mask1;
				
		MASK1=((1<<(groeszeint-x->anzahlbits))-1);
		MASK = ~MASK1;
		lauf = groeszeint;
				
		j=0;
		fertig = j;
		help = 0;
		
		for(i=0; i<argc; i++)
		{
			temporaer = ((((int)(atom_getfloatarg(i, argc, argv) * Konstante)) & MASK) >> (groeszeint-x->anzahlbits)) & ((1<<x->anzahlbits)-1);
			
			lauf = (lauf - x->anzahlbits);
			
			if(lauf >= 0) 
			{
				temp[j]=(temporaer<<lauf) | temp[j];
				if(i == argc-1) j++;
			}
			if(lauf < 0) 
			{
				help = 1;
				mask1 = (1<<(-lauf))-1;
				mask = ((1<<x->anzahlbits)-1)-mask1;
			
				temp[j] = (((temporaer & mask)>>(-lauf)) & ((1<<(x->anzahlbits+lauf))-1)) | temp[j];
				lauf = (lauf%groeszeint)+groeszeint;
				j++;
				temp[j] = (temporaer & mask1)<<lauf;
			}
			if(lauf==0) 
			{
				lauf=groeszeint;
				j++;
			}
			
			if(fertig != j)
			{
				k=4*fertig;
				SETFLOAT(&x->buffer[k], ((unsigned char *)temp)[k]);
				k++;
				SETFLOAT(&x->buffer[k], ((unsigned char *)temp)[k]);
				k++;
				SETFLOAT(&x->buffer[k], ((unsigned char *)temp)[k]);
				k++;
				SETFLOAT(&x->buffer[k], ((unsigned char *)temp)[k]);
				fertig++;
				if(i == argc-1 && help == 1) 
				{
					k++;
					SETFLOAT(&x->buffer[k], ((unsigned char *)temp)[k]);
					k++;
					SETFLOAT(&x->buffer[k], ((unsigned char *)temp)[k]);
					k++;
					SETFLOAT(&x->buffer[k], ((unsigned char *)temp)[k]);
					k++;
					SETFLOAT(&x->buffer[k], ((unsigned char *)temp)[k]);
					fertig++;
					help = 0;
				}
			}
		}
		
		outlet_float(x->blocksize_out, argc);
		outlet_float(x->resolution_out, x->anzahlbits);
		outlet_list(x->datalist_out, &s_list, 4*fertig, x->buffer);
		
		if(x->buffer != (t_atom *)0)
			   freebytes(x->buffer, ( (4*(((int)(argc*x->anzahlbits/groeszeint))+1)) )*sizeof(t_atom) );			
		if(temp != (int *)0)
			   freebytes(temp, ( (((int)(argc*x->anzahlbits/groeszeint))+1) )*sizeof(int) );			
		return;
	}
}

void *blob_new(void)
{
	t_blob *x = (t_blob *)pd_new(blob_class);
	x->datalist_out = outlet_new(&x->x_obj, &s_list);
	x->resolution_out = outlet_new(&x->x_obj, &s_float);
	x->blocksize_out = outlet_new(&x->x_obj, &s_float);
	
	x->buffer = (t_atom *) 0;
	x->anzahlbits = 0;

	return (void *)x;
}

void blob_setup(void)
{
	blob_class = class_new(gensym("blob"),
		(t_newmethod)blob_new, 0,
		sizeof(t_blob),
		CLASS_DEFAULT, 0);
	class_addlist(blob_class, blob_list);
	class_addmethod(blob_class, (t_method)blob_format, gensym("format"), A_GIMME, 0);
}
