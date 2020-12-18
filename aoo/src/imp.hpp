#pragma once

#include <stdint.h>

namespace aoo {

uint32_t make_version();

bool check_version(uint32_t version);

char * copy_string(const char *s);

void * copy_sockaddr(const void *sa, int32_t len);


namespace net {

int32_t parse_pattern(const char *msg, int32_t n, int32_t *type);

} // net

} // aoo
