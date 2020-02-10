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
void aoo_udp_insocket_close(int socketfd);
int aoo_udp_read(int in_sockfd,int bufsize, unsigned char *buf);
int aoo_udp_outsocket(char *hostname,int out_portno);
void aoo_udp_outsocket_close(int socketfd);
int aoo_udp_send(int out_sockfd, int nsend, unsigned char *buf);

#endif /* __AOO_OSC_H__ */
