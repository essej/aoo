/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo.h"

#include <memory>

namespace aoo {

// NOTE: aoo::isource and aoo::isink don't define virtual destructors
// and have to be destroyed with their respective destroy() method.
// We provide a custom deleter and shared pointer to automate this task.
//
// The absence of a virtual destructor allows for ABI independent
// C++ interfaces on Windows (where the vtable layout is stable
// because of COM) and usually also on other platforms.
// (Compilers use different strategies for virtual destructors,
// some even put more than 1 entry in the vtable.)
// Also, we only use standard C types as function parameters
// and return types.
//
// In practice this means you only have to build 'aoo' once as a
// shared library and can then use its C++ interface in applications
// built with different compilers resp. compiler versions.
//
// If you want to be on the safe safe, use the C interface :-)

/*//////////////////////// AoO source ///////////////////////*/

class isource {
public:
    class deleter {
    public:
        void operator()(isource *x){
            destroy(x);
        }
    };
    // smart pointer for AoO source instance
    using pointer = std::unique_ptr<isource, deleter>;

    // create a new AoO source instance
    static isource * create(aoo_id id, uint32_t flags);

    // destroy the AoO source instance
    static void destroy(isource *src);

    // setup the source - needs to be synchronized with other method calls!
    virtual aoo_error setup(int32_t samplerate, int32_t blocksize, int32_t nchannels) = 0;

    // add a new sink (always threadsafe)
    virtual aoo_error add_sink(const void *address, int32_t addrlen,
                             aoo_id id, uint32_t flags) = 0;

    // remove a sink (always threadsafe)
    virtual aoo_error remove_sink(const void *address, int32_t addrlen, aoo_id id) = 0;

    // remove all sinks (always threadsafe)
    virtual void remove_all() = 0;

    // handle messages from sinks (threadsafe, called from the network thread)
    virtual aoo_error handle_message(const char *data, int32_t n,
                                     const void *address, int32_t addrlen,
                                     aoo_sendfn fn, void *user) = 0;

    // update and send outgoing messages (threadsafe, called from the network thread)
    virtual aoo_error update(aoo_sendfn fn, void *user) = 0;

    // process audio blocks (threadsafe, called from the audio thread)
    // data:        array of channel data (non-interleaved)
    // nsamples:    number of samples per channel
    // t:           current NTP timestamp (see aoo_osctime_get)
    virtual aoo_error process(const aoo_sample **data,
                            int32_t nsamples, uint64_t t) = 0;

    // set event handler callback + mode
    virtual aoo_error set_eventhandler(aoo_eventhandler fn,
                                       void *user, int32_t mode) = 0;

    // check for pending events (always thread safe)
    virtual aoo_bool events_available() = 0;

    // poll events (threadsafe, but not reentrant)
    // will call the event handler function one or more times
    virtual aoo_error poll_events() = 0;

    //---------------------- options ----------------------//
    // set/get options (always threadsafe)

    aoo_error start(){
        return set_option(AOO_OPT_START, AOO_ARG_NULL);
    }

    aoo_error stop(){
        return set_option(AOO_OPT_STOP, AOO_ARG_NULL);
    }

    aoo_error set_id(aoo_id id){
        return set_option(AOO_OPT_ID, AOO_ARG(id));
    }

    aoo_error get_id(aoo_id &id){
        return get_option(AOO_OPT_ID, AOO_ARG(id));
    }

    aoo_error set_format(aoo_format& f){
        return set_option(AOO_OPT_FORMAT, AOO_ARG(f));
    }

    aoo_error get_format(aoo_format_storage& f){
        return get_option(AOO_OPT_FORMAT, AOO_ARG(f));
    }

    aoo_error set_buffersize(int32_t n){
        return set_option(AOO_OPT_BUFFERSIZE, AOO_ARG(n));
    }

    aoo_error get_buffersize(int32_t& n){
        return get_option(AOO_OPT_BUFFERSIZE, AOO_ARG(n));
    }

    aoo_error set_timefilter_bandwidth(float f){
        return set_option(AOO_OPT_TIMEFILTER_BANDWIDTH, AOO_ARG(f));
    }

    aoo_error get_timefilter_bandwidth(float& f){
        return get_option(AOO_OPT_TIMEFILTER_BANDWIDTH, AOO_ARG(f));
    }

    aoo_error set_packetsize(int32_t n){
        return set_option(AOO_OPT_PACKETSIZE, AOO_ARG(n));
    }

    aoo_error get_packetsize(int32_t& n){
        return get_option(AOO_OPT_PACKETSIZE, AOO_ARG(n));
    }

    aoo_error set_resend_buffersize(int32_t n){
        return set_option(AOO_OPT_RESEND_BUFFERSIZE, AOO_ARG(n));
    }

    aoo_error get_resend_buffersize(int32_t& n){
        return get_option(AOO_OPT_RESEND_BUFFERSIZE, AOO_ARG(n));
    }

    aoo_error set_redundancy(int32_t n){
        return set_option(AOO_OPT_REDUNDANCY, AOO_ARG(n));
    }

    aoo_error get_redundancy(int32_t& n){
        return get_option(AOO_OPT_REDUNDANCY, AOO_ARG(n));
    }

    aoo_error set_ping_interval(int32_t ms){
        return set_option(AOO_OPT_PING_INTERVAL, AOO_ARG(ms));
    }

    aoo_error get_ping_interval(int32_t& ms){
        return get_option(AOO_OPT_PING_INTERVAL, AOO_ARG(ms));
    }

    virtual aoo_error set_option(int32_t opt, void *ptr, int32_t size) = 0;
    virtual aoo_error get_option(int32_t opt, void *ptr, int32_t size) = 0;

    //--------------------- sink options --------------------------//
    // set/get sink options (always threadsafe)

    aoo_error set_sink_channelonset(const void *address, int32_t addrlen,
                                  aoo_id id, int32_t onset){
        return set_sinkoption(address, addrlen, id, AOO_OPT_CHANNELONSET, AOO_ARG(onset));
    }

    aoo_error get_sink_channelonset(const void *address, int32_t addrlen, aoo_id id, int32_t& onset){
        return get_sinkoption(address, addrlen, id, AOO_OPT_CHANNELONSET, AOO_ARG(onset));
    }

    virtual aoo_error set_sinkoption(const void *address, int32_t addrlen, aoo_id id,
                                   int32_t opt, void *ptr, int32_t size) = 0;

    virtual aoo_error get_sinkoption(const void *address, int32_t addrlen, aoo_id id,
                                   int32_t opt, void *ptr, int32_t size) = 0;
protected:
    ~isource(){} // non-virtual!
};

inline isource * isource::create(aoo_id id, uint32_t flags){
    return aoo_source_new(id, flags);
}

inline void isource::destroy(isource *src){
    aoo_source_free(src);
}

/*//////////////////////// AoO sink ///////////////////////*/

class isink {
public:
    class deleter {
    public:
        void operator()(isink *x){
            destroy(x);
        }
    };
    // smart pointer for AoO sink instance
    using pointer = std::unique_ptr<isink, deleter>;

    // create a new AoO sink instance
    static isink * create(aoo_id id, uint32_t flags);

    // destroy the AoO sink instance
    static void destroy(isink *sink);

    // setup the sink - needs to be synchronized with other method calls!
    virtual aoo_error setup(int32_t samplerate, int32_t blocksize, int32_t nchannels) = 0;

    // invite a source (always thread safe)
    virtual aoo_error invite_source(const void *address, int32_t addrlen, aoo_id id) = 0;

    // uninvite a source (always thread safe)
    virtual aoo_error uninvite_source(const void *address, int32_t addrlen, aoo_id id) = 0;

    // uninvite all sources (always thread safe)
    virtual aoo_error uninvite_all() = 0;

    // handle messages from sources (threadsafe, called from the network thread)
    virtual aoo_error handle_message(const char *data, int32_t n,
                                     const void *address, int32_t addrlen,
                                     aoo_sendfn fn, void *user) = 0;

    // update and send outgoing messages (threadsafe, called from the network thread)
    virtual aoo_error update(aoo_sendfn fn, void *user) = 0;

    // process audio (threadsafe, but not reentrant)
    virtual aoo_error process(aoo_sample **data, int32_t nsamples, uint64_t t) = 0;

    // set event handler callback + mode
    virtual aoo_error set_eventhandler(aoo_eventhandler fn,
                                       void *user, int32_t mode) = 0;

    // check for pending events (always thread safe)
    virtual aoo_bool events_available() = 0;

    // poll events (threadsafe, but not reentrant)
    // will call the event handler function one or more times
    virtual aoo_error poll_events() = 0;

    //---------------------- options ----------------------//
    // set/get options (always threadsafe)

    aoo_error reset(){
        return set_option(AOO_OPT_RESET, AOO_ARG_NULL);
    }

    aoo_error set_id(aoo_id id){
        return set_option(AOO_OPT_ID, AOO_ARG(id));
    }

    aoo_error get_id(aoo_id &id){
        return get_option(AOO_OPT_ID, AOO_ARG(id));
    }

    aoo_error set_buffersize(int32_t n){
        return set_option(AOO_OPT_BUFFERSIZE, AOO_ARG(n));
    }

    aoo_error get_buffersize(int32_t& n){
        return get_option(AOO_OPT_BUFFERSIZE, AOO_ARG(n));
    }

    aoo_error set_timefilter_bandwidth(float f){
        return set_option(AOO_OPT_TIMEFILTER_BANDWIDTH, AOO_ARG(f));
    }

    aoo_error get_timefilter_bandwidth(float& f){
        return get_option(AOO_OPT_TIMEFILTER_BANDWIDTH, AOO_ARG(f));
    }

    aoo_error set_packetsize(int32_t n){
        return set_option(AOO_OPT_PACKETSIZE, AOO_ARG(n));
    }

    aoo_error get_packetsize(int32_t& n){
        return get_option(AOO_OPT_PACKETSIZE, AOO_ARG(n));
    }

    aoo_error set_ping_interval(int32_t n){
        return set_option(AOO_OPT_PING_INTERVAL, AOO_ARG(n));
    }

    aoo_error get_ping_interval(int32_t& n){
        return get_option(AOO_OPT_PING_INTERVAL, AOO_ARG(n));
    }

    aoo_error set_resend_enable(bool b){
        return set_option(AOO_OPT_RESEND_ENABLE, AOO_ARG(b));
    }

    aoo_error get_resend_enable(bool& b){
        return get_option(AOO_OPT_RESEND_ENABLE, AOO_ARG(b));
    }

    aoo_error set_resend_interval(int32_t n){
        return set_option(AOO_OPT_RESEND_INTERVAL, AOO_ARG(n));
    }

    aoo_error get_resend_interval(int32_t& n){
        return get_option(AOO_OPT_RESEND_INTERVAL, AOO_ARG(n));
    }

    aoo_error set_resend_maxnumframes(int32_t n){
        return set_option(AOO_OPT_RESEND_MAXNUMFRAMES, AOO_ARG(n));
    }

    aoo_error get_resend_maxnumframes(int32_t& n){
        return get_option(AOO_OPT_RESEND_MAXNUMFRAMES, AOO_ARG(n));
    }

    aoo_error set_source_timeout(int32_t n){
        return set_option(AOO_OPT_SOURCE_TIMEOUT, AOO_ARG(n));
    }

    aoo_error get_source_timeout(int32_t& n){
        return get_option(AOO_OPT_SOURCE_TIMEOUT, AOO_ARG(n));
    }

    virtual aoo_error set_option(int32_t opt, void *ptr, int32_t size) = 0;
    virtual aoo_error get_option(int32_t opt, void *ptr, int32_t size) = 0;

    //----------------- source options -------------------//
    // set/get source options (always threadsafe)

    aoo_error reset_source(const void *address, int32_t addrlen, aoo_id id) {
        return set_source_option(address, addrlen, id, AOO_OPT_RESET, AOO_ARG_NULL);
    }

    aoo_error get_source_format(const void *address, int32_t addrlen, aoo_id id,
                                aoo_format_storage& f)
    {
        return get_source_option(address, addrlen, id, AOO_OPT_FORMAT, AOO_ARG(f));
    }

    virtual aoo_error set_source_option(const void *address, int32_t addrlen, aoo_id id,
                                       int32_t opt, void *ptr, int32_t size) = 0;

    virtual aoo_error get_source_option(const void *address, int32_t addrlen, aoo_id id,
                                       int32_t opt, void *ptr, int32_t size) = 0;
protected:
    ~isink(){} // non-virtual!
};

inline isink * isink::create(aoo_id id, uint32_t flags){
    return aoo_sink_new(id, flags);
}

inline void isink::destroy(isink *sink){
    aoo_sink_free(sink);
}

} // aoo
