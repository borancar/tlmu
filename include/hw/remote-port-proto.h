/*
 * QEMU remote port protocol parts.
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef REMOTE_PORT_PROTO_H__
#define REMOTE_PORT_PROTO_H__

#include <stdbool.h>
#include <string.h>

/*
 * Remote-Port (RP) is an inter-simulator protocol. It assumes a reliable
 * point to point communcation with the remote simulation environment.
 *
 * Setup
 * In the SETUP phase a mandatory HELLO packet is exchanged with optional
 * CFG packets following. HELLO packets are useful to ensure that both
 * sides are speaking the same protocol and using compatible versions.
 *
 * CFG packets are used to negotiate configuration options. At the moment
 * these remain unimplemented.
 *
 * Once the session is up, communication can start through various other
 * commands. The list can be found further down this document.
 * Commands are carried over RP packets. Every RP packet contains a header
 * with length, flags and an ID to track potential responses.
 * The header is followed by a packet specific payload. You'll find the
 * details of the various commands packet layouts here. Some commands can
 * carry data/blobs in their payload.
 */


#define RP_VERSION_MAJOR 3
#define RP_VERSION_MINOR 1

/* Could be auto generated.  */
enum rp_cmd {
    RP_CMD_nop         = 0,
    RP_CMD_hello       = 1,
    RP_CMD_cfg         = 2,
    RP_CMD_read        = 3,
    RP_CMD_write       = 4,
    RP_CMD_interrupt   = 5,
    RP_CMD_sync        = 6,
    RP_CMD_max         = 6
};

enum {
    RP_OPT_quantum = 0,
};

struct rp_cfg_state {
    uint64_t quantum;
};

enum {
    RP_PKT_FLAGS_optional      = 1 << 0,
    RP_PKT_FLAGS_response      = 1 << 1,
};

struct rp_pkt_hdr {
    uint32_t cmd;
    uint32_t len;
    uint32_t id;
    uint32_t flags;
    uint32_t dev;
} __attribute__ ((packed));

struct rp_pkt_cfg {
    struct rp_pkt_hdr hdr;
    uint32_t opt;
    uint8_t set;
} __attribute__ ((packed));

struct rp_version {
    uint16_t major;
    uint16_t minor;
} __attribute__ ((packed));

struct rp_pkt_hello {
    struct rp_pkt_hdr hdr;
    struct rp_version version;
} __attribute__ ((packed));


enum {
    RP_BUS_ATTR_EOP  =  (1 << 0),
};

struct rp_pkt_busaccess {
    struct rp_pkt_hdr hdr;
    uint64_t timestamp;
    uint64_t attributes;
    uint64_t addr;

    /* Length in bytes.  */
    uint32_t len;

    /* Width of each beat in bytes. Set to zero for unknown (let the remote
       side choose).  */
    uint32_t width;

    /* Width of streaming, must be a multiple of width.
       addr should repeat itself around this width. Set to same as len
       for incremental (normal) accesses.  In bytes.  */
    uint32_t stream_width;
} __attribute__ ((packed));

enum {
    WIRE_IRQ_0       = 0,
    WIRE_IRQ_MAX     = 127,
    WIRE_HALT_0      = 128,
    WIRE_HALT_MAX    = 159,
    WIRE_RESET_0     = 160,
    WIRE_RESET_MAX   = 191,

    WIRE_MAX = 192
};

struct rp_pkt_interrupt {
    struct rp_pkt_hdr hdr;
    uint64_t timestamp;
    uint64_t vector;
    uint32_t line;
    uint8_t val;
} __attribute__ ((packed));

struct rp_pkt_sync {
    struct rp_pkt_hdr hdr;
    uint64_t timestamp;
} __attribute__ ((packed));

struct rp_pkt {
    union {
        struct rp_pkt_hdr hdr;
        struct rp_pkt_hello hello;
        struct rp_pkt_busaccess busaccess;
        struct rp_pkt_interrupt interrupt;
        struct rp_pkt_sync sync;
    };
};

struct rp_peer_state {
    void *opaque;

    struct rp_pkt pkt;
    bool hdr_used;

    struct rp_version version;

    /* Used to normalize our clk.  */
    int64_t clk_base;

    struct rp_cfg_state local_cfg;
    struct rp_cfg_state peer_cfg;
};

const char *rp_cmd_to_string(enum rp_cmd cmd);
int rp_decode_hdr(struct rp_pkt *pkt);
int rp_decode_payload(struct rp_pkt *pkt);

void rp_encode_hdr(struct rp_pkt_hdr *hdr,
                   uint32_t cmd, uint32_t id, uint32_t dev, uint32_t len,
                   uint32_t flags);

size_t rp_encode_hello(uint32_t id, uint32_t dev, struct rp_pkt_hello *pkt,
                       uint16_t version_major, uint16_t version_minor);

static inline void *rp_busaccess_dataptr(struct rp_pkt_busaccess *pkt)
{
    /* Right after the packet.  */
    return pkt + 1;
}

size_t rp_encode_read(uint32_t id, uint32_t dev,
                      struct rp_pkt_busaccess *pkt,
                      int64_t clk,
                      uint64_t addr, uint32_t attr, uint32_t size,
                      uint32_t width, uint32_t stream_width);

size_t rp_encode_read_resp(uint32_t id, uint32_t dev,
                           struct rp_pkt_busaccess *pkt,
                           int64_t clk,
                           uint64_t addr, uint32_t attr, uint32_t size,
                           uint32_t width, uint32_t stream_width);

size_t rp_encode_write(uint32_t id, uint32_t dev,
                       struct rp_pkt_busaccess *pkt,
                       int64_t clk,
                       uint64_t addr, uint32_t attr, uint32_t size,
                       uint32_t width, uint32_t stream_width);

size_t rp_encode_write_resp(uint32_t id, uint32_t dev,
                       struct rp_pkt_busaccess *pkt,
                       int64_t clk,
                       uint64_t addr, uint32_t attr, uint32_t size,
                       uint32_t width, uint32_t stream_width);

size_t rp_encode_interrupt(uint32_t id, uint32_t dev,
                           struct rp_pkt_interrupt *pkt,
                           int64_t clk,
                           uint32_t line, uint64_t vector, uint8_t val);

size_t rp_encode_sync(uint32_t id, uint32_t dev,
                      struct rp_pkt_sync *pkt,
                      int64_t clk);

size_t rp_encode_sync_resp(uint32_t id, uint32_t dev,
                           struct rp_pkt_sync *pkt,
                           int64_t clk);

/* Dynamically resizable remote port pkt.  */

typedef struct RemotePortDynPkt {
    struct rp_pkt *pkt;
    size_t size;
} RemotePortDynPkt;

/*
 * Make sure dpkt is allocated and has enough room.
 */

void rp_dpkt_alloc(RemotePortDynPkt *dpkt, size_t size);

void rp_dpkt_swap(RemotePortDynPkt *a, RemotePortDynPkt *b);

/*
 * Check if the dpkt is valid. Used for debugging purposes.
 */

bool rp_dpkt_is_valid(RemotePortDynPkt *dpkt);

/*
 * Invalidate the dpkt. Used for debugging purposes.
 */

void rp_dpkt_invalidate(RemotePortDynPkt *dpkt);

void rp_dpkt_free(RemotePortDynPkt *dpkt);

#endif
