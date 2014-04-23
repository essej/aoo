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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#define SOCKET_ERROR -1

#include "aoo/aoo.h"
#include "aoo/aoo_udp.h"

extern int aoo_verbosity;
static void sockerror(char *s);

/*****************************************************************************
  Function: aoo_udp_insocket

  Summary:
        open (bind) a UDP socket

  Description:
        Receiver for UDP packages to be parsed by aoo_parse

  Precondition:
  Parameters: portno
  Returns: Errors as zero or negativ values and socketid as sucess

  Remarks:
 ***************************************************************************/

int aoo_udp_insocket(int in_portno)
{
    struct sockaddr_in server;
    int in_sockfd = 0;

    /* --- out socket --- */
    if(aoo_verbosity >= AOO_VERBOSITY_DETAIL)
        printf("open socket in\n");

    in_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (in_sockfd < 0)
    {
        sockerror("socket()");
        return(0);
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;

    /* assign client port number */
    server.sin_port = htons((unsigned short)in_portno);

    /* name the socket */
    if (bind(in_sockfd, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        sockerror("bind");
        close(in_sockfd);
        return (0);
    }

    return in_sockfd;
}

/*****************************************************************************
  Function: aoo_udp_insocket, aoo_udp_read

  Summary:
        reads data from an bind socket

  Description:
        Receiver for UDP packages to be parsed by aoo_parse

  Precondition:
  Parameters: socketfd, bufsize and pointer to a buffer to read in
  Returns: Errors as zero or negativ values and socketid as sucess

  Remarks:
 ***************************************************************************/

int read_in(int in_sockfd,int bufsize, unsigned char *buf)
{
    int ret = -1;
    fd_set readset, writeset, exceptset;
    int maxfd;

    FD_ZERO(&writeset);
    FD_ZERO(&readset);
    FD_ZERO(&exceptset);

    FD_SET(in_sockfd, &readset);

    if(aoo_verbosity >= AOO_VERBOSITY_DEBUG)
        printf("read in socket in: select; ");

    maxfd = in_sockfd + 1;

    if (select(maxfd, &readset, &writeset, &exceptset, 0) < 0)
    {
        perror("select");
        exit(1);
    }
    if(aoo_verbosity >= AOO_VERBOSITY_DEBUG)
        printf("selected; ");

    if (FD_ISSET(in_sockfd, &readset))
        ret = recv(in_sockfd, buf, bufsize, 0);
    else
        fprintf(stderr,"no socket set");

    if (ret < 0)
    {
        sockerror("recv (udp)");
        close(in_sockfd);
        exit(1);
    }

    if(aoo_verbosity >= AOO_VERBOSITY_DEBUG)
        printf("read on socket in: %d bytes\n",ret);


    return ret;
}

/*****************************************************************************
  Function: aoo_udp_outsocket

  Summary: open an out socket, to send data

  Description:
        hostname and portno is needed for destination

  Precondition:
  Parameters: hostname, out port number
  Returns: outsocketfd

  Remarks: without checks.
 ***************************************************************************/

int open_outsocket(char *hostname,int out_portno)
{
    struct sockaddr_in server;
    struct hostent *hp;
    int out_sockfd;

    /* --- out socket --- */
    if(aoo_verbosity >= AOO_VERBOSITY_DETAIL)
        printf("open socket out %s %d\n",hostname,out_portno);

    out_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (out_sockfd < 0)
    {
        sockerror("socket()");
        exit(1);
    }
    /* connect socket using hostname provided in command line */
    server.sin_family = AF_INET;
    hp = gethostbyname(hostname);
    if (hp == 0)
    {
        fprintf(stderr, "%s: unknown host\n", hostname);
        close(out_sockfd);
        exit(1);
    }
    memcpy((char *)&server.sin_addr, (char *)hp->h_addr, hp->h_length);

    /* assign client port number */
    server.sin_port = htons((unsigned short)out_portno);


#ifdef ALLOW_BROADCAST
    {
        int so_broadcast = true;

        if(setsockopt(out_sockfd, SOL_SOCKET, SO_BROADCAST, &so_broadcast, sizeof so_broadcast) != 0)
            sockerror("no broadcast");
    }
#endif

    /* try to connect.  */
    if (connect(out_sockfd, (struct sockaddr *) &server, sizeof (server)) < 0)
    {
        sockerror("connect");
        close(out_sockfd);
        exit(1);
    }
    return out_sockfd;
}




/*****************************************************************************
  Function:
	aoo_udp_send

  Summary:
    send UDP packet on a oben socket

  Description:
	socket must be initialized before

  Precondition: aoo_udp_outsocket
  Parameters: None
  Returns: aoo_osc *, used in parser or NULL if failure

  Remarks:
 ***************************************************************************/
int send_out(int out_sockfd, int nsend, unsigned char *buf)
{
    int nsent,res;
    char *bp;

    for (bp = buf, nsent = 0; nsent < nsend;)
    {

        if(aoo_verbosity >= AOO_VERBOSITY_DEBUG)
            printf("send out socket %d/%d:%s",nsent,nsend,bp);

        res = send(out_sockfd, bp, nsend-nsent, 0);
        if (res < 0)
        {
            sockerror("send");
            return res;
        }
        nsent += res;
        bp += res;
    }
    return res;
}

/* *************************** HELPER ******************* */
void sockerror(char *s)
{
    int err = errno;
    fprintf(stderr, "%s: %s (%d)\n", s, strerror(err), err);
}

