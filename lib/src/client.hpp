/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo_net.hpp"

namespace aoo {
namespace net {

class client final : public iclient {
public:
    client(int tcpsocket, void *udpsocket, aoo_sendfn fn);
    ~client();

    int32_t run() override;

    int32_t quit() override;

    int32_t connect(const char *host, int port,
                    const char *username, const char *pwd) override;

    int32_t disconnect() override;

    int32_t group_join(const char *group, const char *pwd) override;

    int32_t group_leave(const char *group) override;

    int32_t handle_message(const char *data, int32_t n, void *addr) override;

    int32_t send() override;

    int32_t events_available() override;

    int32_t handle_events(aoo_eventhandler fn, void *user) override;
private:
    int tcpsocket_;
    void *udpsocket_;
    aoo_sendfn sendfn_;
};

} // net
} // aoo
