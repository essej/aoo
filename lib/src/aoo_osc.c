/* Copyright (c) 2014 Winfried Ritsch
 * 
 * This library is covered by the LGPL, read licences
 * at <http://www.gnu.org/licenses/>  for details 
 * 
 */
#include "aoo/aoo_osc.h"
#include "aoo/aoo.h"


/*****************************************************************************
  Function: aoo_osc_parse

  Summary:
        receives OSC packages for a drain and extract audio data

  Description:
        test package for valid OSC message, parse it return audiodata

  Precondition: aoo_osc_drain_new()
  Parameters: None
  Returns: None

  Remarks:
 ***************************************************************************/

bool aoo_osc_parse(aoo_osc_drain *osc,unsigned int datalen, (void *) data) {


   char *readptr = (char *) &data;
	char *endptr = readptr + datalen;
	
	unsigned int messages = 0;
	bool tc_flag;
	
	/* message to short */
	if(datalen < (sizeof(AOO_OSC_BUNDLE) + sizeof(osc_timestamp)
	               + sizeof(AOO_OSC_FORMAT_TT) + osc->header_len))
		return 0;

	if (strncmp(readptr, AOO_OSC_BUNDLE, sizeof(AOO_OSC_BUNDLE)) != 0)
		return 0;
	readptr += sizeof(AOO_OSC_BUNDLE);

	osc->timestamp.val = *((osc_timetag *) readptr);
	readptr += sizeof(osc_timetag);
	/* dont know how to validate timetag, so i dont */


	/* first format message */
	if(strncmp(osc->header,readptr,osc->header_len) != 0)
		return 0;
	readptr += osc->header_len;

	if(strncmp(AOO_OSC_FORMAT,readptr,sizeof(AOO_OSC_FORMAT)) != 0)
		return 0;
	readptr += sizeof(AOO_OSC_FORMAT);


	if(strncmp(AOO_OSC_FORMAT_TT,readptr,sizeof(AOO_OSC_FORMAT)) == 0)
		tc_flag = false;
	else if(strncmp(AOO_OSC_FORMAT_TT_TC,readptr,sizeof(AOO_OSC_FORMAT)) == 0)
		tc_flag = true;
	else
		return 0;
		
	readptr += sizeof(AOO_OSC_FORMAT_TT); 
	/* same size as AOO_OSC_FORMAT_TT_TC */

	if(readptr+3*sizeof(osc_int) >= endptr)
		return 0;

	osc->format.samplerate = (osc_int) *readptr;
	readptr += sizeof(osc_int);
	osc->format.blocksize = (osc_int) *readptr;
	readptr += sizeof(osc_int);
	osc->format.overlap = (osc_int) *readptr;
	readptr += sizeof(osc_int);

	/* read mimetype (only one now is "audio/pcm" 
	 * it will be changed in future if compression is accepted*/
	
	if(strncmp("audio/pcm\0\0",readptr,sizeof("audio/pcm\0\0")) != 0)
		return 0;
	readptr += sizeof("audio/pcm\0\0");

	if(tc_flag){
		osc->format.time_correction = *((osc_float *) readptr);
		readptr += sizeof(osc_float);
	} else
		osc->format.time_correction = 0.0;
	
	/* read channel messages */
	while(readptr < endptr){

		/* check for size of message omitted */

		char *p;
		unsigned int channelnr;
		size_t ds;
		aoo_channel channel;

		if(strncmp(osc->header,readptr,osc->header_len) != 0)
		return 0;
		readptr += osc->header_len;
		
		if(strncmp(AOO_OSC_CHANNEL,readptr,sizeof(AOO_OSC_CHANNEL)) != 0)
			return 0;
		readptr += sizeof(AOO_OSC_CHANNEL);

		channelnr = (unsigned int) stroul(readptr,&p,0);
		if (errno != 0 || *p != 0 || p == str)
			return 0;

		readptr += aoo_size4(strlen((char *) readptr));

		/* blob has at least size of datasize bytes */
		if((readptr+4*sizeof(osc_int)+sizeof(osc_float)) >= endptr)
			return 0;

		channel.id = *((osc_int *) readptr);
		readptr += sizeof(osc_int);
		channel.sequence = *((osc_int *) readptr);
		readptr += sizeof(osc_int);
		channel.resolution = *((osc_int *) readptr);
		readptr += sizeof(osc_int);
		channel.resampling = *((osc_float *) readptr);
		readptr += sizeof(osc_float);
		ds = channel.datasize = *((osc_int *) readptr);
		readptr += sizeof(osc_int);

		if(ds == 0)
			continue;
			
		if((readptr + ds) < endptr){

			if((channel.data = malloc(ds) == NULL)
				return 0;

			memcpy(channel.data,readptr, ds);
			readptr += ds;
		}
		/* DO something with channel data !!! */
		
	} /* channel message */
	
    return 1;
}

/*****************************************************************************
  Function: aoo_osc_send

  Summary: send AoO Message as bundle

  Description:
        test package for valid OSC message, parse it ans stores values

  Precondition: 
  Parameters: address string, typestring, data, len of data
  Returns: None

  Remarks: without checks.
 ***************************************************************************/
void aoo_osc_send(osc_string drain,unsigned int channels,aoo_format format,
					unsigned int samples,(void *) audiodata)  {

    return;
}

/*****************************************************************************
  Function:
	aoo_osc_new, void aoo_osc_free

  Summary:
    generates new storage for osc one drain parser.

  Description:
	for each drain one parser is needed.
		
  Precondition: 
  Parameters: None
  Returns: aoo_osc *, used in parser or NULL if failure

  Remarks:
 ***************************************************************************/
/* asume drain string is shorter than 15 */
#define OSC_MAX_HEADER sizeof(AOO_OSC_MESSAGE AOO_OSC_DRAIN)+16

aoo_osc *aoo_osc_drain_new(osc_string drain,int channels)
{
	aoo_osc *osc;  
	char header[OSC_MAX_HEADER];
	
	if (!(osc = malloc(sizeof(aoo_osc_drain))))
		return NULL;

	osc->data = NULL;
	osc->format.time_correction = 0.0;
	
	snprintf(header,OSC_MAX_HEADER,AOO_OSC_MESSAGE AOO_OSC_DRAIN "%s",drain);
	
	osc->header_len = strlen(header);
	if(!(osc->header = malloc(osc->header_len+1))){
		free(osc);
		return NULL;
	}
	strncpy(osc->header,header,osc->header_len+1);
	
	osc->message = NULL;
	osc->announce = false;
	osc->announce_count = 0;
	
    return osc;
}

void aoo_osc_drain_free(aoo_osc *osc)
{
	if(osc == NULL)
		return;
	if(osc->header)
		free(osc->header);
	free(osc);
	return;
}

/*****************************************************************************
  Function:
    void aoo_osc_announce(aoo_osc *osc)

  Summary:
    send OSC Announce Message

  Description:
    announce the IP and other Info to the network or client

  Precondition: aoo_osc_new()
  Parameters: pointer to osc
  Returns: None

  Remarks: without checks.
 ***************************************************************************/

void aoo_osc_announce(aoo_osc *osc) {

	if(!osc)
		return;


/* Not implemented for now

    if (aooBcastIsPutReady() < 64u) // smallest debug msg
        return;

 
    aoo_osc_announce_data.ip[0].v[3] = AppConfig.MyIPAddr.v[0];
    aoo_osc_announce_data.ip[1].v[3] = AppConfig.MyIPAddr.v[1];
    aoo_osc_announce_data.ip[2].v[3] = AppConfig.MyIPAddr.v[2];
    aoo_osc_announce_data.ip[3].v[3] = AppConfig.MyIPAddr.v[3];

    aoo_osc_announce_data.netmask[0].v[3] = AppConfig.DefaultMask.v[0];
    aoo_osc_announce_data.netmask[1].v[3] = AppConfig.DefaultMask.v[1];
    aoo_osc_announce_data.netmask[2].v[3] = AppConfig.DefaultMask.v[2];
    aoo_osc_announce_data.netmask[3].v[3] = AppConfig.DefaultMask.v[3];

    aoo_osc_announce_data.gateway[0].v[3] = AppConfig.MyGateway.v[0];
    aoo_osc_announce_data.gateway[1].v[3] = AppConfig.MyGateway.v[1];
    aoo_osc_announce_data.gateway[2].v[3] = AppConfig.MyGateway.v[2];
    aoo_osc_announce_data.gateway[3].v[3] = AppConfig.MyGateway.v[3];

    aoo_osc_announce_data.remoteip[0].v[3] = aooClientIP.v[0];
    aoo_osc_announce_data.remoteip[1].v[3] = aooClientIP.v[1];
    aoo_osc_announce_data.remoteip[2].v[3] = aooClientIP.v[2];
    aoo_osc_announce_data.remoteip[3].v[3] = aooClientIP.v[3];

    aoo_osc_announce_data.mac[0].v[3] = AppConfig.MyMACAddr.v[0];
    aoo_osc_announce_data.mac[1].v[3] = AppConfig.MyMACAddr.v[1];
    aoo_osc_announce_data.mac[2].v[3] = AppConfig.MyMACAddr.v[2];
    aoo_osc_announce_data.mac[3].v[3] = AppConfig.MyMACAddr.v[3];
    aoo_osc_announce_data.mac[4].v[3] = AppConfig.MyMACAddr.v[4];
    aoo_osc_announce_data.mac[5].v[3] = AppConfig.MyMACAddr.v[5];

    aoo_osc_announce_data.id.v[3] = AOO_ID;

    aoo_osc_announce_data.count.v[3] = announce_count.v[0];
    aoo_osc_announce_data.count.v[2] = announce_count.v[1];
    aoo_osc_announce_data.count.v[1] = 0;
    aoo_osc_announce_data.count.v[0] = 0;
    announce_count.Val++;

    memcpy((void *) aoo_osc_announce_data.name, (void *) AppConfig.NetBIOSName, 16);
    aoo_osc_announce_data.name[15] = 0;

    //    aooUDPut((BYTE *) AOO_OSC_ANNOUNCE_ADR, 12); //aoo_size4(AOO_OSC_ANNOUNCE_ADR));
    //    aooUDPut((BYTE *) AOO_OSC_ANNOUNCE_TT, 28); //aoo_size4(AOO_OSC_ANNOUNCE_TT));
    //    aooUDPSend((BYTE *) &aoo_osc_announce_data, sizeof(AOO_OSC_ANNOUNCE));
    UDPPutROMArray(AOO_OSC_ANNOUNCE_ADR, 12);
    UDPPutROMArray(AOO_OSC_ANNOUNCE_TT, 28);
    UDPPutArray((BYTE *) & aoo_osc_announce_data, (4 * 24 + 16));
    UDPFlush();
*/
	return;
}
