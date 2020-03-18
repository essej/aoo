#pragma once

#include "m_pd.h"
#include "aoo/aoo.h"

uint64_t aoo_pd_osctime(int n, t_float sr);

int aoo_parseresend(void *x, int argc, const t_atom *argv,
                    int32_t *limit, int32_t *interval,
                    int32_t *maxnumframes);

void aoo_defaultformat(aoo_format_storage *f, int nchannels);

int aoo_parseformat(void *x, aoo_format_storage *f, int argc, t_atom *argv);

int aoo_printformat(const aoo_format_storage *f, int argc, t_atom *argv);
