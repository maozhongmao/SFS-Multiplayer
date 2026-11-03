/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MP_AUTHORITY_WIRE_H
#define MP_AUTHORITY_WIRE_H

#include <stddef.h>
#include <stdint.h>

#define MP_AUTHORITY_MAX_IDS 256

static inline uint32_t mp_authority_read_u32(const unsigned char* p){
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Payload includes the packet type byte: [type=5][count:u32][rocket ids:i32...]. */
static inline int mp_parse_authority_payload(const unsigned char* payload, int payload_bits,
                                             int* ids, int capacity, int* out_count){
    if(out_count) *out_count = 0;
    if(!payload || !ids || !out_count || capacity < 0 || payload_bits < 40) return 0;
    if(payload_bits > 40 + MP_AUTHORITY_MAX_IDS * 32 || (payload_bits & 7) != 0) return 0;
    if(payload[0] != 5) return 0;

    const uint32_t count = mp_authority_read_u32(payload + 1);
    if(count > MP_AUTHORITY_MAX_IDS || count > (uint32_t)capacity) return 0;
    if((uint32_t)payload_bits != 40u + count * 32u) return 0;

    for(uint32_t i = 0; i < count; ++i)
        ids[i] = (int32_t)mp_authority_read_u32(payload + 5 + i * 4u);
    *out_count = (int)count;
    return 1;
}

#endif
