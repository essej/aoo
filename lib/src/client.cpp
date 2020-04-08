/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "client.hpp"

/*//////////////////// AoO client /////////////////////*/

aoonet_client * aoonet_client_new(int socket) {
    return new aoo::net::client(socket);
}

aoo::net::client::client(int socket)
    : socket_(socket){}

void aoonet_client_free(aoonet_client *client){
    // cast to correct type because base class
    // has no virtual destructor!
    delete static_cast<aoo::net::client *>(client);
}

aoo::net::client::~client() {}

int32_t aoonet_client_run(aoonet_client *client){
    return client->run();
}

int32_t aoo::net::client::run(){
    return 0;
}

int32_t aoonet_client_quit(aoonet_client *client){
    return client->quit();
}

int32_t aoo::net::client::quit(){
    return 0;
}

int32_t aoonet_client_connect(aoonet_client *client, const char *host, int port,
                           const char *username, const char *pwd)
{
    return client->connect(host, port, username, pwd);
}

int32_t aoo::net::client::connect(const char *host, int port,
                             const char *username, const char *pwd)
{
    return 0;
}

int32_t aoonet_client_disconnect(aoonet_client *client){
    return client->disconnect();
}

int32_t aoo::net::client::disconnect(){
    return 0;
}

int32_t aoonet_client_group_join(aoonet_client *client, const char *group, const char *pwd){
    return client->group_join(group, pwd);
}

int32_t aoo::net::client::group_join(const char *group, const char *pwd){
    return 0;
}

int32_t aoonet_client_group_leave(aoonet_client *client, const char *group){
    return client->group_leave(group);
}

int32_t aoo::net::client::group_leave(const char *group){
    return 0;
}

int32_t aoonet_client_handle_message(aoonet_client *client, const char *data, int32_t n, void *addr){
    return client->handle_message(data, n, addr);
}

int32_t aoo::net::client::handle_message(const char *data, int32_t n, void *addr){
    return 0;
}

int32_t aoonet_client_send(aoonet_client *client){
    return client->send();
}

int32_t aoo::net::client::send(){
    return 0;
}

int32_t aoonet_client_events_available(aoonet_server *client){
    return client->events_available();
}

int32_t aoo::net::client::events_available(){
    return 0;
}

int32_t aoonet_client_handle_events(aoonet_client *client, aoo_eventhandler fn, void *user){
    return client->handle_events(fn, user);
}

int32_t aoo::net::client::handle_events(aoo_eventhandler fn, void *user){
    return 0;
}

namespace aoo {



} // aoo
