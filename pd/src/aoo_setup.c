#include "m_pd.h"

#include "aoo_common.h"

void aoo_send_tilde_setup(void);
void aoo_receive_tilde_setup(void);
void aoo_pack_tilde_setup(void);
void aoo_unpack_tilde_setup(void);
void aoo_route_setup(void);
void aoo_server_setup(void);

EXPORT void aoo_setup(void)
{
    aoo_initialize();

    aoo_send_tilde_setup();
    aoo_receive_tilde_setup();
    aoo_pack_tilde_setup();
    aoo_unpack_tilde_setup();
    aoo_route_setup();
    aoo_server_setup();
}
