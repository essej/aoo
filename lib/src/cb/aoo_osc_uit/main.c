#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
/* #include <math.h> */

#include "aoo/aoo.h"
#include "aoo/aoo_osc.h"


/* for a kind of unit test of this module */

#define uit_error(f, ...) {fprintf(stderr,"ERROR:"f"\n",  __VA_ARGS__); return -1;}
int aoo_verbosity = AOO_VERBOSITY_DEBUG;
#define PRINT_BYTES_MAX 16

int process_channel(unsigned int d, unsigned int c, osc_timetag tt,
                aoo_format_parameter *fp,aoo_channel_parameter *cp, osc_blob b)
{
    int n,cnt;
    unsigned char *cb = (unsigned char *)b;

    printf("process drain %d, channel %d:\n",d,c);
    printf("   timetag: %llu = %u sec %u frac\n",tt.val,tt.time.sec,tt.time.frac);
    printf("   format samplerate:%d\n",fp->samplerate);
    printf("   format blocksize:%d\n",fp->blocksize);
    printf("   format overlap:%d\n",fp->overlap);
    printf("   format mimetype:%s\n",fp->mimetype);
    printf("   format time_correction:%f\n",fp->time_correction);

    printf("   channel id:%d\n",cp->id);
    printf("   channel sequence:%d\n",cp->sequence);
    printf("   channel resolution:%d\n",cp->resolution);
    printf("   channel resampling:%d\n",cp->resampling);
    printf("   channel blobsize:%d\n",cp->blobsize);
    printf("   channel blob:%p\n",b);

    cnt=(cp->blobsize>PRINT_BYTES_MAX)?PRINT_BYTES_MAX:cp->blobsize;
    printf("   channel blob data");
    for(n=0;n < cnt;n++)
        printf(":%02d",cb[n]);
    printf("\n");

    return 0;
}

int main()
{
    int ret;
    int n,i;
    float fret;
    char s[AOO_MAX_NAME_LEN];
    osc_drain *d;
    osc_src *src;
    unsigned char *data;

    /* test help functions */

    if(aoo_size4(3) != 4)
        uit_error("wrong aoo_size4 %d instead of %d",aoo_size4(3),4);
    if(aoo_size4(20) != 20)
        uit_error("wrong aoo_size4 %d instead of %d",aoo_size4(20),20);
    if(aoo_size4(4) != 4)
        uit_error("wrong aoo_size4 %d instead of %d",aoo_size4(3),4);

    osc_drain_set_string(s,7);
    if(strcmp("0007",s)!=0)
        uit_error("drain=7:%s\n",s);

    osc_channel_set_string(s,999);
    if(strcmp("0999",s)!=0)
       uit_error("channel=999:%s\n",s);

    if((ret=aoo_resbits(0)) != 32)uit_error("aoo_resbits(0)=%d",ret);
    if((ret=aoo_resbits(1)) != 64)uit_error("aoo_resbits(0)=%d",ret);
    if((ret=aoo_resbits(2)) != 32)uit_error("aoo_resbits(0)=%d",ret);
    if((ret=aoo_resbits(9)) != 9)uit_error("aoo_resbits(0)=%d",ret);
    if((ret=aoo_resbits(77)) != 77)uit_error("aoo_resbits(0)=%d",ret);


    if((fret=aoo_resampling_fakt(0)) != 1.0)
        uit_error("aoo_resampling_fakt(0)=%f",fret);
    if((fret=aoo_resampling_fakt(1)) != 1.0)
        uit_error("aoo_resampling_fakt(1)=%f",fret);
    if((fret=aoo_resampling_fakt(2)) != 2.0)
        uit_error("aoo_resampling_fakt(2)=%f",fret);
    if((fret=aoo_resampling_fakt(-2)) != 1.0/2.0)
        uit_error("aoo_resampling_fakt(-2)=%f",fret);
    if((fret=aoo_resampling_fakt(-3)) != (float) (-1.0/-3.0))
        uit_error("aoo_resampling_fakt(-3)=%f",fret);
    if((fret=aoo_resampling_fakt(99)) != 99.0)
        uit_error("aoo_resampling_fakt(99)=%f",fret);


    if((ret=aoo_blobsize_bytes(4,3)) != 2)
        uit_error("blobsize_bytes(4,3)=%d",ret);
    if((ret=aoo_blobsize_bytes(8,3)) != 3)
        uit_error("blobsize_bytes(4,3)=%d",ret);

    if((ret=aoo_resample_blocksize(8,2))!=16)
           uit_error("aoo_resample_blocksize(8,2)=%d",ret);
    if((ret=aoo_resample_blocksize(8,-2))!=4)
           uit_error("aoo_resample_blocksize(8,-2)=%d",ret);

    /* src test standard use 2 channels, float */

    if(!(src = osc_src_new(1,2)))
       uit_error("osc_src_new(1,2):%p",src);

    for(n=0;n<2;n++){
        data = (unsigned char *) (* src->channel_data)[n];
        for(i=0;i<(* src->channel)[n]->blobsize;i++)
             data[i] = (unsigned char) n+i;
    }

    /* drain ops */
    if(!(d = osc_drain_new(1,2,&process_channel)))
       uit_error("osc_drain_new(1,2)%p",d);

    if((ret=osc_drain_parse(d,src->bundlesize,src->bundle))<=0)
        uit_error("osc_drain_parse: %d",ret);

    osc_drain_free(d);
    osc_src_free(src);

   /* mixed cahnnels test */
    if(!(src = osc_src_new(2,2)))
       uit_error("osc_src_new(1,2):%p",src);


    /* drain channel 0, float, resampled no */
    osc_src_set_channel(src,0,0,10,0,0);
    /* drain channel 4, byte, downsampled by 2 */
    osc_src_set_channel(src,1,4,14,8,-2);

    for(n=0;n<2;n++){
        data = (unsigned char *) (* src->channel_data)[n];
        for(i=0;i<(* src->channel)[n]->blobsize;i++)
             data[i] = (unsigned char) n+i;
    }

    osc_src_timetag(src,TIMETAG_IMMIDIATELY);

    /* drain 5 channels  */
    if(!(d = osc_drain_new(2,5,&process_channel)))
       uit_error("osc_drain_new(1,2)%p",d);

    if((ret=osc_drain_parse(d,src->bundlesize,src->bundle))<=0)
        uit_error("osc_drain_parse: %d",ret);

    osc_drain_free(d);
    osc_src_free(src);

    printf("suceeded\n");
    return 0;
}
