/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MP_DESTROY_PART_WIRE_H
#define MP_DESTROY_PART_WIRE_H

#include <stdint.h>
#include <string.h>
#include "mp_types.h"

typedef struct {
    const unsigned char* data;
    int bits;
    int pos;
    int failed;
} mp_destroy_part_reader_t;

static inline uint64_t mp_destroy_part_bits(mp_destroy_part_reader_t* r, int count){
    uint64_t value = 0;
    if(!r || count < 0 || count > 64 || r->pos < 0 || r->pos + count > r->bits){ if(r) r->failed = 1; return 0; }
    for(int i = 0; i < count; ++i, ++r->pos)
        value |= (uint64_t)((r->data[r->pos >> 3] >> (r->pos & 7)) & 1u) << i;
    return value;
}

static inline int mp_parse_destroy_part(const unsigned char* payload, int payload_bits, mp_destroy_part_t* out){
    if(!payload || !out || payload_bits != 145 || payload[0] != 13) return 0;
    mp_destroy_part_reader_t r = { payload, payload_bits, 0, 0 };
    if(mp_destroy_part_bits(&r, 8) != 13) return 0;
    uint64_t wt_bits = mp_destroy_part_bits(&r, 64);
    mp_destroy_part_t packet;
    memcpy(&packet.world_time, &wt_bits, sizeof(packet.world_time));
    packet.rocket_id = (int32_t)(uint32_t)mp_destroy_part_bits(&r, 32);
    packet.part_id = (int32_t)(uint32_t)mp_destroy_part_bits(&r, 32);
    packet.create_explosion = (int)mp_destroy_part_bits(&r, 1);
    packet.reason = (unsigned)mp_destroy_part_bits(&r, 8);
    if(r.failed || r.pos != payload_bits || packet.rocket_id < 0 || packet.part_id < 0) return 0;
    *out = packet;
    return 1;
}

#endif
