#pragma once

#include "aoo/aoo_events.h"

#include <memory>
#include <string>

namespace aoo {
namespace net {

struct event_handler {
    event_handler(AooEventHandler fn, void *user, AooThreadLevel level)
        : fn_(fn), user_(user), level_(level) {}

    template<typename T>
    void operator()(const T& event) const {
        fn_(user_, &reinterpret_cast<const AooEvent&>(event), level_);
    }
private:
    AooEventHandler fn_;
    void *user_;
    AooThreadLevel level_;
};

struct ievent {
    virtual ~ievent() {}

    virtual void dispatch(const event_handler& fn) const = 0;
};

using event_ptr = std::unique_ptr<ievent>;

struct net_error_event : ievent
{
    net_error_event(int32_t code, std::string msg)
        : code_(code), msg_(std::move(msg)) {}

    void dispatch(const event_handler& fn) const override {
        AooNetEventError e;
        e.type = kAooNetEventError;
        e.errorCode = code_;
        e.errorMessage = msg_.c_str();

        fn(e);
    }

    int32_t code_;
    std::string msg_;
};

} // namespace net
} // namespace aoo
