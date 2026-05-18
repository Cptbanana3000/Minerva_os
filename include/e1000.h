#ifndef MINERVA_E1000_H
#define MINERVA_E1000_H

#include <stdint.h>
#include "pci.h"

typedef struct {
    uint8_t present;
    uint8_t mmio;
    uint8_t mac_valid;
    uint8_t tx_ready;
    uint8_t rx_ready;
    uint8_t mac[6];
    pci_device_t pci;
    uint32_t mmio_base;
    uint32_t io_base;
    uint32_t tx_attempts;
    uint32_t tx_sent;
    uint32_t tx_errors;
    uint8_t tx_last_status;
    uint32_t rx_packets;
    uint32_t rx_errors;
    uint16_t rx_last_length;
    uint16_t rx_last_type;
    uint8_t rx_last_status;
    uint8_t rx_last_errors;
} e1000_info_t;

void e1000_init(void);
const e1000_info_t *e1000_info(void);
int e1000_send_frame(const uint8_t *frame, uint16_t length);
int e1000_receive_frame(uint8_t *buffer, uint16_t buffer_size, uint16_t *length);
int e1000_send_test_frame(void);
int e1000_poll_receive(void);

#endif
