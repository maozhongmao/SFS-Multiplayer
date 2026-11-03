/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/mp_authority_wire.h"

static void put_u32(unsigned char* p, unsigned int v){
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

int main(void){
    unsigned char packet[5 + 4 * 3] = {0};
    int ids[MP_AUTHORITY_MAX_IDS];
    int n = -1;

    packet[0] = 5;
    put_u32(packet + 1, 3);
    put_u32(packet + 5, 737259214u);
    put_u32(packet + 9, 1030181600u);
    put_u32(packet + 13, 166222989u);
    assert(mp_parse_authority_payload(packet, (int)sizeof(packet) * 8, ids,
                                      MP_AUTHORITY_MAX_IDS, &n));
    assert(n == 3);
    assert(ids[0] == 737259214);
    assert(ids[1] == 1030181600);
    assert(ids[2] == 166222989);

    packet[0] = 5;
    put_u32(packet + 1, 0);
    n = -1;
    assert(mp_parse_authority_payload(packet, 40, ids, MP_AUTHORITY_MAX_IDS, &n));
    assert(n == 0);

    put_u32(packet + 1, 3);
    n = -1;
    assert(!mp_parse_authority_payload(packet, (int)sizeof(packet) * 8 - 8, ids,
                                       MP_AUTHORITY_MAX_IDS, &n));
    assert(n == 0);

    n = -1;
    assert(!mp_parse_authority_payload(packet, (int)sizeof(packet) * 8, ids, 2, &n));
    assert(n == 0);

    packet[0] = 11;
    n = -1;
    assert(!mp_parse_authority_payload(packet, (int)sizeof(packet) * 8, ids,
                                       MP_AUTHORITY_MAX_IDS, &n));
    assert(n == 0);

    packet[0] = 5;
    put_u32(packet + 1, MP_AUTHORITY_MAX_IDS + 1u);
    n = -1;
    assert(!mp_parse_authority_payload(packet, (int)sizeof(packet) * 8, ids,
                                       MP_AUTHORITY_MAX_IDS, &n));
    assert(n == 0);

    puts("mp_authority_wire_test OK");
    return 0;
}
