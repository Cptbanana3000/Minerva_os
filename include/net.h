#ifndef MINERVA_NET_H
#define MINERVA_NET_H

#include <stdint.h>

typedef struct {
    uint8_t local_ip[4];
    uint8_t gateway_ip[4];
    uint8_t gateway_mac[6];
    uint8_t gateway_mac_valid;
    uint32_t arp_requests;
    uint32_t arp_replies;
    uint32_t rx_frames;
    uint16_t last_ethertype;
} net_info_t;

void net_init(void);
const net_info_t *net_info(void);
int net_arp_probe_gateway(void);

#endif
