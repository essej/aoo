/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "time.hpp"
#include "log.hpp"

#include <stdexcept>
#include <string>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <cassert>
#include <sstream>

#ifdef _WIN32
# include <windows.h>
// to prevent segfault in LOG_* when initializing get_system_time
# include <iostream>
#else
# include <sys/time.h>
#endif

namespace aoo {

//------------------- time_tag -----------------------------//

#ifdef _WIN32

using system_time_fn = VOID (WINAPI *) (LPFILETIME);

// GetSystemTimePreciseAsFileTime is only available in Windows 8 and above.
// For older versions of Windows we fall back to GetSystemTimeAsFileTime.
system_time_fn get_system_time = [](){
    auto module = LoadLibraryA("Kernel32.dll");
    if (module){
        auto fn = (void *)GetProcAddress(module, "GetSystemTimePreciseAsFileTime");
        if (fn){
            return reinterpret_cast<system_time_fn>(fn);
            LOG_INFO("using GetSystemTimePreciseAsFileTime");
        } else {
            LOG_INFO("using GetSystemTimeAsFileTime");
        }
    } else {
        LOG_ERROR("couldn't open kernel32.dll!");
    }

    return &GetSystemTimeAsFileTime;
}();

#endif

// OSC time stamp (NTP time)
time_tag time_tag::now(){
#if 1
    // a) use OS specific clock
#if defined(_WIN32)
    // make sure to get the highest precision
    // LATER try to use GetSystemTimePreciseAsFileTime
    // (only available on Windows 8 and above)
    FILETIME ft;
    get_system_time(&ft);
    // GetSystemTimeAsFileTime returns the number of
    // 100-nanosecond ticks since Jan 1, 1601.
    LARGE_INTEGER date;
    date.HighPart = ft.dwHighDateTime;
    date.LowPart = ft.dwLowDateTime;
    auto d = lldiv(date.QuadPart, 10000000);
    auto seconds = d.quot;
    auto nanos = d.rem * 100;
    // Kudos to https://www.frenk.com/2009/12/convert-filetime-to-unix-timestamp/
    // Between Jan 1, 1601 and Jan 1, 1970 there are 11644473600 seconds
    seconds -= 11644473600;
#else
    struct timeval v;
    gettimeofday(&v, nullptr);
    auto seconds = v.tv_sec;
    auto nanos = v.tv_usec * 1000;
#endif // _WIN32
#else
    // b) use std system clock (1970 epoch)
    auto epoch = std::chrono::system_clock::now().time_since_epoch();
    auto s = std::chrono::duration_cast<std::chrono::seconds>(epoch);
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(epoch - s);
    auto seconds = s.count();
    auto nanos = ns.count();
    // LOG_DEBUG("seconds: " << seconds << ", nanos: " << nanos);
#endif
    // add number of seconds between 1900 and 1970 (including leap years!)
    uint32_t high = seconds + 2208988800UL;
    // fractional part in nanoseconds mapped to the range of uint32_t
    uint32_t low = nanos * 4.294967296; // 2^32 / 1e9
    return time_tag(high, low);
}

std::ostream& operator << (std::ostream& os, time_tag t){
    auto s = t.to_seconds();
    int days, hours, minutes, seconds, micros; // use 'int' for snprintf

    auto d = lldiv(s, 86400);
    days = d.quot;
    d = lldiv(d.rem, 3600);
    hours = d.quot;
    d = lldiv(d.rem, 60);
    minutes = d.quot;
    seconds = d.rem;
    micros = (s - (uint64_t)s) * 1000000.0;

    if (days){
        os << "[" << days << "]";
    }

    char buf[256];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%06d",
             hours, minutes, seconds, micros);

    os << buf;

    return os;
}

//------------------- NTP server --------------------//

#ifdef _WIN32

static std::string seconds_to_string(int sec)
{
    if (sec <= 0){
        return "-"; // shouldn't happen
    }

    int d = 0, h = 0, m = 0, s = 0, count = 0;
    div_t dv = div(sec, 60);
    s = dv.rem;
    if (dv.quot){
        dv = div(dv.quot, 60);
        m = dv.rem;
        if (dv.quot){
            dv = div(dv.quot, 24);
            h = dv.rem;
            d = dv.quot;
        }
    }
    char buf[64];
    if (d){
        count += snprintf(buf, 64, "%d days ", d);
    }
    snprintf(buf + count, 64 - count, "%02d:%02d:%02d", h, m, s);

    return buf;
}

static HKEY reg_openkey(HKEY key, const char *subkey)
{
    HKEY result;
    char buf[256];

    DWORD ret = RegOpenKeyExA(key, subkey, 0, KEY_QUERY_VALUE, &result);
    if (ret == ERROR_SUCCESS){
        return result;
    } else {
        snprintf(buf, sizeof(buf), "Registry: couldn't open key %s (%d)", subkey, (int)ret);

        throw std::domain_error(buf);
    }
}

static void reg_getvalue(HKEY key, const char *subkey, const char *value,
                        DWORD type, void *data, DWORD *size)
{
    char buf[256];
    DWORD rtype;

    DWORD ret = RegGetValueA(key, subkey, value, RRF_RT_ANY, &rtype, data, size);
    if (ret == ERROR_SUCCESS){
        if (rtype == type){
            return; // success
        } else {
            snprintf(buf, sizeof(buf), "Registry: wrong type for value %s", value);
        }
    } else {
        snprintf(buf, sizeof(buf), "Registry: couldn't get value %s in %s (%d)",
                 value, subkey, (int)ret);
    }

    throw std::domain_error(buf);
}

static bool service_running(const char *service)
{
    SC_HANDLE scm = OpenSCManagerA(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_CONNECT);
    if (scm == NULL)
        return false;

    SC_HANDLE hService = OpenServiceA(scm, service, GENERIC_READ);
    if (hService == NULL)
    {
        CloseServiceHandle(scm);
        return false;
    }

    bool running = false;
    SERVICE_STATUS status;
    if (QueryServiceStatus(hService, &status)){
        running = status.dwCurrentState == SERVICE_RUNNING;
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(scm);

    return running;
}

std::pair<bool, std::string> check_ntp_server()
{
    std::stringstream msg;
    HKEY w32time = nullptr;
    HKEY timeProviders = nullptr;
    int count = 0;

    try {
        if (!service_running("W32Time")) {
            return { false, "Windows time server is not running!" };
        }

        w32time = reg_openkey(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\services\\W32Time");
        // First get the time server(s) + flags from "Parameters/NtpServer"
        // e.g. "time.windows.com,0x9 pool.ntp.org,0x9"
        // Entries are separated by whitespace. Every server name is followed by
        // a comma and a hexidecimal flag value.
        char server[256];
        DWORD size = 256;

        reg_getvalue(w32time, "Parameters", "NtpServer", REG_SZ, server, &size);

        std::string_view sv(server, size - 1); // exclude \0!
        size_t offset = 0;
        bool warn_special_poll_interval = false;

        msg << "NTP servers:\n";

        while (offset != std::string_view::npos) {
            // skip leading whitespace
            offset = sv.find_first_not_of(' ', offset);
            if (offset == std::string_view::npos) {
                break;
            }

            // parse NTP server name and flags
            // NB: treat missing flags as "no flags"
            std::string_view name;
            int flags = 0;
            if (auto pos = sv.find(',', offset); pos != std::string_view::npos) {
                if (sscanf(&sv[pos + 1], "%i", &flags) != 1) {
                    throw std::domain_error("could not parse flag value in NtpServer list");
                }
                name = sv.substr(offset, pos - offset);
                offset = sv.find(' ', pos);
            } else {
                auto end = sv.find(' ', offset);
                if (end != std::string_view::npos) {
                    name = sv.substr(offset, end - offset);
                } else {
                    name = sv.substr(offset);
                }
                offset = end;
            }

            LOG_DEBUG("NTP server: " << name << ", flags: 0x" << std::hex << flags << std::dec);

            if (flags & 1) {
                // special poll interval
                timeProviders = reg_openkey(w32time, "TimeProviders");

                DWORD poll_interval;
                size = sizeof(DWORD);
                reg_getvalue(timeProviders, "NtpClient", "SpecialPollInterval",
                             REG_DWORD, &poll_interval, &size);
                auto poll_interval_str = seconds_to_string(poll_interval);

                if (count > 0) {
                    msg << "\n";
                }
                msg << "  " << name << " (special poll interval: " << poll_interval_str << ")";

                warn_special_poll_interval = true;
            } else {
                // min/max poll interval
                DWORD min_poll_interval, max_poll_interval;
                size = sizeof(DWORD);

                reg_getvalue(w32time, "Config", "MinPollInterval",
                             REG_DWORD, &min_poll_interval, &size);
                reg_getvalue(w32time, "Config", "MaxPollInterval",
                             REG_DWORD, &max_poll_interval, &size);
                // min/max poll interval are given in powers of 2
                auto min_poll_interval_str = seconds_to_string(1 << min_poll_interval);
                auto max_poll_interval_str = seconds_to_string(1 << max_poll_interval);

                if (count > 0) {
                    msg << "\n";
                }
                msg << "  " << name << " (min. poll interval: " << min_poll_interval_str
                    << ", max. poll interval: " << max_poll_interval_str << ")";
            }

            count++;
            // move to next list entry (if any)
        }

        if (warn_special_poll_interval) {
            msg << "\n\nNOTE: disable SpecialPollInterval for more accurate timing (see README)";
        }
    } catch (const std::exception& e) {
        return { false, e.what() };
    }

    if (w32time){
        RegCloseKey(w32time);
    }
    if (timeProviders){
        RegCloseKey(timeProviders);
    }

    if (count > 0) {
        return { true, msg.str() };
    } else {
        return { false, "No NTP servers provided." };
    }
}

#else

std::pair<bool, std::string> check_ntp_server() {
    return { true, "" };
}

#endif

} // aoo
