/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo_net.hpp"

namespace aoo {
namespace net {

class server final : public iserver {
public:
    server(int socket);
    ~server();

    int32_t run() override;

    int32_t quit() override;

    int32_t events_available() override;

    int32_t handle_events(aoo_eventhandler fn, void *user) override;
private:
    int socket_;
};

} // net
} // aoo
