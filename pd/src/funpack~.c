/*funpack~.c - PD external - based on unpack~.c
Copyleft 2009-2010 Wolfgang Jaeger (wolfgang DOT jaeger AT student DOT kug DOT ac DOT at
For information on usage and redistribution, and for a DISCLAIMER OF ALL
WARRANTIES, see the file, "LICENSE.txt," in this distribution.*/

#include "m_pd.h"

#define SAMPLESPERBLOCK 64
#define NUMBEROFSEQUENCENUMBERS 99
#define MODULOBUFFERINTERPOLATION 6
#define MAXNUMBEROFIDS 10

static t_class *funpack_tilde_class;

typedef struct _funpack_tilde
{
	t_object x_obj;
	
	t_sample *buffer1, *buffer2 ;
	t_sample *rp1, *wp1, *rp2, *wp2;
	t_int lap;
	t_int bufsize;
	t_int ab;
	
	t_int sequencenumber[MAXNUMBEROFIDS];
	t_int tempseq[MAXNUMBEROFIDS];
	
	t_int wasempty;
	t_int wasemptyID;
	
	t_int ID[MAXNUMBEROFIDS];
	t_int countID;
	
	t_int IDerror;
	t_int nonewcolumngood;
	
	t_int mode;
}t_funpack_tilde;

void funpack_tilde_mode(t_funpack_tilde *x, t_floatarg f)
{
	if((t_int)f < 1) x->mode = 0;
	else x->mode = 1;
}

void funpack_tilde_sequencenumber(t_funpack_tilde *x, t_floatarg f)
{
	if(x->countID != 0)
	{
		x->sequencenumber[x->countID-1] = (t_int)f;
		if(x->nonewcolumngood == 1)
		{
			x->nonewcolumngood = 0;
			if( ((x->tempseq[x->countID-1]+1)%NUMBEROFSEQUENCENUMBERS) != x->sequencenumber[x->countID-1] )
			{
				x->nonewcolumngood = 2;
			}
		}
	}
	else
	{
		post("No ID!");
	}
}

void funpack_tilde_ID(t_funpack_tilde *x, t_floatarg f)
{
	int i;
	/*first ID: write signal into buffer*/ 
	if(x->countID == 0)
	{
		x->ID[x->countID]=(t_int)f;
		x->countID++;
		x->wasemptyID = 0;
	}
	else if(x->countID < MAXNUMBEROFIDS)
	{
		for(i = 0; i < x->countID; i++)
		{
			/*if any ID appears twice: signal should be append*/
			if(x->ID[i] == (t_int)f)
			{
				if( x->ID[x->countID] == 0 )
				{
					/*new column good*/
					if( x->ID[0] == (t_int)f )
					{
						x->ID[0]=(t_int)f;
						x->countID = 1;
						x->wasemptyID = 0;
					}
					/*new column NOT good*/
					else {
						x->IDerror = 1;
						x->ID[0]=(t_int)f;
						x->countID = 1;
						x->wasemptyID = 0;
					}		  
				}
				else {
					x->IDerror = 3;
					x->ID[0]=(t_int)f;
					x->countID = 1;
					x->wasemptyID = 0;
				}
				return;
			}
		}
		/*no new column good*/
		if( x->ID[x->countID] == (t_int)f )
		{
			x->ID[x->countID]=(t_int)f;
			x->countID++;
			x->nonewcolumngood = 1;
		}
		/*new "stream"*/
		else if( x->ID[x->countID] == 0)
		{
			x->ID[x->countID]=(t_int)f;
			x->countID++;
		}
		/*no new column NOT good*/
		else if( x->ID[x->countID] != (t_int)f)
		{
			x->IDerror = 2;
			x->ID[x->countID]=(t_int)f;
			x->countID++;
		}
		else {
		post("this post shouldn't be possible");
		}
	}
	else
	{
		t_int k;
		for(k=0; k<MAXNUMBEROFIDS; k++) 
		{
			x->ID[k]=0;
		}
		post("it's not allowed to mix more than %i signals", MAXNUMBEROFIDS);
		x->ID[0]=(t_int)f;
		x->countID = 1;
		x->wasemptyID = 0;
	}
}

static void funpack_tilde_list(t_funpack_tilde *x, t_symbol *s, int argc, t_atom *argv)
{
	if(x->countID == 0)
	{
		return;
	}
	/*.......*/
	if(x->IDerror == 1)
	{
		/*write into buffer first, then go back and correct the jump, before returning set stuff back*/
		t_atom *ap = argv;
		t_int i;
		t_sample factor;
		t_sample *help1;
		t_sample *help2;
		if(!x->ab)
		{
			if( (!x->lap && ((x->wp1-x->rp1)<5*SAMPLESPERBLOCK) && ((x->wp1-x->rp1)>5)) || (x->lap && ((x->wp1-x->buffer1 + x->buffer1+x->bufsize-x->rp1)<5*SAMPLESPERBLOCK) && ((x->wp1-x->buffer1 + x->buffer1+x->bufsize-x->rp1)>5)) )
			{
				for (i = 0, ap = argv; i < argc; ap++, i++)
				{	
					*(x->wp1)++ = atom_getfloat(ap);
					if (x->wp1 == x->buffer1 + x->bufsize) 
					{
						x->wp1 = x->buffer1;
						x->lap = !x->lap;
					}
				}
				if( (x->wp1 - (SAMPLESPERBLOCK+5)) < x->buffer1 ) help1 = x->wp1 - (SAMPLESPERBLOCK+5) + x->bufsize;
				else help1 = x->wp1 - (SAMPLESPERBLOCK+5);
				if( (x->wp1 - (SAMPLESPERBLOCK-4)) < x->buffer1 ) help2 = x->wp1 - (SAMPLESPERBLOCK-4) + x->bufsize;
				else help2 = x->wp1 - (SAMPLESPERBLOCK-4);
				factor = (*help1 - *help2) / 8; 
				for(i=1; i < 9; i++)
				{
					help2 = help1+i;
					if( help2 >= x->buffer1 + x->bufsize )
					{
						help2 = help2 - x->bufsize;
					}
					*help2 = *help1 - factor*i;
				}
			}
		}
		if(x->ab)
		{
			if( (!x->lap && ((x->wp2-x->rp2)<5*SAMPLESPERBLOCK) && ((x->wp2-x->rp2)>5)) || (x->lap && ((x->wp2-x->buffer2 + x->buffer2+x->bufsize-x->rp2)<5*SAMPLESPERBLOCK) && ((x->wp2-x->buffer2 + x->buffer2+x->bufsize-x->rp2)>5)) )
			{
				for (i = 0, ap = argv; i < argc; ap++, i++)
				{	
					*(x->wp2)++ = atom_getfloat(ap);
					if (x->wp2 == x->buffer2 + x->bufsize) 
					{
						x->wp2 = x->buffer2;
						x->lap = !x->lap;
					}
				}
				if( (x->wp2 - (SAMPLESPERBLOCK+5)) < x->buffer2 ) help1 = x->wp2 - (SAMPLESPERBLOCK+5) + x->bufsize;
				else help1 = x->wp2 - (SAMPLESPERBLOCK+5);
				if( (x->wp2 - (SAMPLESPERBLOCK-4)) < x->buffer2 ) help2 = x->wp2 - (SAMPLESPERBLOCK-4) + x->bufsize;
				else help2 = x->wp2 - (SAMPLESPERBLOCK-4);
				factor = (*help1 - *help2) / 8; 
				for(i=1; i < 9; i++)
				{
					help2 = help1+i;
					if( help2 >= x->buffer2 + x->bufsize )
					{
						help2 = help2 - x->bufsize;
					}
					*help2 = *help1 - factor*i;
				}
			}
		}
		x->IDerror = 0;
		for(i=1; i<MAXNUMBEROFIDS; i++) 
		{
			x->ID[i]=0;
		}
		for(i=0; i<MAXNUMBEROFIDS; i++)
		{
			x->tempseq[i]=0;
			x->sequencenumber[i]=0;
		}	
		x->tempseq[x->countID-1] = x->sequencenumber[x->countID-1];
		if(x->tempseq[x->countID-1] >= NUMBEROFSEQUENCENUMBERS) x->tempseq[x->countID-1] = 0;
		return;
	}
	if(x->IDerror == 2)
	{
		/*mix the signals and go back to correct the jump, before going ahead set stuff back*/
		t_atom *ap = argv;
		t_int j = 0;
		t_sample factor;
		t_sample *help1;
		t_sample *help2;
		if(!x->ab)
		{
			if( (!x->lap && (x->wp1-x->rp1)>=(SAMPLESPERBLOCK+5)) || (x->lap && (x->wp1-x->buffer1 + x->buffer1+x->bufsize-x->rp1)>=(SAMPLESPERBLOCK+5)) )
			{
				t_sample *tempwp;
				t_float bitsinbuffer = SAMPLESPERBLOCK;
				tempwp = x->wp1; 
				if(tempwp == x->buffer1) tempwp = x->buffer1 + x->bufsize;
				for (j = 0, ap = argv; j < bitsinbuffer; ap++, j++)
				{
					tempwp--;
					if(x->mode == 0) *tempwp = *tempwp * (x->countID-1) / x->countID + atom_getfloat(ap+((SAMPLESPERBLOCK-1)-j)) / x->countID;
					if(x->mode == 1) *tempwp = *tempwp + atom_getfloat(ap+((SAMPLESPERBLOCK-1)-j));
					if(tempwp == x->buffer1) tempwp = x->buffer1 + x->bufsize;
				}
			}
			if( (x->wp1 - (SAMPLESPERBLOCK+5)) < x->buffer1 ) help1 = x->wp1 - (SAMPLESPERBLOCK+5) + x->bufsize;
			else help1 = x->wp1 - (SAMPLESPERBLOCK+5);
			if( (x->wp1 - (SAMPLESPERBLOCK-4)) < x->buffer1 ) help2 = x->wp1 - (SAMPLESPERBLOCK-4) + x->bufsize;
			else help2 = x->wp1 - (SAMPLESPERBLOCK-4);
			factor = (*help1 - *help2) / 8; 
			for(j=1; j < 9; j++)
			{
				help2 = help1+j;
				if( help2 >= x->buffer1 + x->bufsize )
				{
					help2 = help2 - x->bufsize;
				}
				*help2 = *help1 - factor*j;
			}
		}
		if(x->ab)
		{
			if( (!x->lap && (x->wp2-x->rp2)>=(SAMPLESPERBLOCK+5)) || (x->lap && (x->wp2-x->buffer2 + x->buffer2+x->bufsize-x->rp2)>=(SAMPLESPERBLOCK+5)) )
			{
				t_sample *tempwp;
				t_float bitsinbuffer = SAMPLESPERBLOCK;
				tempwp = x->wp2; 
				if(tempwp == x->buffer2) tempwp = x->buffer2 + x->bufsize;
				for (j = 0, ap = argv; j < bitsinbuffer; ap++, j++)
				{
					tempwp--;
					if(x->mode == 0) *tempwp = *tempwp * (x->countID-1) / x->countID + atom_getfloat(ap+((SAMPLESPERBLOCK-1)-j)) / x->countID;
					if(x->mode == 1) *tempwp = *tempwp + atom_getfloat(ap+((SAMPLESPERBLOCK-1)-j));
					if(tempwp == x->buffer2) tempwp = x->buffer2 + x->bufsize;
				}
			}
			if( (x->wp2 - (SAMPLESPERBLOCK+5)) < x->buffer2 ) help1 = x->wp2 - (SAMPLESPERBLOCK+5) + x->bufsize;
			else help1 = x->wp2 - (SAMPLESPERBLOCK+5);
			if( (x->wp2 - (SAMPLESPERBLOCK-4)) < x->buffer2 ) help2 = x->wp2 - (SAMPLESPERBLOCK-4) + x->bufsize;
			else help2 = x->wp2 - (SAMPLESPERBLOCK-4);
			factor = (*help1 - *help2) / 8; 
			for(j=1; j < 9; j++)
			{
				help2 = help1+j;
				if( help2 >= x->buffer2 + x->bufsize )
				{
					help2 = help2 - x->bufsize;
				}
				*help2 = *help1 - factor*j;
			}
		}
		x->IDerror = 0;
		for(j=1; j<MAXNUMBEROFIDS; j++) 
		{
			x->ID[j]=0;
		}
		for(j=0; j<MAXNUMBEROFIDS; j++)
		{
			x->tempseq[j]=0;
			x->sequencenumber[j]=0;
		}	
		x->tempseq[x->countID-1] = x->sequencenumber[x->countID-1];
		if(x->tempseq[x->countID-1] >= NUMBEROFSEQUENCENUMBERS) x->tempseq[x->countID-1] = 0;
		return;
	}
	if(x->IDerror == 3)
	{
		/*go back to correct, append the data afterwards and set back stuff in a final step*/
		t_atom *ap = argv;
		t_int i;
		t_sample factor;
		t_sample *help1;
		t_sample *help2;
		if(!x->ab)
		{
			if( (!x->lap && ((x->wp1-x->rp1)<5*SAMPLESPERBLOCK) && ((x->wp1-x->rp1)>(SAMPLESPERBLOCK+5))) || (x->lap && ((x->wp1-x->buffer1 + x->buffer1+x->bufsize-x->rp1)<5*SAMPLESPERBLOCK) && ((x->wp1-x->buffer1 + x->buffer1+x->bufsize-x->rp1)>(SAMPLESPERBLOCK+5))) )
			{
				if( (x->wp1 - (SAMPLESPERBLOCK+5)) < x->buffer1 ) help1 = x->wp1 - (SAMPLESPERBLOCK+5) + x->bufsize;
				else help1 = x->wp1 - (SAMPLESPERBLOCK+5);
				if( (x->wp1 - (SAMPLESPERBLOCK-4)) < x->buffer1 ) help2 = x->wp1 - (SAMPLESPERBLOCK-4) + x->bufsize;
				else help2 = x->wp1 - (SAMPLESPERBLOCK-4);
				factor = (*help1 - *help2) / 8; 
				for(i=1; i < 9; i++)
				{
					help2 = help1+i;
					if( help2 >= x->buffer1 + x->bufsize )
					{
						help2 = help2 - x->bufsize;
					}
					*help2 = *help1 - factor*i;
				}
				for (i = 0, ap = argv; i < argc; ap++, i++)
				{	
					*(x->wp1)++ = atom_getfloat(ap);
					if (x->wp1 == x->buffer1 + x->bufsize) 
					{
						x->wp1 = x->buffer1;
						x->lap = !x->lap;
					}
				}
				
			}
		}
		if(x->ab)
		{
			if( (!x->lap && ((x->wp2-x->rp2)<5*SAMPLESPERBLOCK) && ((x->wp2-x->rp2)>(SAMPLESPERBLOCK+5))) || (x->lap && ((x->wp2-x->buffer2 + x->buffer2+x->bufsize-x->rp2)<5*SAMPLESPERBLOCK) && ((x->wp2-x->buffer2 + x->buffer2+x->bufsize-x->rp2)>(SAMPLESPERBLOCK+5))) )
			{
				if( (x->wp2 - (SAMPLESPERBLOCK+5)) < x->buffer2 ) help1 = x->wp2 - (SAMPLESPERBLOCK+5) + x->bufsize;
				else help1 = x->wp2 - (SAMPLESPERBLOCK+5);
				if( (x->wp2 - (SAMPLESPERBLOCK-4)) < x->buffer2 ) help2 = x->wp2 - (SAMPLESPERBLOCK-4) + x->bufsize;
				else help2 = x->wp2 - (SAMPLESPERBLOCK-4);
				factor = (*help1 - *help2) / 8; 
				for(i=1; i < 9; i++)
				{
					help2 = help1+i;
					if( help2 >= x->buffer2 + x->bufsize )
					{
						help2 = help2 - x->bufsize;
					}
					*help2 = *help1 - factor*i;
				}
				for (i = 0, ap = argv; i < argc; ap++, i++)
				{	
					*(x->wp2)++ = atom_getfloat(ap);
					if (x->wp2 == x->buffer2 + x->bufsize) 
					{
						x->wp2 = x->buffer2;
						x->lap = !x->lap;
					}
				}
			}
		}
		x->IDerror = 0;
		for(i=1; i<MAXNUMBEROFIDS; i++) 
		{
			x->ID[i]=0;
		}
		x->tempseq[x->countID-1]++;
		if(x->tempseq[x->countID-1] >= NUMBEROFSEQUENCENUMBERS) x->tempseq[x->countID-1] = 0;
		return;
	}
	/*.......*/
	/*...*/
	if(x->countID == 1)
	{
		if(!x->ab)
		{	
			if( (!x->lap && (x->wp1-x->rp1)>(2*x->bufsize/3)) | (x->lap && (((x->wp1-x->buffer1)+(x->buffer1+x->bufsize-x->rp1))>(2*x->bufsize/3))) )
			{
				t_int modulo = MODULOBUFFERINTERPOLATION-1;
				t_int m = 0;
				do
				{
					m++;
					if(m%modulo)
					{
						*(x->wp2)++ = *(x->rp1);
					}
					*(x->rp1)++ = 0;
					if (x->rp1 == x->buffer1 + x->bufsize)
					{
						x->rp1 = x->buffer1;
						x->lap = !x->lap;
					}
				}while(x->rp1 != x->wp1);
				x->rp1 = x->wp1 = x->buffer1;
				x->lap = 0;
				x->ab = 1;
			}
		}
		if(x->ab)
		{	
			if( (!x->lap && (x->wp2-x->rp2)>(2*x->bufsize/3)) | (x->lap && (((x->wp2-x->buffer2)+(x->buffer2+x->bufsize-x->rp2))>(2*x->bufsize/3))) )
			{
				t_int modulo = MODULOBUFFERINTERPOLATION-1;
				t_int m = 0;
				do
				{
					m++;
					if(m%modulo)
					{
						*(x->wp1)++ = *(x->rp2);
					}
					*(x->rp2)++ = 0;
					if (x->rp2 == x->buffer2 + x->bufsize)
					{
						x->rp2 = x->buffer2;
						x->lap = !x->lap;
					}
				}while(x->rp2 != x->wp2);
				x->rp2 = x->wp2 = x->buffer2;
				x->lap = 0;
				x->ab = 0;
			}
		}
	}
	/*...*/
	if(((x->tempseq[x->countID-1]+1)%NUMBEROFSEQUENCENUMBERS) == x->sequencenumber[x->countID-1])
	{
		x->tempseq[x->countID-1]++;
		if(x->tempseq[x->countID-1] >= NUMBEROFSEQUENCENUMBERS) x->tempseq[x->countID-1] = 0;
		t_atom *ap = argv;
		t_int i;
		if(!x->ab)
		{
			if(x->countID == 1)
			{
				for (i = 0, ap = argv; i < argc; ap++, i++) 
				{	
					if( (!x->lap && (x->rp1<=x->wp1)) | (x->lap && (x->wp1<x->rp1)) )
					{
						if(x->wasempty == 0)
						{
							*(x->wp1)++ = atom_getfloat(ap);
							if (x->wp1 == x->buffer1 + x->bufsize) 
							{
								x->wp1 = x->buffer1;
								x->lap = !x->lap;
							}
						}
						if(x->wasempty != 0)
						{
							*(x->wp1)++ = (atom_getfloat(ap) * ( (t_float)(i+1) / (t_float)argc));
							if (x->wp1 == x->buffer1 + x->bufsize) 
							{
								x->wp1 = x->buffer1;
								x->lap = !x->lap;
							}
						}
					}
				}
			}
			if( x->countID>1 && ((!x->lap && (x->wp1-x->rp1)>=SAMPLESPERBLOCK) || (x->lap && (x->wp1-x->buffer1 + x->buffer1+x->bufsize-x->rp1)>=SAMPLESPERBLOCK)) )	
			{
				t_int j = 0;
				t_sample *tempwp;
				t_float bitsinbuffer = SAMPLESPERBLOCK;
				tempwp = x->wp1; 
				if(tempwp == x->buffer1) tempwp = x->buffer1 + x->bufsize;
				if(x->wasemptyID == 0)
				{
					for (j = 0; j < bitsinbuffer; j++)
					{
						tempwp--;
						if(x->mode == 0) *tempwp = *tempwp * (x->countID-1) / x->countID + atom_getfloat(ap+((SAMPLESPERBLOCK-1)-j)) / x->countID;
						if(x->mode == 1) *tempwp = *tempwp + atom_getfloat(ap+((SAMPLESPERBLOCK-1)-j));
						if(tempwp == x->buffer1) tempwp = x->buffer1 + x->bufsize;
					}
				}
				if(x->wasemptyID == 1)
				{
					for (j = 0; j < bitsinbuffer; j++)
					{
						tempwp--;
						if(x->mode == 0) *tempwp = *tempwp * (x->countID-1) / x->countID + atom_getfloat(ap+((SAMPLESPERBLOCK-1)-j)) * (((SAMPLESPERBLOCK-1)-j) / bitsinbuffer) / x->countID;
						if(x->mode == 1) *tempwp = *tempwp + atom_getfloat(ap+((SAMPLESPERBLOCK-1)-j)) * (((SAMPLESPERBLOCK-1)-j) / bitsinbuffer);
						if(tempwp == x->buffer1) tempwp = x->buffer1 + x->bufsize;
					}
				}
			}
			if(x->wasempty != 0) 
			{ 
				x->wasempty = 0;
				x->wasemptyID = 1;
			}
		}
		if(x->ab)
		{
			if(x->countID == 1)
			{
				for (i = 0, ap = argv; i < argc; ap++, i++) 
				{	
					if( (!x->lap && (x->rp2<=x->wp2)) | (x->lap && (x->wp2<x->rp2)) )
					{
						if(x->wasempty == 0)
						{
							*(x->wp2)++ = atom_getfloat(ap);
							if (x->wp2 == x->buffer2 + x->bufsize) 
							{
								x->wp2 = x->buffer2;
								x->lap = !x->lap;
							}
						} 
						if(x->wasempty != 0)
						{
							*(x->wp2)++ = (atom_getfloat(ap) * ( (t_float)(i+1) / (t_float)argc));
							if (x->wp2 == x->buffer2 + x->bufsize) 
							{
								x->wp2 = x->buffer2;
								x->lap = !x->lap;
							}
						}
					}
				}
			}
			if(x->countID>1 && ((!x->lap && (x->wp2-x->rp2)>=SAMPLESPERBLOCK) || (x->lap && (x->wp2-x->buffer2 + x->buffer2+x->bufsize-x->rp2)>=SAMPLESPERBLOCK)) )
			{
				t_int j = 0;
				t_sample *tempwp;
				t_float bitsinbuffer = SAMPLESPERBLOCK;
				tempwp = x->wp2; 
				if(tempwp == x->buffer2) 
				{
					tempwp = x->buffer2 + x->bufsize;
				}
				if(x->wasemptyID == 0)
				{
					for (j = 0; j < bitsinbuffer; j++)
					{
						tempwp--;
						if(x->mode == 0) *tempwp = *tempwp * (x->countID-1) / x->countID + atom_getfloat(ap+((SAMPLESPERBLOCK-1)-j)) / x->countID;
						if(x->mode == 1) *tempwp = *tempwp + atom_getfloat(ap+((SAMPLESPERBLOCK-1)-j));
						if(tempwp == x->buffer2) tempwp = x->buffer2 + x->bufsize;
					}
				}
				if(x->wasemptyID == 1)
				{
					for (j = 0; j < bitsinbuffer; j++)
					{
						tempwp--;
						if(x->mode == 0) *tempwp = *tempwp * (x->countID-1) / x->countID + atom_getfloat(ap+((SAMPLESPERBLOCK-1)-j)) * (((SAMPLESPERBLOCK-1)-j) / bitsinbuffer) / x->countID;
						if(x->mode == 1) *tempwp = *tempwp + atom_getfloat(ap+((SAMPLESPERBLOCK-1)-j)) * (((SAMPLESPERBLOCK-1)-j) / bitsinbuffer);
						if(tempwp == x->buffer2) tempwp = x->buffer2 + x->bufsize;
					}
				}
			}
			if(x->wasempty != 0) 
			{ 
				x->wasempty = 0;
				x->wasemptyID = 1;
			}
		}
	} 
	else if(((x->tempseq[x->countID-1]+1)%NUMBEROFSEQUENCENUMBERS) != x->sequencenumber[x->countID-1])
	{
		if(x->countID == 1)
		{
			/*fade out*/
			if(!x->ab)
			{
				t_int j = 0;
				t_sample *tempwp;
				t_float bitsinbuffer = 0;
				if( !x->lap ) bitsinbuffer = x->wp1-x->rp1;
				if( x->lap ) bitsinbuffer = x->wp1-x->buffer1 + x->buffer1+x->bufsize-x->rp1;
				if (bitsinbuffer > SAMPLESPERBLOCK) bitsinbuffer = SAMPLESPERBLOCK;
				tempwp = x->wp1; 
				if(tempwp == x->buffer1) tempwp = x->buffer1 + x->bufsize;
				for (j = 0; j < bitsinbuffer; j++)
				{
					tempwp--;
					*tempwp = (*tempwp) * ((t_float)j / bitsinbuffer);
					if(tempwp == x->buffer1) tempwp = x->buffer1 + x->bufsize;
				}
			}
			if(x->ab)
			{
				t_int j = 0;
				t_sample *tempwp;
				t_float bitsinbuffer = 0;
				if( !x->lap ) bitsinbuffer = x->wp2-x->rp2;
				if( x->lap ) bitsinbuffer = x->wp2-x->buffer2 + x->buffer2+x->bufsize-x->rp2;
				if (bitsinbuffer > SAMPLESPERBLOCK) bitsinbuffer = SAMPLESPERBLOCK;
				tempwp = x->wp2; 
				if(tempwp == x->buffer2) tempwp = x->buffer2 + x->bufsize;
				for (j = 0; j < bitsinbuffer; j++)
				{
					tempwp--;
					*tempwp = (*tempwp) * ((t_float)j / bitsinbuffer);
					if(tempwp == x->buffer2) tempwp = x->buffer2 + x->bufsize;
				}
			}
			/*fill up with zeros*/
			if(!x->ab)
			{
				if( ((!x->lap && (x->wp1-x->rp1)<=4*SAMPLESPERBLOCK) | (x->lap && (((x->wp1-x->buffer1)+(x->buffer1+x->bufsize-x->rp1))<=4*SAMPLESPERBLOCK))) )
				{
					x->tempseq[x->countID-1]++;									
					t_int i; 
					for (i = 0; i < argc; i++) 
					{	
						if( (!x->lap && (x->rp1<=x->wp1)) | (x->lap && (x->wp1<x->rp1)) )
						{
							*(x->wp1)++ = 0;
							if (x->wp1 == x->buffer1 + x->bufsize) 
							{
								x->wp1 = x->buffer1;
								x->lap = !x->lap;
							}
						}
					}
				}
			}
			if(x->ab)
			{
				if( ((!x->lap && (x->wp2-x->rp2)<=4*SAMPLESPERBLOCK) | (x->lap && (((x->wp2-x->buffer2)+(x->buffer2+x->bufsize-x->rp2))<=4*SAMPLESPERBLOCK))) )
				{
					x->tempseq[x->countID-1]++;									
					t_int i; 
					for (i = 0; i < argc; i++) 
					{	
						if( (!x->lap && (x->rp2<=x->wp2)) | (x->lap && (x->wp2<x->rp2)) )
						{
							*(x->wp2)++ = 0;
							if (x->wp2 == x->buffer2 + x->bufsize) 
							{
								x->wp2 = x->buffer2;
								x->lap = !x->lap;
							}
						}
					}
				}
			} 
			/*fade in*/
			t_atom *ap = argv;
			t_int i;
			if(!x->ab)
			{
				for (i = 0, ap = argv; i < argc; ap++, i++) 
				{	
					if( (!x->lap && (x->rp1<=x->wp1)) | (x->lap && (x->wp1<x->rp1)) )
					{
						*(x->wp1)++ = (atom_getfloat(ap) * ( (t_float)(i+1) / (t_float)argc));
						if (x->wp1 == x->buffer1 + x->bufsize) 
						{
							x->wp1 = x->buffer1;
							x->lap = !x->lap;
						}
					}
				}
				x->wasempty = 0;
				x->wasemptyID = 1;
			}
			if(x->ab)
			{
				for (i = 0, ap = argv; i < argc; ap++, i++) 
				{	
					if( (!x->lap && (x->rp2<=x->wp2)) | (x->lap && (x->wp2<x->rp2)) )
					{
						*(x->wp2)++ = (atom_getfloat(ap) * ( (t_float)(i+1) / (t_float)argc));
						if (x->wp2 == x->buffer2 + x->bufsize) 
						{
							x->wp2 = x->buffer2;
							x->lap = !x->lap;
						}
					}
				}
				x->wasempty = 0;
				x->wasemptyID = 1;
			}
		}
		if(x->countID>1)
		{	
			if(x->nonewcolumngood == 2)
			{
				/*go back in buffer and correct the jump*/
				t_int i;
				t_sample factor;
				t_sample *help1;
				t_sample *help2;
				if(!x->ab)
				{
					if( (!x->lap && ((x->wp1-x->rp1)<5*SAMPLESPERBLOCK) && ((x->wp1-x->rp1)>5)) || (x->lap && ((x->wp1-x->buffer1 + x->buffer1+x->bufsize-x->rp1)<5*SAMPLESPERBLOCK) && ((x->wp1-x->buffer1 + x->buffer1+x->bufsize-x->rp1)>5)) )
					{
						if( (x->wp1 - (SAMPLESPERBLOCK+5)) < x->buffer1 ) help1 = x->wp1 - (SAMPLESPERBLOCK+5) + x->bufsize;
						else help1 = x->wp1 - (SAMPLESPERBLOCK+5);
						if( (x->wp1 - (SAMPLESPERBLOCK-4)) < x->buffer1 ) help2 = x->wp1 - (SAMPLESPERBLOCK-4) + x->bufsize;
						else help2 = x->wp1 - (SAMPLESPERBLOCK-4);
						factor = (*help1 - *help2) / 8; 
						for(i=1; i < 9; i++)
						{
							help2 = help1+i;
							if( help2 >= x->buffer1 + x->bufsize )
							{
								help2 = help2 - x->bufsize;
							}
							*help2 = *help1 - factor*i;
						}
					}
				}
				if(x->ab)
				{
					if( (!x->lap && ((x->wp2-x->rp2)<5*SAMPLESPERBLOCK) && ((x->wp2-x->rp2)>5)) || (x->lap && ((x->wp2-x->buffer2 + x->buffer2+x->bufsize-x->rp2)<5*SAMPLESPERBLOCK) && ((x->wp2-x->buffer2 + x->buffer2+x->bufsize-x->rp2)>5)) )
					{
						if( (x->wp2 - (SAMPLESPERBLOCK+5)) < x->buffer2 ) help1 = x->wp2 - (SAMPLESPERBLOCK+5) + x->bufsize;
						else help1 = x->wp2 - (SAMPLESPERBLOCK+5);
						if( (x->wp2 - (SAMPLESPERBLOCK-4)) < x->buffer2 ) help2 = x->wp2 - (SAMPLESPERBLOCK-4) + x->bufsize;
						else help2 = x->wp2 - (SAMPLESPERBLOCK-4);
						factor = (*help1 - *help2) / 8; 
						for(i=1; i < 9; i++)
						{
							help2 = help1+i;
							if( help2 >= x->buffer2 + x->bufsize )
							{
								help2 = help2 - x->bufsize;
							}
							*help2 = *help1 - factor*i;
						}
					}
				}
				x->nonewcolumngood = 0;
			}
			/*fade in further signals*/
			t_atom *ap = argv;
			if(!x->ab)
			{
				if(((!x->lap && (x->wp1-x->rp1)>=SAMPLESPERBLOCK) || (x->lap && (x->wp1-x->buffer1 + x->buffer1+x->bufsize-x->rp1)>=SAMPLESPERBLOCK)))
				{
					t_int j = 0;
					t_sample *tempwp;
					t_float bitsinbuffer = SAMPLESPERBLOCK;
					tempwp = x->wp1; 
					if(tempwp == x->buffer1) tempwp = x->buffer1 + x->bufsize;
					for (j = 0; j < bitsinbuffer; j++)
					{
						tempwp--;
						if(x->mode == 0) *tempwp = *tempwp * (j/bitsinbuffer) / x->countID + *tempwp * (x->countID-1) / x->countID + atom_getfloat(ap+((SAMPLESPERBLOCK-1)-j)) * (((SAMPLESPERBLOCK-1)-j)/bitsinbuffer) / x->countID;
						if(x->mode == 1) *tempwp = *tempwp + atom_getfloat(ap+((SAMPLESPERBLOCK-1)-j)) * (((SAMPLESPERBLOCK-1)-j)/bitsinbuffer);
						if(tempwp == x->buffer1) tempwp = x->buffer1 + x->bufsize;
					}
				}
			}
			if(x->ab)
			{
				if(((!x->lap && (x->wp2-x->rp2)>=SAMPLESPERBLOCK) || (x->lap && (x->wp2-x->buffer2 + x->buffer2+x->bufsize-x->rp2)>=SAMPLESPERBLOCK)))
				{
					t_int j = 0;
					t_sample *tempwp;
					t_float bitsinbuffer = SAMPLESPERBLOCK;
					tempwp = x->wp2; 
					if(tempwp == x->buffer2) tempwp = x->buffer2 + x->bufsize;
					for (j = 0; j < bitsinbuffer; j++)
					{
						tempwp--;
						if(x->mode == 0) *tempwp = *tempwp * (j/bitsinbuffer) / x->countID + *tempwp * (x->countID-1) / x->countID + atom_getfloat(ap+((SAMPLESPERBLOCK-1)-j)) * (((SAMPLESPERBLOCK-1)-j)/bitsinbuffer) / x->countID;
						if(x->mode == 1) *tempwp = *tempwp + atom_getfloat(ap+((SAMPLESPERBLOCK-1)-j)) * (((SAMPLESPERBLOCK-1)-j)/bitsinbuffer);
						if(tempwp == x->buffer2) tempwp = x->buffer2 + x->bufsize;
					}
				}
			}
		}
		x->tempseq[x->countID-1] = x->sequencenumber[x->countID-1]-1;
		x->tempseq[x->countID-1]++;
		if(x->tempseq[x->countID-1] >= NUMBEROFSEQUENCENUMBERS) x->tempseq[x->countID-1] = 0;
	}
}

static t_int *funpack_tilde_perform(t_int *w)
{
	t_sample *out = (t_sample *)(w[1]);
	t_funpack_tilde *x = (t_funpack_tilde *)(w[2]);
	t_int n = (t_int)(w[3]);	
	/*...*/
	if(!x->ab)
	{	
		if( (x->wp1 != x->rp1) && ((!x->lap && (x->wp1-x->rp1)<(x->bufsize/3)) | (x->lap && (((x->wp1-x->buffer1)+(x->buffer1+x->bufsize-x->rp1))<(x->bufsize/3)))) )
		{
			t_int m = 0;
			t_int modulo = MODULOBUFFERINTERPOLATION;
			t_sample temp = 0;
			do
			{
				if(!(m%modulo))
				{
					*(x->wp2)++ = *(x->rp1);
					temp = *(x->rp1);
					*(x->rp1)++ = 0;
					if (x->rp1 == x->buffer1 + x->bufsize)
					{
						x->rp1 = x->buffer1;
						x->lap = !x->lap;
					}
				}
				if(!((m-1)%modulo))
				{
					*(x->wp2)++ = ((temp+*(x->rp1))/2);
					if (x->rp1 == x->buffer1 + x->bufsize)
					{
						x->rp1 = x->buffer1;
						x->lap = !x->lap;
					}
					*(x->wp2)++ = *(x->rp1);
					*(x->rp1)++ = 0;
					if (x->rp1 == x->buffer1 + x->bufsize)
					{
						x->rp1 = x->buffer1;
						x->lap = !x->lap;
					}
				}
				if( (m%modulo) && ((m-1)%modulo))  
				{
					*(x->wp2)++ = *(x->rp1);
					*(x->rp1)++ = 0;
					if (x->rp1 == x->buffer1 + x->bufsize)
					{
						x->rp1 = x->buffer1;
						x->lap = !x->lap;
					}
				}
				m++;
			}while(x->rp1 != x->wp1);
			x->rp1 = x->wp1 = x->buffer1;
			x->lap = 0;
			x->ab = 1;
		}
	}
	if(x->ab)
	{	
		if( (x->wp2 != x->rp2) && ((!x->lap && (x->wp2-x->rp2)<(x->bufsize/3)) | (x->lap && (((x->wp2-x->buffer2)+(x->buffer2+x->bufsize-x->rp2))<(x->bufsize/3)))) )
		{
			t_int m = 0;
			t_int modulo = MODULOBUFFERINTERPOLATION;
			t_sample temp = 0;
			do
			{
				if(!(m%modulo))
				{
					*(x->wp1)++ = *(x->rp2);
					temp = *(x->rp2);
					*(x->rp2)++ = 0;
					if (x->rp2 == x->buffer2 + x->bufsize)
					{
						x->rp2 = x->buffer2;
						x->lap = !x->lap;
					}
				}
				if(!((m-1)%modulo))
				{
					*(x->wp1)++ = ((temp+*(x->rp2))/2);
					if (x->rp2 == x->buffer2 + x->bufsize)
					{
						x->rp2 = x->buffer2;
						x->lap = !x->lap;
					}
					*(x->wp1)++ = *(x->rp2);
					*(x->rp2)++ = 0;
					if (x->rp2 == x->buffer2 + x->bufsize)
					{
						x->rp2 = x->buffer2;
						x->lap = !x->lap;
					}
				}
				if( (m%modulo) && ((m-1)%modulo))  
				{
					*(x->wp1)++ = *(x->rp2);
					*(x->rp2)++ = 0;
					if (x->rp2 == x->buffer2 + x->bufsize)
					{
						x->rp2 = x->buffer2;
						x->lap = !x->lap;
					}
				}
				m++;
			}while(x->rp2 != x->wp2);
			x->rp2 = x->wp2 = x->buffer2;
			x->lap = 0;
			x->ab = 0;
		}
	}
	/*...*/ 
	if(!x->ab)
	{
		t_float bitsinbuffer = 0;
		t_float fadeout = 0;
		if( !x->lap ) bitsinbuffer = x->wp1-x->rp1;
		if( x->lap ) bitsinbuffer = x->wp1-x->buffer1 + x->buffer1+x->bufsize-x->rp1;
		fadeout = bitsinbuffer;
		if( (x->wp1 != x->rp1) && ((!x->lap && (x->wp1-x->rp1)<=SAMPLESPERBLOCK) | (x->lap && (((x->wp1-x->buffer1)+(x->buffer1+x->bufsize-x->rp1))<=SAMPLESPERBLOCK))) )
		{
			while(n--)
			{
				if( (!x->lap && (x->rp1<x->wp1)) | (x->lap && (x->wp1<=x->rp1)) )
				{
					*out++ = ((*(x->rp1)) * (fadeout/bitsinbuffer));
					fadeout--;
					*(x->rp1)++ = 0;
					if (x->rp1 == x->buffer1 + x->bufsize)
					{
						x->rp1 = x->buffer1;
						x->lap = !x->lap;
					}
					if(x->wasempty != 1) x->wasempty = 1;	
				}
				else
				{
					*out++ = 0;
					if(x->wasempty != 1) x->wasempty = 1;
				}
			}
		}
		else 
		{	
			while(n--)
			{
				if( (!x->lap && (x->rp1<x->wp1)) | (x->lap && (x->wp1<=x->rp1)) )
				{
					*out++ = *(x->rp1);
					*(x->rp1)++ = 0;
					if (x->rp1 == x->buffer1 + x->bufsize)
					{
						x->rp1 = x->buffer1;
						x->lap = !x->lap;
					}
				}
				else
				{
					*out++ = 0;
					if(x->wasempty != 1) x->wasempty = 1;
				}
			}
		}
	}
	if(x->ab)
	{
		t_float bitsinbuffer = 0;
		t_float fadeout = 0;
		if( !x->lap ) bitsinbuffer = x->wp2-x->rp2;
		if( x->lap ) bitsinbuffer = x->wp2-x->buffer2 + x->buffer2+x->bufsize-x->rp2;
		fadeout = bitsinbuffer;
		if( (x->wp2 != x->rp2) && ((!x->lap && (x->wp2-x->rp2)<=SAMPLESPERBLOCK) | (x->lap && (((x->wp2-x->buffer2)+(x->buffer2+x->bufsize-x->rp2))<=SAMPLESPERBLOCK))) )
		{
			while(n--)
			{
				if( (!x->lap && (x->rp2<x->wp2)) | (x->lap && (x->wp2<=x->rp2)) )
				{
					*out++ = ((*(x->rp2)) * (fadeout/bitsinbuffer));
					fadeout--;
					*(x->rp2)++ = 0;
					if (x->rp2 == x->buffer2 + x->bufsize)
					{
						x->rp2 = x->buffer2;
						x->lap = !x->lap;
					}
					if(x->wasempty != 1) x->wasempty = 1;
				}
				else
				{
					*out++ = 0;
					if(x->wasempty != 1) x->wasempty = 1;;
				}
			}
		}
		else 
		{	
			while(n--)
			{
				if( (!x->lap && (x->rp2<x->wp2)) | (x->lap && (x->wp2<=x->rp2)) )
				{
					*out++ = *(x->rp2);
					*(x->rp2)++ = 0;
					if (x->rp2 == x->buffer2 + x->bufsize)
					{
						x->rp2 = x->buffer2;
						x->lap = !x->lap;
					}
				}
				else
				{
					*out++ = 0;
					if(x->wasempty != 1) x->wasempty = 1;
				}
			}
		}
	}
	return(w+4);
}

static void funpack_tilde_dsp(t_funpack_tilde *x, t_signal **sp)
{
	dsp_add(funpack_tilde_perform, 3, sp[0]->s_vec, x, sp[0]->s_n);
}

static void *funpack_tilde_new(t_floatarg f)
{
	t_int k;
	t_funpack_tilde *x = (t_funpack_tilde *)pd_new(funpack_tilde_class);
	
	x->lap= x->ab = 0;
	x->bufsize = MODULOBUFFERINTERPOLATION*SAMPLESPERBLOCK;	
	x->buffer1 = (t_sample *)getbytes(x->bufsize * sizeof(*x->buffer1));
	x->buffer2 = (t_sample *)getbytes(x->bufsize * sizeof(*x->buffer2));
	x->rp1 = x->wp1 = x->buffer1;
	x->rp2 = x->wp2 = x->buffer2;
	
	x->wasempty=0;
	x->wasemptyID=0;
	
	x->countID = 0;
	x->IDerror = 0;
	x->nonewcolumngood = 0;
	
	for(k=0; k<MAXNUMBEROFIDS; k++) 
	{
    	x->sequencenumber[k]=0;
		x->tempseq[k]=NUMBEROFSEQUENCENUMBERS-1;
		x->ID[k]=0;
	}
	
	x->mode = 0;
		
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, gensym("float"), gensym("sequencenumber"));
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, gensym("float"), gensym("ID"));
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, gensym("float"), gensym("mode"));
	outlet_new(&x->x_obj, gensym("signal"));
	return(x);
}

void funpack_tilde_setup(void)
{
	funpack_tilde_class = class_new(gensym("funpack~"), (t_newmethod)funpack_tilde_new, 0,
							  sizeof(t_funpack_tilde), 0, A_DEFFLOAT, 0);
	class_addmethod(funpack_tilde_class, (t_method)funpack_tilde_dsp, gensym("dsp"), 0);
	class_addmethod(funpack_tilde_class, (t_method)funpack_tilde_sequencenumber, gensym("sequencenumber"), A_DEFFLOAT, 0);
	class_addmethod(funpack_tilde_class, (t_method)funpack_tilde_ID, gensym("ID"), A_DEFFLOAT, 0);
	class_addmethod(funpack_tilde_class, (t_method)funpack_tilde_mode, gensym("mode"), A_DEFFLOAT, 0);
	class_addlist(funpack_tilde_class, (t_method)funpack_tilde_list);
}
