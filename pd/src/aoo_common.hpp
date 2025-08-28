/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "m_pd.h"

#include "aoo.h"
#include "aoo_client.hpp"

#include "common/log.hpp"
#include "common/net_utils.hpp"
#include "common/priority_queue.hpp"

#define classname(x) class_getname(*(t_pd *)x)

// NB: in theory we can support any number of channels.
// This is mainly meant to handle patches that accidentally use
// the old argument order where the port number would come first!
#define AOO_MAX_NUM_CHANNELS 256

#define DEJITTER_TOLERANCE 0.1 // jitter tolerance in percent

#define DEJITTER_MAXDELTA 0.02 // max. expected jitter in seconds

#define DEJITTER_DEBUG 0 // debug dejitter algorithm

#define DEJITTER_CHECK 1 // extra checks

// compatibility with older versions of "m_pd.h"
#if PD_MINOR_VERSION < 52
typedef enum {
    PD_CRITICAL = 0,
    PD_ERROR,
    PD_NORMAL,
    PD_DEBUG,
    PD_VERBOSE
} t_loglevel;
#endif

#if PD_MINOR_VERSION >= 54
# define PD_HAVE_MULTICHANNEL
#else
# pragma message("building without multi-channel support; requires Pd 0.54+")
# define CLASS_MULTICHANNEL 0
#endif

using t_signal_setmultiout = void (*)(t_signal **, int);
extern t_signal_setmultiout g_signal_setmultiout;

/*///////////////////////////// OSC time ///////////////////////////////*/

uint64_t get_osctime();

double get_elapsed_ms(AooNtpTime tt);

struct t_dejitter;

t_dejitter *dejitter_get();

void dejitter_release(t_dejitter *x);

uint64_t dejitter_osctime(t_dejitter *x);

/*///////////////////////////// aoo_node /////////////////////////////*/

class t_node {
public:
    static t_node * get(t_pd *obj, int port, void *x = nullptr, AooId id = 0);

    virtual ~t_node() {}

    virtual void release(t_pd *obj, void *x = nullptr) = 0;

    virtual AooClient * client() = 0;

    virtual int port() const = 0;

    virtual void notify() = 0;

    virtual bool resolve(t_symbol *host, int port, aoo::ip_address& addr) const = 0;

    virtual bool find_peer(const aoo::ip_address& addr,
                           t_symbol *& group, t_symbol *& user) const = 0;

    virtual bool find_peer(t_symbol * group, t_symbol * user,
                           aoo::ip_address& addr) const = 0;

    virtual int serialize_endpoint(const aoo::ip_address& addr, AooId id,
                                   int argc, t_atom *argv) const = 0;
};

/*///////////////////////////// helper functions ///////////////////////////////*/

int address_to_atoms(const aoo::ip_address& addr, int argc, t_atom *argv);

int endpoint_to_atoms(const aoo::ip_address& addr, AooId id, int argc, t_atom *argv);

bool format_parse(t_pd *x, AooFormatStorage &f, int argc, t_atom *argv,
                  int defchannels, int defsr, int defblocksize);

int format_to_atoms(const AooFormat &f, int argc, t_atom *argv);

bool atom_to_datatype(const t_atom &a, AooDataType& type, void *x);

int datatype_element_size(AooDataType type);

int atoms_to_data(AooDataType type, int argc, const t_atom *argv,
                  AooByte *data, AooSize size);

int data_to_atoms(const AooData& data, int argc, t_atom *argv);

int stream_message_to_atoms(const AooStreamMessage& data, int argc, t_atom *argv);

/*//////////////////////////// priority queue ////////////////////////////////*/

template<typename T>
struct t_queue_item {
    template<typename U>
    t_queue_item(U&& _data, double _time)
        : data(std::forward<U>(_data)), time(_time) {}
    T data;
    double time;
};

template<typename T>
bool operator> (const t_queue_item<T>& a, const t_queue_item<T>& b) {
    return a.time > b.time;
}

template<typename T>
using t_priority_queue = aoo::priority_queue<t_queue_item<T>, std::greater<t_queue_item<T>>>;
