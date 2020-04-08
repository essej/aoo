/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "server.hpp"

/*//////////////////// AoO server /////////////////////*/

aoonet_server * aoonet_server_new(int port, int32_t *err) {
    int socket = 0;

    return new aoo::net::server(socket);
}

aoo::net::server::server(int socket)
    : socket_(socket){}

void aoonet_server_free(aoonet_server *server){
    // cast to correct type because base class
    // has no virtual destructor!
    delete static_cast<aoo::net::server *>(server);
}

aoo::net::server::~server() {}

int32_t aoonet_server_run(aoonet_server *server){
    return server->run();
}

int32_t aoo::net::server::run(){
    return 0;
}

int32_t aoonet_server_quit(aoonet_server *server){
    return server->quit();
}

int32_t aoo::net::server::quit(){
    return 0;
}

int32_t aoonet_server_events_available(aoonet_server *server){
    return server->events_available();
}

int32_t aoo::net::server::events_available(){
    return 0;
}

int32_t aoonet_server_handle_events(aoonet_server *server, aoo_eventhandler fn, void *user){
    return server->handle_events(fn, user);
}

int32_t aoo::net::server::handle_events(aoo_eventhandler fn, void *user){
    return 0;
}

namespace aoo {



} // aoo
