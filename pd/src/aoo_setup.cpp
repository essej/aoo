/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo_common.hpp"

#include "string.h"
#include "stdlib.h"

// setup function
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#elif __GNUC__ >= 4
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT
#endif

void aoo_send_tilde_setup(void);
void aoo_receive_tilde_setup(void);
void aoo_pack_tilde_setup(void);
void aoo_unpack_tilde_setup(void);
void aoo_route_setup(void);
void aoo_node_setup(void);
void aoo_server_setup(void);
void aoo_client_setup(void);

extern "C" EXPORT void aoo_setup(void)
{
    post("AOO (audio over OSC) %s", aoo_version_string());
    post("  (c) 2020 Christof Ressi, Winfried Ritsch, et al.");

    aoo_initialize();

    std::string msg;
    if (aoo::check_ntp_server(msg)){
        post("%s", msg.c_str());
    } else {
        error("%s", msg.c_str());
    }

    post("");

    aoo_send_tilde_setup();
    aoo_receive_tilde_setup();
    aoo_pack_tilde_setup();
    aoo_unpack_tilde_setup();
    aoo_route_setup();
    aoo_node_setup();
    aoo_server_setup();
    aoo_client_setup();
}
