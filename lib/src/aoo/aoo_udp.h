/*
 * AoO - OSC interface
 *
 * Copyright (c) 2014 Winfried Ritsch <ritsch_at_algo.mur.at>
 *
 * This library is covered by the LGPL, read licences
 * at <http://www.gnu.org/licenses/>  for details
 *
 */

#ifndef __AOO_OSC_H__
#define __AOO_OSC_H__

/* #include <math.h> */

/* max UDP length should be enough */
#define AOO_MAX_MESSAGE_LEN 65536
#define ALLOW_BROADCAST

/* --- Prototypes --- */
int aoo_udp_insocket(int in_portno);
int read_in(int in_sockfd,int bufsize, unsigned char *buf);
int open_outsocket(char *hostname,int out_portno);
int send_out(int out_sockfd, int nsend, unsigned char *buf);


#endif /* __AOO_OSC_H__ */
