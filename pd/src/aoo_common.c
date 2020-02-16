#include "m_pd.h"
#include "aoo/aoo.h"

#include <stdio.h>
#include <inttypes.h>

#ifndef AOO_DEBUG_OSCTIME
#define AOO_DEBUG_OSCTIME 0
#endif

#define AOO_PD_MINPERIOD 0.5

#ifndef AOO_PD_OSCTIMEHACK
#define AOO_PD_OSCTIMEHACK 0
#endif

uint64_t aoo_pd_osctime(int n, t_float sr)
{
    uint64_t t = aoo_osctime_get();
#if AOO_PD_OSCTIMEHACK || AOO_DEBUG_OSCTIME
    double s = aoo_osctime_toseconds(t);
    double period = (double)n / sr;
    double diff;
    static PERTHREAD double last = 0;
    if (last > 0){
        diff = s - last;
    } else {
        diff = period;
    }
    last = s;
#endif
#if AOO_PD_OSCTIMEHACK
    // HACK to catch blocks calculated in a row because of Pd's ringbuffer scheduler
    static PERTHREAD uint64_t osc = 0;
    static PERTHREAD int32_t count = 0;
    if (diff > period * AOO_PD_MINPERIOD){
        osc = t;
        count = 0;
    } else {
        // approximate timestamp
        count++;
        t = aoo_osctime_addseconds(t, period * count);
    }
#endif
#if AOO_DEBUG_OSCTIME
    s = aoo_osctime_toseconds(t);
    fprintf(stderr, "osctime: %" PRIu64 ", seconds: %f, diff (ms): %f\n", t, s, diff * 1000.0);
    fflush(stderr);
#endif
    return t;
}
