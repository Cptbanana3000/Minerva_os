#include <stdint.h>
#include "net.h"
#include "e1000.h"
#include "libc.h"

#define ETHERTYPE_ARP 0x0806u
#define ETHERTYPE_IPV4 0x0800u
#define ARP_HTYPE_ETHERNET 0x0001u
#define ARP_OP_REQUEST 0x0001u
#define ARP_OP_REPLY 0x0002u

typedef struct __attribute__((packed)) {
    uint8_t dst[6];
    uint8_t src[6];
    uint8_t ethertype[2];
} ethernet_header_t;

typedef struct __attribute__((packed)) {
    uint8_t htype[2];
    uint8_t ptype[2];
    uint8_t hlen;
    uint8_t plen;
    uint8_t oper[2];
    uint8_t sha[6];
    uint8_t spa[4];
    uint8_t tha[6];
    uint8_t tpa[4];
} arp_packet_t;

static net_info_t g_net;
static uint8_t rx_frame[1536];

static void put16be(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)(value & 0xFFu);
}

static uint16_t get16be(const uint8_t *src) {
    return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

void net_init(void) {
    memset(&g_net, 0, sizeof(g_net));
    g_net.local_ip[0] = 10;
    g_net.local_ip[1] = 0;
    g_net.local_ip[2] = 2;
    g_net.local_ip[3] = 15;
    g_net.gateway_ip[0] = 10;
    g_net.gateway_ip[1] = 0;
    g_net.gateway_ip[2] = 2;
    g_net.gateway_ip[3] = 2;
}

const net_info_t *net_info(void) {
    return &g_net;
}

static int ip_equal(const uint8_t *a, const uint8_t *b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static int net_handle_arp(const uint8_t *frame, uint16_t length) {
    if (length < sizeof(ethernet_header_t) + sizeof(arp_packet_t)) return 0;

    const ethernet_header_t *eth = (const ethernet_header_t*)frame;
    const arp_packet_t *arp = (const arp_packet_t*)(frame + sizeof(ethernet_header_t));
    if (get16be(eth->ethertype) != ETHERTYPE_ARP) return 0;
    if (get16be(arp->htype) != ARP_HTYPE_ETHERNET) return 0;
    if (get16be(arp->ptype) != ETHERTYPE_IPV4) return 0;
    if (arp->hlen != 6 || arp->plen != 4) return 0;
    if (get16be(arp->oper) != ARP_OP_REPLY) return 0;
    if (!ip_equal(arp->spa, g_net.gateway_ip)) return 0;
    if (!ip_equal(arp->tpa, g_net.local_ip)) return 0;

    memcpy(g_net.gateway_mac, arp->sha, sizeof(g_net.gateway_mac));
    g_net.gateway_mac_valid = 1;
    g_net.arp_replies++;
    return 1;
}

static void net_poll_frames(uint32_t limit) {
    for (uint32_t i = 0; i < limit; i++) {
        uint16_t length = 0;
        if (!e1000_receive_frame(rx_frame, sizeof(rx_frame), &length)) break;
        g_net.rx_frames++;
        if (length >= sizeof(ethernet_header_t)) {
            const ethernet_header_t *eth = (const ethernet_header_t*)rx_frame;
            g_net.last_ethertype = get16be(eth->ethertype);
        }
        net_handle_arp(rx_frame, length);
    }
}

int net_arp_probe_gateway(void) {
    const e1000_info_t *nic = e1000_info();
    uint8_t frame[60];
    ethernet_header_t *eth = (ethernet_header_t*)frame;
    arp_packet_t *arp = (arp_packet_t*)(frame + sizeof(ethernet_header_t));

    if (!nic->present || !nic->tx_ready || !nic->rx_ready || !nic->mac_valid) return 0;

    memset(frame, 0, sizeof(frame));
    for (uint32_t i = 0; i < 6; i++) eth->dst[i] = 0xFFu;
    memcpy(eth->src, nic->mac, 6);
    put16be(eth->ethertype, ETHERTYPE_ARP);

    put16be(arp->htype, ARP_HTYPE_ETHERNET);
    put16be(arp->ptype, ETHERTYPE_IPV4);
    arp->hlen = 6;
    arp->plen = 4;
    put16be(arp->oper, ARP_OP_REQUEST);
    memcpy(arp->sha, nic->mac, 6);
    memcpy(arp->spa, g_net.local_ip, 4);
    memcpy(arp->tpa, g_net.gateway_ip, 4);

    g_net.arp_requests++;
    if (!e1000_send_frame(frame, sizeof(frame))) return 0;

    for (uint32_t spin = 0; spin < 40000 && !g_net.gateway_mac_valid; spin++) {
        net_poll_frames(4);
    }
    return g_net.gateway_mac_valid;
}
