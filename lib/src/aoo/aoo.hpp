#pragma once

#include "aoo.h"
#include "lfqueue.hpp"
#include "time_dll.hpp"

#include <memory>

namespace aoo {

/*//////////////////////// AoO source ///////////////////////*/

class isource {
public:
    virtual ~isource(){}

    static isource * create(int32_t id);

    static void destroy(isource *x);

    virtual void set_format(aoo_format& f) = 0;

    virtual void setup(aoo_source_settings& settings) = 0;

    virtual void add_sink(void *sink, int32_t id, aoo_replyfn fn) = 0;

    virtual void remove_sink(void *sink, int32_t id) = 0;

    virtual void remove_all() = 0;

    virtual void set_sink_channel(void *sink, int32_t id, int32_t chn) = 0;

    virtual void handle_message(const char *data, int32_t n,
                                void *endpoint, aoo_replyfn fn) = 0;

    virtual bool send() = 0;

    virtual bool process(const aoo_sample **data, int32_t n, uint64_t t) = 0;

    class deleter {
    public:
        void operator()(isource *x){
            destroy(x);
        }
    };

    using pointer = std::unique_ptr<isource, deleter>;
};

/*//////////////////////// AoO sink ///////////////////////*/

class isink {
public:
    virtual ~isink(){}

    static isink * create(int32_t id);

    static void destroy(isink *x);

    virtual void setup(aoo_sink_settings& settings) = 0;

    virtual int32_t handle_message(const char *data, int32_t n,
                                   void *endpoint, aoo_replyfn fn) = 0;

    virtual int32_t process(uint64_t t) = 0;


    class deleter {
    public:
        void operator()(isink *x){
            destroy(x);
        }
    };

    using pointer = std::unique_ptr<isink, deleter>;
};

} // aoo
