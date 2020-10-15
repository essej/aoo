/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "m_pd.h"

#include "aoo/aoo.hpp"

#include "common/net_utils.hpp"
#include "common/time.hpp"

#define classname(x) class_getname(*(t_pd *)x)

namespace aoo {

/*///////////////////////////// OSC time ///////////////////////////////*/

uint64_t get_osctime();

struct t_dejitter;

t_dejitter *get_dejitter();

uint64_t get_osctime_dejitter(t_dejitter *context);

/*///////////////////////////// aoo_node /////////////////////////////*/

struct i_node {
    static i_node * get(t_pd *obj, int port, int32_t id);

    virtual ~i_node() {}

    virtual void release(t_pd *obj) = 0;

    virtual int socket() const = 0;

    virtual int port() const = 0;

    virtual int sendto(const char *buf, int32_t size,
                       const ip_address& addr) = 0;

    virtual endpoint *get_endpoint(const ip_address& addr) = 0;


    virtual endpoint *find_peer(t_symbol *group, t_symbol *user) = 0;

    virtual void add_peer(t_symbol *group, t_symbol *user, int32_t id,
                          const ip_address& addr) = 0;

    virtual void remove_peer(t_symbol *group, t_symbol *user) = 0;

    virtual void remove_all_peers() = 0;

    virtual void list_peers(t_outlet *out) = 0;

    virtual void remove_group(t_symbol *group) = 0;

    virtual void notify() = 0;
};

/*///////////////////////////// helper functions ///////////////////////////////*/

int address_to_atoms(const ip_address& addr, int argc, t_atom *argv);

int endpoint_to_atoms(const endpoint* ep, int32_t id, int argc, t_atom *argv);

bool endpoint_get_address(const endpoint* ep, t_symbol *& host, int& port);

bool get_sink_arg(void *x, i_node *node, int argc, t_atom *argv,
                 ip_address& addr, int32_t &id);

bool get_source_arg(void *x, i_node *node, int argc, t_atom *argv,
                   ip_address& addr, int32_t &id);

bool get_peer_arg(void *x, i_node *node, int argc, t_atom *argv,
                  ip_address& addr);

void format_makedefault(aoo_format_storage &f, int nchannels);

bool format_parse(void *x, aoo_format_storage &f, int argc, t_atom *argv,
                  int defnumchannels);

int format_to_atoms(const aoo_format &f, int argc, t_atom *argv);

} // aoo
