/* Copyright (c) 2014 Winfried Ritsch
 *
 * This library is covered by the LGPL, read licences
 * at <http://www.gnu.org/licenses/>  for details
 *
 */
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



/* static prototypes */
static unsigned int make_format_head(char  **sptr,char* drainname);
static unsigned int make_channel_head(char **sptr,char* drainname,char* ch_name);
static unsigned int construct_bundle(osc_src *src);

/*****************************************************************************
  Function:	osc_drain_new

  Summary:  generates new storage for a osc drain parser, for one drain.

  Description:
	for each drain one call is needed.

  Precondition:
  Parameters: None
  Returns: osc_drain *, used in parser or NULL if failure

  Remarks: drain numbers and channel numbers has to be from 0 to 9999
 ***************************************************************************/


osc_drain* osc_drain_new(unsigned int drain, unsigned int channels,
                                 int (* process_channel)(unsigned int,unsigned int,
                                                         osc_timetag,
                                                         aoo_format_parameter *,
                                                         aoo_channel_parameter *,
                                                         osc_blob))
{
    int i,n;
    char sd[AOO_MAX_NAME_LEN],sc[AOO_MAX_NAME_LEN];

    osc_drain* osc;

    if (!(osc = malloc(sizeof(osc_drain))))
        return NULL;

    osc->timetag.val = 0ul; /* default */

    /* free routine need  NULL pointers on unallocated memory */
    osc->format_head = NULL;
    osc->channel_head_size = NULL;
    osc->channel_head = NULL;
    osc->channel_parameter = NULL;
    osc->channel_data = NULL;

    if(drain > 9999) {
        if(aoo_verbosity > AOO_VERBOSITY_INFO)
            fprintf(stderr,"osc_drain_new: drain number to high");
        free(osc);
        return NULL;
    }
    osc->drain = drain;

    if(channels > 9999) {
        if(aoo_verbosity > AOO_VERBOSITY_INFO)
            fprintf(stderr,"osc_drain_new: number of channels to high");
        free(osc);
        return NULL;
    }
    osc->channels = channels;

    /* set process_channel must be NULL if not used */
    osc->process_channel = process_channel;

    /* contruct format head cache */
    osc_drain_set_string(sd,drain);
    if(!(n = make_format_head(&osc->format_head,sd))) {
        free(osc);
        return NULL;
    }
    osc->format_head_size = n;

    /* contruct channel headers */
    if((osc->channel_head_size = calloc(channels,sizeof(osc_int))) == NULL) {
        osc_drain_free(osc);
        return NULL;
    }

    if((osc->channel_head = calloc(channels,sizeof(osc_data *))) == NULL) {
        osc_drain_free(osc);
        return NULL;
    }

    if((osc->channel_parameter = calloc(channels,sizeof(aoo_channel_parameter)))
            == NULL) {
        osc_drain_free(osc);
        return NULL;
    }

    if((osc->channel_data = calloc(channels,sizeof(osc_blob))) == NULL) {
        osc_drain_free(osc);
        return NULL;
    }

    for(i=0; i<channels; i++) {

        osc_channel_set_string(sc,i);
        if(!(n = make_channel_head(&(*osc->channel_head)[i],sd,sc))) {
            osc_drain_free(osc);
            return NULL;
        }
        (* osc->channel_head_size)[i] = n;
    }

    return osc;
}


/***************************************************************************
  Function: osc_drain_parse

  Summary:
        receives OSC packages for a drain and extract audio data

  Description:
        test package for valid OSC message, parse it return audiodata

  Precondition: osc_drain_new called
  Parameters: drain, data with datalen to parse
  Returns: OSC_PARSE_NO_MATCH if no match with drain
           OSC_PARSE_NOT_VALID  if bundle not an AoO message
           OSC_NO_CHANNELS if matched but no channels parsed
           or positiv number of channel parsed.

  Remarks: parser can be called with different drain numbers in sequence
    if one returns OSC_PARSE_NO_MATCH
 ***************************************************************************/

osc_parser_ret osc_drain_parse(osc_drain* osc,
                             unsigned int datalen, void* data)
{
    unsigned int msglen;
    unsigned int channel,processed;
    char* readptr = (char *) data;
    char* endptr = readptr + datalen;
    char* addrptr = NULL;

    /* at least bundle and format message */
    if(datalen < (sizeof(OSC_BUNDLE) + sizeof(osc_timetag) + sizeof(osc_int)
                  + osc->format_head_size + sizeof(aoo_format_parameter)))
        return OSC_PARSE_NOT_VALID;

    if (strcmp(readptr, OSC_BUNDLE) != 0)
        return OSC_PARSE_NOT_VALID;

    readptr += sizeof(OSC_BUNDLE);
    osc->timetag.val = *((uint64_t *) readptr);
    readptr += sizeof(osc_timetag);
    /* dont know how to validate timetag, so i dont */

    /* --- first always format message with matching drain number --- */
    msglen = *((osc_int *) readptr);
    addrptr = readptr += sizeof(osc_int);
    if((addrptr+(osc->format_head_size+sizeof(aoo_format_parameter))) > endptr)
        return OSC_PARSE_NOT_VALID;
    if(memcmp(osc->format_head,readptr,osc->format_head_size) != 0)
        return OSC_PARSE_NO_MATCH;

    readptr += osc->format_head_size;

    memcpy(&osc->format ,readptr,sizeof(aoo_format_parameter));

    /* how to validate format ? - leave it, except mimetype:
     * read mimetype (only one now is "audio/pcm" hardcoded as AOO_MIME_PCM
     * it will be changed in future if compression formats are added)
     */

    if(memcmp(osc->format.mimetype,AOO_MIME_PCM,
               sizeof(AOO_MIME_PCM)) != 0)
        return OSC_PARSE_MIME_UNKOWN;
    readptr = addrptr + msglen; /* next message */

    /* --- channel messages --- */
    channel=0;
    processed=0;
    while(readptr < endptr) {
        msglen = *((osc_int *) readptr);
        addrptr = readptr += sizeof(osc_int);

        /* check for msgsize correctness is not done, but if data is enough */
        if(readptr+msglen > endptr)
            break;

        size_t ds;
        /* not all channels have to be in bundle */
        while(channel<osc->channels) {

            if(memcmp(readptr,(*osc->channel_head)[channel],
                      (*osc->channel_head_size)[channel]) != 0) {
                channel++;
                continue; /* next one */
            }
            /* found channel */
            readptr += (* osc->channel_head_size)[channel];
            memcpy(&(*osc->channel_parameter)[channel],readptr,
                   sizeof(aoo_channel_parameter));

            readptr += sizeof(aoo_channel_parameter);

            if((ds = (* osc->channel_parameter)[channel].blobsize) == 0)
                break; // next message

            if((readptr + ds) > endptr) /* to be sure */
                break;

            /* parsed data does not vanish until processed, so no copy anymore !

            if((osc->channel.data[channel] = malloc(ds)) == NULL)
                break;
            else
                memcpy(osc->channel.data[channel],readptr, ds);
            */
            (* osc->channel_data)[channel] = (osc_blob) readptr;

            /* see if process_channel can do something with this data */
            if(osc->process_channel)
                if((*osc->process_channel)(osc->drain,channel,osc->timetag,
                                        &osc->format,
                                        &(* osc->channel_parameter)[channel],
                                        (* osc->channel_data)[channel]) >= 0)
                    processed++;

            break; // next message
        }; /* channel messages */

        /* next channel must have another channel number increasing */
        channel++;
        readptr = addrptr+msglen;
    }; /* readptr < endptr */

    if(processed > 0)
        return processed;
    return OSC_PARSE_NO_CHANNELS;
}

/*****************************************************************************
  Function: void osc_drain_free

  Summary:
    free storage for osc drain parser.

  Description:
    free allocated memory

  Precondition: None
  Parameters: pointer to drain storage

  Returns: None

  Remarks: can be called even not all memory is located
 ***************************************************************************/

void osc_drain_free(osc_drain* osc)
{
    int i;
    if(osc == NULL)
        return;

    /* free memory */
    if(osc->format_head)
        free(osc->format_head);

    if(osc->channel_head)
        for(i=0; i<osc->channels; i++)
            if((* osc->channel_head)[i])
                free((* osc->channel_head)[i]);

    free(osc->channel_head);

    if(osc->channel_data)
        free(osc->channel_data);

    if(osc->channel_parameter)
        free(osc->channel_parameter);

    free(osc);
    return;
}

/*****************************************************************************
  Function:
    void osc_drain_announce

  Summary:
    send drain Announce Message

  Description: invite others on the net, to use this drain
    should be broadcasted.

  Precondition: osc_drain_new()
  Parameters: pointer to drain
  Returns: None

  Remarks: for UDP IP and other Info to the network or client must be
    extracted from UDP info or some other used protocoll
 ***************************************************************************/

unsigned int osc_drain_announce(osc_drain* drain)
{

    if(!drain)
        return 0;

    /* Not implemented for now

        osc_announce_data.id.v[3] = AOO_ID;

        osc_announce_data.count.v[3] = announce_count.v[0];
        osc_announce_data.count.v[2] = announce_count.v[1];
        osc_announce_data.count.v[1] = 0;
        osc_announce_data.count.v[0] = 0;
        announce_count.Val++;

        memcpy((void *) soc_announce_data.name, (void *) AppConfig.NetBIOSName, 16);
        osc_announce_data.name[15] = 0;
    */
    return 1;
}


/* ======================= SOURCES ========================================= */

/*****************************************************************************
  Function: osc_src_new

  Summary: allocates  and construct a new src with bundle

  Description:
        allocates data variables and data space to  be
        used for OSC transmit.

  Precondition: None
  Parameters: drain number, channels to transmit
  Returns: pointer to osc_src to be used for send

  Remarks: channels is not the number of channels the drain has,
           but number of channels to be send. Use channel
           numbers for send to specific channels in the drain.
           There is no check for correct channels number and number used !
 ***************************************************************************/
osc_src *osc_src_new(unsigned int drain,unsigned int channels)
{
    char sd[AOO_MAX_NAME_LEN],sc[AOO_MAX_NAME_LEN];

    osc_src *src = NULL;
    unsigned int n;

    if(channels == 0 || channels > AOO_MAX_CHANNELS || drain > AOO_MAX_DRAIN)
        return NULL;

    if (!(src = calloc(1,sizeof(osc_src))))
        return NULL;

    src->channels = channels;
    src->drain = drain;

    if((src->ichannel =calloc(channels,sizeof(aoo_channel_parameter))) == NULL)
        goto exit_src_fail;
    if((src->ichannel_nr =calloc(channels,sizeof(unsigned int))) == NULL)
        goto exit_src_fail;
    if((src->ch_head =calloc(channels,sizeof(char *))) == NULL)
        goto exit_src_fail;
    if((src->ch_headlen = calloc(channels,sizeof(unsigned int))) == NULL)
        goto exit_src_fail;
    if((src->channel = calloc(channels,sizeof(aoo_channel_parameter *))) == NULL)
        goto exit_src_fail;
    if((src->channel_data = calloc(channels,sizeof(osc_blob *))) == NULL)
        goto exit_src_fail;

    /* prepare data len */
    src->bundle_len = aoo_size4(sizeof(OSC_BUNDLE)) + sizeof(osc_timetag) + sizeof(osc_int);

    osc_drain_set_string(sd,drain);
    if(!(src->fmt_headlen = make_format_head(&src->fmt_head,sd)))
        goto exit_src_fail;

    src->format_len = src->fmt_headlen + sizeof(aoo_format_parameter);

    src->iformat.samplerate = AOO_FORMAT_DEFAULT_SAMPLERATE;
    src->iformat.blocksize = AOO_FORMAT_DEFAULT_BLOCKSIZE;
    src->iformat.overlap = AOO_FORMAT_DEFAULT_OVERLAP;
    memcpy(src->iformat.mimetype,AOO_MIME_PCM,AOO_MIME_SIZE);
    src->iformat.time_correction= AOO_TIME_CORRECTION_NO;

    for(n=0;n<channels;n++){
        src->ichannel_nr[n] = n;
        osc_channel_set_string(sc,n);
        if(!((* src->ch_headlen)[n] = make_channel_head(&(* src->ch_head)[n],sd,sc)))
            goto exit_src_fail;
        src->ichannel[n].id = AOO_CHANNEL_DEFAULT_ID;
        src->ichannel[n].sequence = 0;
        src->ichannel[n].resolution = AOO_CHANNEL_DEFAULT_RESOLUTION;
        src->ichannel[n].resampling = AOO_CHANNEL_DEFAULT_RESOLUTION;
        src->ichannel[n].blobsize = 0;
    }

    /* construct a default bundle, changed with channels parameter */
    if(construct_bundle(src))
        return src;

exit_src_fail:
    osc_src_free(src);
    return NULL;
}


/*****************************************************************************
  Function: osc_src_free

  Summary: free allocated memory of a source.

  Description:
        can be called within any state if src is allocated

  Precondition: None
  Parameters: src
  Returns: none

  Remarks: if a memory is not allocated it has to be NULL
 ***************************************************************************/

void osc_src_free(osc_src *src)
{
    int n;

    if(!src)
        return;

    if(src->ichannel)
        free(src->ichannel);

    if(src->ichannel_nr)
        free(src->ichannel_nr);

    if(src->fmt_head)
        free(src->fmt_head);
    if(src->ch_head){
        for(n=0;n<src->channels;n++)
            if((* src->ch_head)[n])
                free((* src->ch_head)[n]);
        free(src->ch_head);
    }
    if(src->ch_headlen)
        free(src->ch_headlen);
    if(src->channel)
        free(src->channel);
    if(src->channel_data)
        free(src->channel_data);
    if(src->bundle)
        free(src->bundle);

    if(src)
        free(src);
    return;
}


/*****************************************************************************
  Function: osc_src_set_format

  Summary: changes the format information of a src

  Description:
        Sets the format paramter in src internal and in the bundle.
        Allocates a new bundle if blocksize is changed
        Any paramter which is lower or equal 0, will be ignored.

  Precondition: osc_src_new
  Parameters: osc_src, samplerate, blocksize, overlap

  Returns: size of new bundle

  Remarks: src->bundle has to be NULL if not already allocated !
 ***************************************************************************/
unsigned int osc_src_set_format(osc_src *src, osc_int samplerate,
               osc_int blocksize, osc_int overlap)
{
    if(!src) return 0;
    if(samplerate > 0)
        src->iformat.samplerate=src->format->samplerate=samplerate;

    if(overlap > 0 && overlap != src->iformat.overlap)
        src->iformat.overlap=src->format->overlap=overlap;

    if(blocksize > 0 && src->iformat.blocksize != blocksize){
        src->iformat.blocksize=blocksize;
        construct_bundle(src);
    }
    return src->bundlesize;
}

/*****************************************************************************
  Function: osc_src_set_channel

  Summary: changes the channel paramter information of a source

  Description:
        Sets the channel parameter for a channel in src internal
        and in the bundle.
        Allocates a new bundle if blobsize is changed
        Any paramter which is lower 0, will be ignored.
        Note

  Precondition: osc_src_new
  Parameters: osc_src, channel to send, channel nr in drain,
              id, resolution, resampling

  Returns: size of new bundle

  Remarks: src->bundle has to be NULL if not already allocated !
 ***************************************************************************/
unsigned int osc_src_set_channel(osc_src *src,
                         unsigned int send_channel, unsigned int drain_channel,
                         osc_int id, osc_int resolution, osc_int resampling)
{
    char sd[AOO_MAX_NAME_LEN],sc[AOO_MAX_NAME_LEN];
    char *s;
    int len,n=send_channel;
    bool new_bundle = false;

    if(!src && n >= src->channels)
        return 0;

    if(id >= 0)
        src->ichannel[n].id=(* src->channel)[n]->id=id;

    if(resolution >= 0 && src->ichannel[n].resolution != resolution){
        src->ichannel[n].resolution=resolution;
        new_bundle = true;
    }

    if(src->ichannel[n].resampling != resampling){
        src->ichannel[n].resampling = resampling;
        new_bundle = true;
    }

    if(drain_channel <  AOO_MAX_CHANNELS
       && src->ichannel_nr[n] != drain_channel){

        osc_channel_set_string(sc,drain_channel);
        osc_drain_set_string(sd,src->drain);

        if((*src->ch_head)[n])
            free((*src->ch_head)[n]);
        (*src->ch_head)[n] = NULL;

        if((len = make_channel_head(&s,sd,sc))){
            (* src->ch_head)[n] = s;
            (* src->ch_headlen)[n] = len;
            src->ichannel_nr[n] = drain_channel;
            new_bundle = true;
        }
    }

    if(new_bundle)
        construct_bundle(src);

    return (*src->channel)[n]->blobsize;
}

/*****************************************************************************
  Function: osc_src_get_blobs

  Summary: returns a pointer to array of data pointer to channel blobs

  Description:
        you can also use the pointer in the struct and is here for
        ortogonal function set, without knowing the structure

  Precondition: osc_src_new
  Parameters: osc_src

  Returns: pointer to array of pointers

  Remarks: No checks since have to be fast, you have to know the size
           returned in set channel before or use a macro
 ***************************************************************************/
inline osc_blob osc_src_get_blobs(osc_src *src)
{
    return (osc_blob) src->channel_data;
}


/*****************************************************************************
  Function: osc_blob2float

  Summary: converts a blob to a array of floats respecting resolution


  Description:
    returns a pointer to array of samples in float from a blob,
    if conversion is needed, else the pointer to the blob with floats.


  Precondition:
  Parameters: number of samples, resolution, pointer to blob, pointer to floats

  Returns: pointer to array of floats or NULL if resolution not supported

  Remarks:  dangerous code, since dptr maybe not used to store samples
 ***************************************************************************/

aoo_float_t *osc_blob2float(unsigned int n,int res,osc_blob b,aoo_float_t **dptr)
{
    if(res == AOO_RESOLUTION_FLOAT)
        return (aoo_float_t *) b;

/*  memory leak, so now we need from extern
    if((d=malloc(n*sizeof(aoo_float_t))==NULL)
       return NULL;
*/
    if(res == AOO_RESOLUTION_DOUBLE){
        aoo_double_t *dd = (aoo_double_t *)b;
        aoo_float_t *d = *dptr;
        while(n--)
            d[n]=dd[n];
        return d;
    };

/* here comes the extract bits out of blob code if needed some time */
/*
    if(res >= AOO_RESOLUTION_MIN_BITS){
    ...here the "extract bits out of blob" code to be contributed, if needed some time...
    }
*/
    return NULL;

}

/*****************************************************************************
  Function: osc_float2blob

  Summary: converts an array of floats to blob to a respecting the resolution

  Description:
    writes an array of floats to a blob storage converting to the proper format

  Precondition:
  Parameters: number of samples, resolution, pointer to float storage, pointer to blob data

  Returns: pointer to array of floats or NULL if resolution not supported

  Remarks:  dangerous code, since dptr maybe not used to store samples
 ***************************************************************************/

bool osc_float2blob(unsigned int n,int res,aoo_float_t *d,osc_blob b)
{
    if(res == AOO_RESOLUTION_FLOAT){
        memccpy(d,b,n,sizeof(AOO_RESOLUTION_FLOAT));
        return true;
    };

    if(res == AOO_RESOLUTION_DOUBLE){
        aoo_double_t *dd = (aoo_double_t *)b;
        while(n--)
            dd[n]=d[n];
        return true;
    };

/* here comes the extract bits out of blob code if needed some time */
/*
    if(res >= AOO_RESOLUTION_MIN_BITS){
    ...here the "extract bits out of blob" code to be contributed, if needed some time...
    }
*/
    return false;
}



/* ================================= internal  helpers ======================== */

/* expand an osc string to a 4 byte boundary by \0, return size */
unsigned int aoo_string_expand4(char* sptr)
{
    unsigned int n;
    if((n = strlen(sptr)) % 4) {
        while(n++%4)
            sptr[n-1]='\0';
        n--;
    }
    return n;
}

static unsigned int make_format_head(char **sptr,char* drainname)
{
    unsigned int len,n;
    char *s;

    len = sizeof(AOO_DRAIN) - 1;
    len += strlen(drainname);
    len += sizeof(AOO_FORMAT) - 1;
    len = aoo_size4(len+1); // one zero at end needed
    len += aoo_size4(sizeof(AOO_FORMAT_TT_TC)); // zero included

    if((*sptr = s = malloc(len)) == NULL)
        return 0;

    strcpy(s,AOO_DRAIN);
    strcat(s,drainname);
    strcat(s,AOO_FORMAT);

    /* extent to 4 byte boundary */
    n = aoo_string_expand4(s);
    s += n;
    memcpy(s,AOO_FORMAT_TT_TC,sizeof(AOO_FORMAT_TT_TC));
    n += sizeof(AOO_FORMAT_TT_TC);
    return n;
}

static unsigned int make_channel_head(char **sptr,char* drainname,char* ch_name)
{
    unsigned int len,n;
    char *s;

    len = aoo_size4(sizeof(AOO_DRAIN)-1 + strlen(drainname)
                    + sizeof(AOO_CHANNEL)-1 +strlen(ch_name) +1);
    len += aoo_size4(sizeof(AOO_CHANNEL_TT));

    if((*sptr = s = malloc(len)) == NULL)
        return 0;

    strcpy(s,AOO_DRAIN);
    strcat(s,drainname);
    strcat(s,AOO_CHANNEL);
    strcat(s,ch_name);
    /* extent to 4 byte boundary */
    n = aoo_string_expand4(s);
    s += n;
    memcpy(s,AOO_CHANNEL_TT,sizeof(AOO_CHANNEL_TT));
    n += sizeof(AOO_CHANNEL_TT);

    return n;
}


/* construct a bundle on continous data from osc_src info */

static unsigned int construct_bundle(osc_src *src)
{
    unsigned int n,len,blobsize;
    char *bptr = NULL;

    if(!src)
        return 0;

    /* calculate new len */
    len = src->bundle_len + src->format_len;

    for(n=0;n<src->channels;n++){

        /* align blocksize with stuffed bits of  resampled samples on 4 byte border */
        blobsize=aoo_blobsize_bytes(src->iformat.blocksize,
                                    aoo_resbits(src->ichannel[n].resolution));
        blobsize=aoo_resample_blocksize(blobsize,src->ichannel[n].resampling);

        src->ichannel[n].blobsize = aoo_size4(blobsize);
        len += sizeof(osc_int)+(* src->ch_headlen)[n]+sizeof(aoo_channel_parameter)
               + src->ichannel[n].blobsize;
    }

    if(len > AOO_MAX_BUNDLE_LEN)
        goto exit_contruct_fail;

    /* already a bundle allocated, free first */

    /* allocate bundle */
    if(!(bptr =  malloc(len)))
        goto exit_contruct_fail;

    src->bundlesize = len;
    if(src->bundle)
        free(src->bundle);
    src->bundle = (osc_blob) bptr;

    /* bundle header */
    memcpy(bptr,OSC_BUNDLE,sizeof(OSC_BUNDLE));
    bptr +=  sizeof(OSC_BUNDLE);

    src->timetag = (osc_timetag *) bptr;
    bptr +=  sizeof(osc_timetag);
    (*src->timetag).val = TIMETAG_NO;

    /* format message size */
    *((osc_int *) bptr) = src->fmt_headlen + sizeof(aoo_format_parameter);
    bptr += sizeof(osc_int);

    /* format message */
    memcpy(bptr,src->fmt_head,src->fmt_headlen);
    bptr +=  src->fmt_headlen;
    src->format = (aoo_format_parameter *) bptr;
    memcpy(bptr,&src->iformat,sizeof(aoo_format_parameter));
    bptr +=  sizeof(aoo_format_parameter);

    for(n=0;n<src->channels;n++){

        /* channel message size */
        *((osc_int *) bptr) = (* src->ch_headlen)[n]+ sizeof(aoo_channel_parameter)
                              + src->ichannel[n].blobsize;
        bptr += sizeof(osc_int);

        /* channel message */
        memcpy(bptr,(* src->ch_head)[n],(* src->ch_headlen)[n]);
        bptr +=  (*src->ch_headlen)[n];
        memcpy(bptr,&src->ichannel[n],sizeof(aoo_channel_parameter));
        (* src->channel)[n] = (aoo_channel_parameter *) bptr;
        bptr +=  sizeof(aoo_channel_parameter);

        /* blobdata pointer */
        (*src->channel_data)[n] = (osc_blob) bptr;
        bptr += src->ichannel[n].blobsize;
    }

    return src->bundlesize;

exit_contruct_fail:
    osc_src_free(src);
    return 0;
}

/* code reservoire: */


/* if used with more than one drain name per drain,
   but now removed, since not really useful and not so optimized

osc_drain *osc_drain_new(unsigned int drains, osc_string *drain,
        int channels, int (*process_channel)(aoo_format_parameter *,aoo_channel *))

  ...

    osc->drain=NULL;
    osc->drains=drains;

    // drains + 1 for wildchar
    if(!(osc->drain = calloc(drains+1,sizeof(osc_string)))) {
        free(osc);
        return NULL;
    }
    for(n=0;n<drains;n++){
        if(!(osc->drain[n] = malloc(strlen(drain[n])+1))) {
            free_drains(osc);
            free(osc);
            return NULL;
        }
        strcpy(osc->drain[n],drain[n]);
    }
    drain[drains]="*"; // additional drain is wildchar

*/

