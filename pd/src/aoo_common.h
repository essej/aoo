#pragma once

#include "m_pd.h"

uint64_t aoo_pd_osctime(int n, t_float sr);

int aoo_parseresend(void *x, aoo_sink_settings *s, int argc, t_atom *argv);

void aoo_defaultformat(aoo_format_storage *f, int nchannels);

int aoo_parseformat(void *x, aoo_format_storage *f, int argc, t_atom *argv);
