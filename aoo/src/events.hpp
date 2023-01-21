#pragma once

#include "detail.hpp"

#include "aoo/aoo_events.h"

#include "common/net_utils.hpp"

namespace aoo {

//---------------- endpoint event ------------------//

// we keep the union in a seperate base class, so that we
// can use the default copy constructor and assignment.
struct endpoint_event_union
{
    endpoint_event_union() = default;
    endpoint_event_union(AooEventType _type)
        : type(_type) {}
    union {
        AooEventType type;
        AooEvent event;
        AooEventEndpoint ep;
        AooEventEndpoint source;
        AooEventEndpoint sink;
        AooEventInvite invite;
        AooEventUninvite uninvite;
        AooEventPing ping;
        AooEventPingReply ping_reply;
        AooEventXRun xrun;
    };
};

struct endpoint_event : endpoint_event_union {
    endpoint_event() = default;

    endpoint_event(AooEventType _type) : endpoint_event_union(_type) {}

    endpoint_event(AooEventType _type, const endpoint& _ep)
        : endpoint_event(_type, _ep.address, _ep.id) {}

    endpoint_event(AooEventType _type, const ip_address& addr, AooId id)
        : endpoint_event_union(_type) {
        // only for endpoint events
        if (type != kAooEventXRun) {
            memcpy(&addr_, addr.address(), addr.length());
            ep.endpoint.address = &addr_;
            ep.endpoint.addrlen = addr.length();
            ep.endpoint.id = id;
        }
    }

    endpoint_event(const endpoint_event& other)
        : endpoint_event_union(other) {
        // only for sink events:
        if (type != kAooEventXRun) {
            memcpy(&addr_, other.addr_, sizeof(addr_));
            ep.endpoint.address = &addr_;
        }
    }

    endpoint_event& operator=(const endpoint_event& other) {
        endpoint_event_union::operator=(other);
        // only for sink events:
        if (type != kAooEventXRun) {
            memcpy(&addr_, other.addr_, sizeof(addr_));
            ep.endpoint.address = &addr_;
        }
        return *this;
    }
private:
    char addr_[ip_address::max_length];
};

} // namespace aoo
