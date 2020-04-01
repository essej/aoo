#include "m_pd.h"

#include "aoo_common.h"

#ifdef _WIN32
#include <winsock2.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

extern t_class *aoo_receive_class;

typedef struct _aoo_receive t_aoo_receive;

void aoo_receive_send(t_aoo_receive *x);

void aoo_receive_handle_message(t_aoo_receive *x, const char * data,
                                int32_t n, void *src, aoo_replyfn fn);

extern t_class *aoo_send_class;

typedef struct _aoo_send t_aoo_send;

void aoo_send_send(t_aoo_send *x);

void aoo_send_handle_message(t_aoo_send *x, const char * data,
                                int32_t n, void *src, aoo_replyfn fn);

static void lower_thread_priority(void)
{
#ifdef _WIN32
    int priority = GetThreadPriority(GetCurrentThread());
    SetThreadPriority(GetCurrentThread(), priority - 2);
#else

#endif
}

/*////////////////////// aoo server //////////////////*/

static t_class *aoo_server_class;

typedef struct _client
{
    t_pd *c_obj;
    int32_t c_id;
} t_client;

typedef struct _aoo_server
{
    t_pd x_pd;
    t_symbol *x_sym;
    // dependants
    t_client *x_clients;
    int x_numclients; // doubles as refcount
    // socket
    int x_socket;
    int x_port;
    t_endpoint *x_endpoints;
    // threading
    pthread_t x_sendthread;
    pthread_t x_receivethread;
    pthread_mutex_t x_endpointlock;
    aoo_lock x_clientlock;
    pthread_mutex_t x_mutex;
    pthread_cond_t x_condition;
    int x_quit; // should be atomic, but works anyway
} t_aoo_server;

t_endpoint * aoo_server_getendpoint(t_aoo_server *server,
                                    const struct sockaddr_storage *sa, socklen_t len)
{
    pthread_mutex_lock(&server->x_endpointlock);
    t_endpoint *ep = endpoint_find(server->x_endpoints, sa);
    if (!ep){
        // add endpoint
        ep = endpoint_new(server->x_socket, sa, len);
        ep->next = server->x_endpoints;
        server->x_endpoints = ep;
    }
    pthread_mutex_unlock(&server->x_endpointlock);
    return ep;
}


int aoo_server_port(t_aoo_server *x)
{
    return x->x_port;
}

void aoo_server_notify(t_aoo_server *x)
{
    pthread_cond_signal(&x->x_condition);
}

static void* aoo_server_send(void *y)
{
    t_aoo_server *x = (t_aoo_server *)y;

    lower_thread_priority();

    pthread_mutex_lock(&x->x_mutex);
    while (!x->x_quit){
        pthread_cond_wait(&x->x_condition, &x->x_mutex);

        aoo_lock_lock_shared(&x->x_clientlock);

        for (int i = 0; i < x->x_numclients; ++i){
            t_client *c = &x->x_clients[i];
            if (pd_class(c->c_obj) == aoo_receive_class){
                aoo_receive_send((t_aoo_receive *)c->c_obj);
            } else if (pd_class(c->c_obj) == aoo_send_class){
                aoo_send_send((t_aoo_send *)c->c_obj);
            } else {
                fprintf(stderr, "bug: aoo_server_send\n");
                fflush(stderr);
            }
        }

        aoo_lock_unlock_shared(&x->x_clientlock);
    }
    pthread_mutex_unlock(&x->x_mutex);

    return 0;
}

static void* aoo_server_receive(void *y)
{
    t_aoo_server *x = (t_aoo_server *)y;

    lower_thread_priority();

    while (!x->x_quit){
        struct sockaddr_storage sa;
        socklen_t len;
        char buf[AOO_MAXPACKETSIZE];
        int nbytes = socket_receive(x->x_socket, buf, AOO_MAXPACKETSIZE, &sa, &len, 0);
        if (nbytes > 0){
            // try to find endpoint
            pthread_mutex_lock(&x->x_endpointlock);
            t_endpoint *ep = endpoint_find(x->x_endpoints, &sa);
            if (!ep){
                // add endpoint
                ep = endpoint_new(x->x_socket, &sa, len);
                ep->next = x->x_endpoints;
                x->x_endpoints = ep;
            }
            pthread_mutex_unlock(&x->x_endpointlock);
            // get sink ID
            int32_t type, id;
            if (aoo_parsepattern(buf, nbytes, &type, &id) > 0){
                aoo_lock_lock_shared(&x->x_clientlock);
                if (type == AOO_TYPE_SINK){
                    // forward OSC packet to matching receiver(s)
                    for (int i = 0; i < x->x_numclients; ++i){
                        if ((pd_class(x->x_clients[i].c_obj) == aoo_receive_class) &&
                            ((id == AOO_ID_WILDCARD) || (id == x->x_clients[i].c_id)))
                        {
                            t_aoo_receive *rcv = (t_aoo_receive *)x->x_clients[i].c_obj;
                            aoo_receive_handle_message(rcv, buf, nbytes,
                                ep, (aoo_replyfn)endpoint_send);
                            if (id != AOO_ID_WILDCARD)
                                break;
                        }
                    }
                } else if (type == AOO_TYPE_SOURCE){
                    // forward OSC packet to matching senders(s)
                    for (int i = 0; i < x->x_numclients; ++i){
                        if ((pd_class(x->x_clients[i].c_obj) == aoo_send_class) &&
                            ((id == AOO_ID_WILDCARD) || (id == x->x_clients[i].c_id)))
                        {
                            t_aoo_send *snd = (t_aoo_send *)x->x_clients[i].c_obj;
                            aoo_send_handle_message(snd, buf, nbytes,
                                ep, (aoo_replyfn)endpoint_send);
                            if (id != AOO_ID_WILDCARD)
                                break;
                        }
                    }
                } else {
                    fprintf(stderr, "bug: unknown aoo type\n");
                    fflush(stderr);
                }
                aoo_lock_unlock_shared(&x->x_clientlock);
                // notify send thread
                pthread_cond_signal(&x->x_condition);
            } else {
                // not a valid AoO OSC message
            }
        } else if (nbytes < 0){
            // ignore errors when quitting
            if (!x->x_quit){
                socket_error_print("recv");
            }
        }
    }

    return 0;
}

t_aoo_server* aoo_server_addclient(t_pd *c, int32_t id, int port)
{
    // make bind symbol for port number
    char buf[64];
    snprintf(buf, sizeof(buf), "aoo listener %d", port);
    t_symbol *s = gensym(buf);
    t_client client = { c, id };
    t_aoo_server *x = (t_aoo_server *)pd_findbyclass(s, aoo_server_class);
    if (x){
        // check receiver and add to list
        aoo_lock_lock(&x->x_clientlock);
    #if 1
        for (int i = 0; i < x->x_numclients; ++i){
            if (pd_class(c) == pd_class(x->x_clients[i].c_obj)
                && id == x->x_clients[i].c_id)
            {
                if (c == x->x_clients[i].c_obj){
                    bug("aoo_server_add: client already added!");
                } else {
                    pd_error(c, "%s with ID %d on port %d already exists!",
                             classname(c), id, port);
                }
                aoo_lock_unlock(&x->x_clientlock);
                return 0;
            }
        }
    #endif
        x->x_clients = (t_client *)resizebytes(x->x_clients, sizeof(t_client) * x->x_numclients,
                                                sizeof(t_client) * (x->x_numclients + 1));
        x->x_clients[x->x_numclients] = client;
        x->x_numclients++;
        aoo_lock_unlock(&x->x_clientlock);
    } else {
        // make new aoo server

        // first create socket
        int sock = socket_udp();
        if (sock < 0){
            socket_error_print("socket");
            return 0;
        }

        // bind socket to given port
        if (socket_bind(sock, port) < 0){
            pd_error(c, "%s: couldn't bind to port %d", classname(c), port);
            socket_close(sock);
            return 0;
        }

        // now create aoo server instance
        x = (t_aoo_server *)getbytes(sizeof(t_aoo_server));
        x->x_pd = aoo_server_class;
        x->x_sym = s;
        pd_bind(&x->x_pd, s);

        // add receiver
        x->x_clients = (t_client *)getbytes(sizeof(t_client));
        x->x_clients[0] = client;
        x->x_numclients = 1;

        x->x_socket = sock;
        x->x_port = port;
        x->x_endpoints = 0;

        // start threads
        x->x_quit = 0;
        aoo_lock_init(&x->x_clientlock);
        pthread_mutex_init(&x->x_mutex, 0);
        pthread_cond_init(&x->x_condition, 0);

        pthread_create(&x->x_sendthread, 0, aoo_server_send, x);
        pthread_create(&x->x_receivethread, 0, aoo_server_receive, x);

        verbose(0, "new aoo server on port %d", x->x_port);
    }
    return x;
}

void aoo_server_removeclient(t_aoo_server *x, t_pd *c, int32_t id)
{
    if (x->x_numclients > 1){
        // just remove receiver from list
        aoo_lock_lock(&x->x_clientlock);
        int n = x->x_numclients;
        for (int i = 0; i < n; ++i){
            if (c == x->x_clients[i].c_obj){
                if (id != x->x_clients[i].c_id){
                    bug("aoo_server_remove: wrong ID!");
                    return;
                }
                memmove(&x->x_clients[i], &x->x_clients[i + 1], n - (i + 1));
                x->x_clients = (t_client *)resizebytes(x->x_clients, n * sizeof(t_client),
                                                        (n - 1) * sizeof(t_client));
                x->x_numclients--;
                aoo_lock_unlock(&x->x_clientlock);
                return;
            }
        }
        bug("aoo_server_release: receiver not found!");
        aoo_lock_unlock(&x->x_clientlock);
    } else if (x->x_numclients == 1){
        // last instance
        pd_unbind(&x->x_pd, x->x_sym);

        // tell the threads that we're done
        pthread_mutex_lock(&x->x_mutex);
        x->x_quit = 1;
        pthread_mutex_unlock(&x->x_mutex);

        // notify send thread
        pthread_cond_signal(&x->x_condition);

        // try to wake up receive thread
        aoo_lock_lock(&x->x_clientlock);
        int didit = socket_signal(x->x_socket, x->x_port);
        if (!didit){
            // force wakeup by closing the socket.
            // this is not nice and probably undefined behavior,
            // the MSDN docs explicitly forbid it!
            socket_close(x->x_socket);
        }
        aoo_lock_unlock(&x->x_clientlock);

        // wait for threads
        pthread_join(x->x_sendthread, 0);
        pthread_join(x->x_receivethread, 0);

        if (didit){
            socket_close(x->x_socket);
        }

        // free memory
        t_endpoint *e = x->x_endpoints;
        while (e){
            t_endpoint *next = e->next;
            endpoint_free(e);
            e = next;
        }
        freebytes(x->x_clients, sizeof(t_client) * x->x_numclients);

        pthread_mutex_destroy(&x->x_mutex);
        aoo_lock_destroy(&x->x_clientlock);
        pthread_cond_destroy(&x->x_condition);

        verbose(0, "released aoo server on port %d", x->x_port);

        freebytes(x, sizeof(*x));
    } else {
        bug("aoo_server_release: negative refcount!");
    }
}

void aoo_server_setup(void)
{
    aoo_server_class = class_new(gensym("aoo socket receiver"), 0, 0,
                                  sizeof(t_aoo_server), CLASS_PD, A_NULL);
}
