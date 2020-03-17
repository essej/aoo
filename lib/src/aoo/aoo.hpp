#pragma once

#include "aoo.h"
#include "lfqueue.hpp"
#include "time_dll.hpp"

#include <memory>

namespace aoo {

/*//////////////////////// AoO source ///////////////////////*/

class isource {
public:
    class deleter {
    public:
        void operator()(isource *x){
            destroy(x);
        }
    };

    using pointer = std::unique_ptr<isource, deleter>;

    virtual ~isource(){}

    static isource * create(int32_t id);

    static void destroy(isource *x);

    virtual int32_t setup(const aoo_source_settings& settings) = 0;

    virtual int32_t add_sink(void *sink, int32_t id, aoo_replyfn fn) = 0;

    virtual int32_t remove_sink(void *sink, int32_t id) = 0;

    virtual void remove_all() = 0;

    virtual int32_t handle_message(const char *data, int32_t n,
                                void *endpoint, aoo_replyfn fn) = 0;

    virtual int32_t send() = 0;

    virtual int32_t process(const aoo_sample **data, int32_t n, uint64_t t) = 0;

    virtual bool events_available() = 0;

    virtual int32_t handle_events() = 0;

    //---------------------- options ----------------------//

    int32_t set_format(aoo_format& f){
        return set_option(aoo_opt_format, AOO_ARG(f));
    }

    int32_t get_format(aoo_format_storage& f){
        return get_option(aoo_opt_format, AOO_ARG(f));
    }

    int32_t set_buffersize(int32_t n){
        return set_option(aoo_opt_buffersize, AOO_ARG(n));
    }

    int32_t get_buffersize(int32_t& n){
        return get_option(aoo_opt_buffersize, AOO_ARG(n));
    }

    int32_t set_timefilter_bandwidth(float f){
        return set_option(aoo_opt_timefilter_bandwidth, AOO_ARG(f));
    }

    int32_t get_timefilter_bandwidth(float& f){
        return get_option(aoo_opt_timefilter_bandwidth, AOO_ARG(f));
    }

    int32_t set_packetsize(int32_t n){
        return set_option(aoo_opt_packetsize, AOO_ARG(n));
    }

    int32_t get_packetsize(int32_t& n){
        return get_option(aoo_opt_packetsize, AOO_ARG(n));
    }

    int32_t set_resend_buffersize(int32_t n){
        return set_option(aoo_opt_resend_buffersize, AOO_ARG(n));
    }

    int32_t set_resend_buffersize(int32_t& n){
        return get_option(aoo_opt_resend_buffersize, AOO_ARG(n));
    }

    //--------------------- sink options --------------------------//

    int32_t set_sink_channelonset(void *endpoint, int32_t id, int32_t onset){
        return set_sinkoption(endpoint, id, aoo_opt_channelonset, AOO_ARG(onset));
    }

    int32_t get_sink_channelonset(void *endpoint, int32_t id, int32_t& onset){
        return get_sinkoption(endpoint, id, aoo_opt_channelonset, AOO_ARG(onset));
    }
protected:
    virtual int32_t set_option(int32_t opt, void *ptr, int32_t size) = 0;
    virtual int32_t get_option(int32_t opt, void *ptr, int32_t size) = 0;
    virtual int32_t set_sinkoption(void *endpoint, int32_t id,
                                   int32_t opt, void *ptr, int32_t size) = 0;
    virtual int32_t get_sinkoption(void *endpoint, int32_t id,
                                   int32_t opt, void *ptr, int32_t size) = 0;
};

/*//////////////////////// AoO sink ///////////////////////*/

class isink {
public:
    class deleter {
    public:
        void operator()(isink *x){
            destroy(x);
        }
    };

    using pointer = std::unique_ptr<isink, deleter>;

    virtual ~isink(){}

    static isink * create(int32_t id);

    static void destroy(isink *x);

    virtual int32_t setup(const aoo_sink_settings& settings) = 0;

    virtual int32_t handle_message(const char *data, int32_t n,
                                   void *endpoint, aoo_replyfn fn) = 0;

    virtual int32_t process(uint64_t t) = 0;

    virtual bool events_available() = 0;

    virtual int32_t handle_events() = 0;

    virtual void invite(void *endpoint, aoo_replyfn,
                        int32_t id, int32_t chn) = 0;

    //---------------------- options ----------------------//

    int32_t set_buffersize(int32_t n){
        return set_option(aoo_opt_buffersize, AOO_ARG(n));
    }

    int32_t get_buffersize(int32_t& n){
        return get_option(aoo_opt_buffersize, AOO_ARG(n));
    }

    int32_t set_timefilter_bandwidth(float f){
        return set_option(aoo_opt_timefilter_bandwidth, AOO_ARG(f));
    }

    int32_t get_timefilter_bandwidth(float& f){
        return get_option(aoo_opt_timefilter_bandwidth, AOO_ARG(f));
    }

    int32_t set_ping_interval(int32_t n){
        return set_option(aoo_opt_ping_interval, AOO_ARG(n));
    }

    int32_t get_ping_interval(int32_t& n){
        return get_option(aoo_opt_ping_interval, AOO_ARG(n));
    }

    int32_t set_resend_limit(int32_t n){
        return set_option(aoo_opt_resend_limit, AOO_ARG(n));
    }

    int32_t get_resend_limit(int32_t& n){
        return get_option(aoo_opt_resend_limit, AOO_ARG(n));
    }

    int32_t set_resend_interval(int32_t n){
        return set_option(aoo_opt_resend_interval, AOO_ARG(n));
    }

    int32_t get_resend_interval(int32_t& n){
        return get_option(aoo_opt_resend_interval, AOO_ARG(n));
    }

    int32_t set_resend_maxnumframes(int32_t n){
        return set_option(aoo_opt_resend_maxnumframes, AOO_ARG(n));
    }

    int32_t get_resend_maxnumframes(int32_t& n){
        return get_option(aoo_opt_resend_maxnumframes, AOO_ARG(n));
    }

    int32_t set_resend_packetsize(int32_t n){
        return set_option(aoo_opt_resend_packetsize, AOO_ARG(n));
    }

    int32_t get_resend_packetsize(int32_t& n){
        return get_option(aoo_opt_resend_packetsize, AOO_ARG(n));
    }

    //----------------- source options -------------------//

    int32_t get_source_format(void *endpoint, int32_t id, aoo_format_storage& f){
        return get_sourceoption(endpoint, id, aoo_opt_format, AOO_ARG(f));
    }
protected:
    virtual int32_t set_option(int32_t opt, void *ptr, int32_t size) = 0;
    virtual int32_t get_option(int32_t opt, void *ptr, int32_t size) = 0;
    virtual int32_t set_sourceoption(void *endpoint, int32_t id,
                                   int32_t opt, void *ptr, int32_t size) = 0;
    virtual int32_t get_sourceoption(void *endpoint, int32_t id,
                                   int32_t opt, void *ptr, int32_t size) = 0;
};

} // aoo
