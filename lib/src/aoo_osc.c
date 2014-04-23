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
#define AOO_MAX_NAME_LEN 11 /* max string of drain or channel nr, since uint */

/*****************************************************************************
  Function:	osc_drain_new

  Summary:  generates new storage for a osc drain parser, for one drain.

  Description:
	for each drain one call is needed.

  Precondition:
  Parameters: None
  Returns: aoo_osc *, used in parser or NULL if failure

  Remarks: drain numbers and channel numbers has to be from 0 to 9999
 ***************************************************************************/

osc_drain* osc_drain_new(unsigned int drain, unsigned int channels,
                                 int (*process_channel)(aoo_format_parameter *,aoo_channel_parameter *))
{
    int i,n;
    char sd[AOO_MAX_NAME_LEN],sc[AOO_MAX_NAME_LEN];

    osc_drain* osc;

    if (!(osc = malloc(sizeof(osc_drain))))
        return NULL;

    osc->timestamp.val = 0ul; /* default */

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

        aoo_channel_set_string(sc,i);
        if(!(n = make_channel_head(&osc->channel_head[i],sd,sc))) {
            osc_drain_free(osc);
            return NULL;
        }
        osc->channel_head_size[i] = n;
    }

    return osc;
}


/*****************************************************************************
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

aoo_parser_ret osc_drain_parse(osc_drain* osc,
                             unsigned int datalen, void* data)
{
    unsigned int msglen;
    unsigned int channel,processed;
    char* readptr = (char *) &data;
    char* endptr = readptr + datalen;
    char* addrptr = NULL;

    /* at least bundle and format message */
    if(datalen < (sizeof(OSC_BUNDLE) + sizeof(osc_timetag) + sizeof(osc_int)
                  + osc->format_head_size + sizeof(aoo_format_parameter)))
        return OSC_PARSE_NOT_VALID;

    if (strcmp(readptr, OSC_BUNDLE) != 0)
        return OSC_PARSE_NOT_VALID;
    readptr += sizeof(OSC_BUNDLE);
    osc->timestamp.val = *((uint64_t *) readptr);
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

            if(memcmp(readptr,osc->channel_head[channel],
                      osc->channel_head_size[channel]) != 0) {
                channel++;
                continue; /* next one */
            }
            readptr += osc->channel_head_size[channel];
            memcpy(&osc->channel_parameter[channel],readptr,
                   sizeof(aoo_channel_parameter));

            readptr += sizeof(aoo_channel_parameter);

            if((ds = osc->channel_parameter[channel].blobsize) == 0)
                break; // next message

            if((readptr + ds) > endptr) /* to be sure */
                break;

            /* parsedata does not vanish until processed, so no copy anymore !

            if((osc->channel.data[channel] = malloc(ds)) == NULL)
                break;
            else
                memcpy(osc->channel.data[channel],readptr, ds);
            */
            /* can be optimized by directly feed channel, parameter, data */
            osc->channel_data[channel] = readptr;

            /* see if process_channel can do something with this data */
            if(osc->process_channel)
                if(osc->process_channel(channel,
                                        &osc->format,
                                        &osc->channel_parameter[channel],
                                        osc->channel_data[channel]) >= 0)
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
    if(osc->format_head) {
        free(osc->format_head);
        osc->format_head = NULL;
    }

    if(osc->channel_head)
        for(i=0; i<osc->channels; i++)
            if(osc->channel_head[i])
                free(osc->channel_head[i]);

    free(osc->channel_head);
    osc->channel_head = NULL;


    if(osc->channel_data)
        for(i=0; i<osc->channels; i++)
            if(osc->channel_data[i])
                free(osc->channel_data[i]);

    free(osc->channel_data);
    osc->channel_data = NULL;


    if(osc->channel_parameter) {
        free(osc->channel_parameter);
        osc->channel_parameter = NULL;
    }

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

  Precondition: aoo_osc_new()
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

        aoo_osc_announce_data.id.v[3] = AOO_ID;

        aoo_osc_announce_data.count.v[3] = announce_count.v[0];
        aoo_osc_announce_data.count.v[2] = announce_count.v[1];
        aoo_osc_announce_data.count.v[1] = 0;
        aoo_osc_announce_data.count.v[0] = 0;
        announce_count.Val++;

        memcpy((void *) aoo_osc_announce_data.name, (void *) AppConfig.NetBIOSName, 16);
        aoo_osc_announce_data.name[15] = 0;
    */
    return 1;
}


/* ======================= SOURCES ========================================= */

/*****************************************************************************
  Function: osc_src_new

  Summary: construct a new source mit parameter

  Description:
        allocates data variables and data space, which can be
        used for transmit and process which writes data there.

  Precondition: None
  Parameters: drain, channels, max_blobsize
  Returns: None

  Remarks: if max blobsize is the biggest expected blob, mostly used constant
 ***************************************************************************/
osc_src *osc_src_new(unsigned int drain,unsigned int channels,
                             unsigned int blob_size)
{
    osc_src *src = NULL;
    unsigned int n,len = 0;
    char *format_head, **channel_head;
    unsigned int fmt_len,*ch_len;
    char sd[AOO_MAX_NAME_LEN],sc[AOO_MAX_NAME_LEN];
    osc_blob bptr;

    if(channels == 0 || channels > AOO_MAX_CHANNELS || drain > AOO_MAX_DRAIN)
        return NULL;

    if (!(src = malloc(sizeof(osc_src))))
        return NULL;

    /* allocated temp data */
    format_head = NULL;
    channel_head = NULL;
    ch_len = NULL;

    if((channel_head =calloc(channels,sizeof(char *))) == NULL)
        goto exit_src_new;
    if((ch_len = calloc(channels,sizeof(unsigned int))) == NULL)
        goto exit_src_new;


    /* prepare data */

    osc_drain_set_string(sd,drain);
    if(!(fmt_len = make_format_head(&format_head,sd))) {
        free(src); src = NULL;
        goto exit_src_new;
    }

    for(n=0;n<channels;n++){
        aoo_channel_set_string(sc,n);
        if(!(ch_len[n] = make_channel_head(&channel_head[n],sd,sc))){
            free(src); src = NULL;
            goto exit_src_new;
        }

    }

    len = sizeof(OSC_BUNDLE) + sizeof(osc_timetag) + sizeof(osc_int)
                  + fmt_len + sizeof(aoo_format_parameter);
    for(n=0;n<channels;n++)
        len += sizeof(osc_int) + ch_len[n] + sizeof(aoo_channel_parameter)
               + blob_size;

    if(len > AOO_MAX_BUNDLE_LEN) {
        free(src); src = NULL;
        goto exit_src_new;
    }

    /* construct bundle */

    if(!(bptr = src->bundle = malloc(len))){
        free(src); src = NULL;
        goto exit_src_new;
    }



    /* bundle header */
    memcpy(bptr,OSC_BUNDLE,sizeof(OSC_BUNDLE));
    bptr +=  sizeof(OSC_BUNDLE);
    src->timetag = bptr;
    bptr +=  sizeof(osc_timetag);

    /* format message */
    *((osc_int *) bptr) = fmt_len + sizeof(aoo_format_parameter); /* msglen */
    memcpy(bptr,format_head,fmt_len);
    bptr +=  fmt_len;
    src->format = bptr;
    bptr +=  sizeof(aoo_format_parameter);
    /* fill default paramter */
    src->format->samplerate = 44100;
    src->format->blocksize = 0;
    src->format->overlap = 0;
    strcpy(src->format->mimetype,AOO_MIME_PCM);
    src->format->time_correction = 0.0;

    for(n=0;n<channels;n++){


        /* channel message */
        *((osc_int *) bptr) = ch_len[n] + sizeof(aoo_channel_parameter)+blob_size; /* msglen */
        memcpy(bptr,channel_head[n],ch_len[n]);
        bptr +=  ch_len[n];
        src->channel[n] = bptr;
        bptr +=  sizeof(aoo_channel_parameter);

        /* fill default paramter */
        src->channel[n]->id = 0;
        src->channel[n]->sequence = 0;
        src->channel[n]->resolution = AOO_RESOLUTION_FLOAT;
        src->channel[n]->resampling = 0;
        src->channel[n]->blobsize = blob_size;

        /* blobdata */
        src->channel_data[n] = bptr;
    }


exit_src_new:

    /* local allocated mem */
    if(format_head)free(format_head);
    if(channel_head){
        for(n=0;n<channels;n++)
            if(channel_head[n])
                free(channel_head[n]);
        free(channel_head);
    }
    if(ch_len)
        free(ch_len);

    return src;


}


/*****************************************************************************
  Function: osc_src_format

  Summary: construct a new source mit parameter

  Description:
        allocates data variables and data space, which can be
        used for transmit and process which writes data there.

  Precondition: None
  Parameters: drain, channels, max_blobsize
  Returns: None

  Remarks: if max blobsize is the biggest expected blob, mostly used constant
 ***************************************************************************/


int osc_src_format(osc_src* src,aoo_format_parameter* format)
{
    return OSC_SRC_NOT_VALID;
}


/* --- helpers --- */

/* expand an osc string to a 4 byte boundary by \0, return size */
static inline unsigned int osc_string_expand4(char* sptr)
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

    len = aoo_size4(sizeof(AOO_DRAIN) -1 + strlen(drainname)
                    + sizeof(AOO_FORMAT)+sizeof(AOO_FORMAT_TT))
          + aoo_size4(sizeof(AOO_FORMAT_TT_TC));

    if((*sptr = s = malloc(len)) == NULL)
        return 0;

    strcpy(s,AOO_DRAIN);
    strcat(s,drainname);
    strcat(s,AOO_FORMAT);

    /* extent to 4 byte boundary */
    n = osc_string_expand4(s);
    s += n;
    memcpy(s,AOO_FORMAT_TT_TC,sizeof(AOO_FORMAT_TT_TC));
    n += sizeof(AOO_FORMAT_TT_TC);
    return n;
}

static unsigned int make_channel_head(char **sptr,char* drainname,char* ch_name)
{
    unsigned int len,n;
    char *s;

    len = aoo_size4(sizeof(AOO_DRAIN) -1 + strlen(drainname)
                    + sizeof(AOO_FORMAT)+sizeof(AOO_FORMAT_TT))
          + aoo_size4(sizeof(AOO_FORMAT_TT_TC));

    if((*sptr = s = malloc(len)) == NULL)
        return 0;

    strcpy(s,AOO_DRAIN);
    strcat(s,drainname);
    strcat(s,AOO_CHANNEL);
    strcat(s,ch_name);
    /* extent to 4 byte boundary */
    n = osc_string_expand4(s);
    s += n;
    memcpy(s,AOO_CHANNEL_TT,sizeof(AOO_CHANNEL_TT));
    n += sizeof(AOO_CHANNEL_TT);

    return n;
}


/* code reservoire: */


/* if used with more than one drain name per drain,
   but now removed, since not really useful and not so optimized

aoo_osc_drain *aoo_osc_drain_new(unsigned int drains, osc_string *drain,
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


