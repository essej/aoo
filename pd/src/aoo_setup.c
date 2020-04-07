/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "m_pd.h"

#include "aoo_common.h"

void aoo_send_tilde_setup(void);
void aoo_receive_tilde_setup(void);
void aoo_pack_tilde_setup(void);
void aoo_unpack_tilde_setup(void);
void aoo_route_setup(void);
void aoo_node_setup(void);

EXPORT void aoo_setup(void)
{
    char version[64];
    int offset = 0;
    offset += snprintf(version, sizeof(version), "%d.%d",
                       AOO_VERSION_MAJOR, AOO_VERSION_MINOR);
#if AOO_VERSION_BUGFIX > 0
    offset += snprintf(version + offset, sizeof(version) - offset,
                       ".%d", AOO_VERSION_BUGFIX);
#endif
#if AOO_VERSION_PRERELEASE > 0
    offset += snprintf(version + offset, sizeof(version) - offset,
                       "-pre%d", AOO_VERSION_PRERELEASE);
#endif

    post("AOO (audio over OSC) %s", version);
    post("  (c) 2020 Christof Ressi, Winfried Ritsch, et al.");

    aoo_initialize();

    aoo_send_tilde_setup();
    aoo_receive_tilde_setup();
    aoo_pack_tilde_setup();
    aoo_unpack_tilde_setup();
    aoo_route_setup();
    aoo_node_setup();
}
