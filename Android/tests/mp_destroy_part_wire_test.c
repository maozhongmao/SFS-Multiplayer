/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/mp_destroy_part_wire.h"

static void put_bits(unsigned char* out, int* bit, uint64_t value, int count){
    for(int i = 0; i < count; ++i, ++*bit)
        if((value >> i) & 1u) out[*bit >> 3] |= (unsigned char)(1u << (*bit & 7));
}
static void put_u32(unsigned char* out, int* bit, uint32_t value){ put_bits(out, bit, value, 32); }
static void put_f64(unsigned char* out, int* bit, double value){ uint64_t raw; memcpy(&raw, &value, sizeof(raw)); put_bits(out, bit, raw, 64); }

int main(void){
    unsigned char payload[19] = {0};
    int bit = 0;
    put_bits(payload, &bit, 13, 8);
    put_f64(payload, &bit, 1234.5);
    put_u32(payload, &bit, 7654321u);
    put_u32(payload, &bit, 998877u);
    put_bits(payload, &bit, 1, 1);
    put_bits(payload, &bit, 4, 8);
    assert(bit == 145);

    mp_destroy_part_t event;
    assert(mp_parse_destroy_part(payload, bit, &event));
    assert(event.world_time == 1234.5);
    assert(event.rocket_id == 7654321);
    assert(event.part_id == 998877);
    assert(event.create_explosion == 1);
    assert(event.reason == 4);
    assert(!mp_parse_destroy_part(payload, bit - 1, &event));
    payload[0] = 12;
    assert(!mp_parse_destroy_part(payload, bit, &event));
    puts("mp_destroy_part_wire_test OK");
    return 0;
}
