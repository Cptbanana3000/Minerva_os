#include <stdint.h>
#include "e1000.h"
#include "pci.h"
#include "paging.h"
#include "serial.h"
#include "libc.h"

#define E1000_MMIO_SIZE 0x20000u
#define E1000_REG_CTRL  0x00000u
#define E1000_REG_RCTL  0x00100u
#define E1000_REG_RAL0  0x05400u
#define E1000_REG_RAH0  0x05404u
#define E1000_REG_RDBAL 0x02800u
#define E1000_REG_RDBAH 0x02804u
#define E1000_REG_RDLEN 0x02808u
#define E1000_REG_RDH   0x02810u
#define E1000_REG_RDT   0x02818u
#define E1000_REG_TCTL  0x00400u
#define E1000_REG_TIPG  0x00410u
#define E1000_REG_TDBAL 0x03800u
#define E1000_REG_TDBAH 0x03804u
#define E1000_REG_TDLEN 0x03808u
#define E1000_REG_TDH   0x03810u
#define E1000_REG_TDT   0x03818u

#define E1000_CTRL_SLU  0x00000040u

#define E1000_RCTL_EN    0x00000002u
#define E1000_RCTL_UPE   0x00000008u
#define E1000_RCTL_MPE   0x00000010u
#define E1000_RCTL_BAM   0x00008000u
#define E1000_RCTL_SECRC 0x04000000u

#define E1000_TCTL_EN   0x00000002u
#define E1000_TCTL_PSP  0x00000008u
#define E1000_TCTL_CT   (0x10u << 4)
#define E1000_TCTL_COLD (0x40u << 12)

#define E1000_TX_CMD_EOP  0x01u
#define E1000_TX_CMD_IFCS 0x02u
#define E1000_TX_CMD_RS   0x08u
#define E1000_TX_STATUS_DD 0x01u

#define E1000_RX_STATUS_DD  0x01u
#define E1000_RX_STATUS_EOP 0x02u

#define E1000_TX_DESC_COUNT 8
#define E1000_TX_BUFFER_SIZE 1536
#define E1000_RX_DESC_COUNT 8
#define E1000_RX_BUFFER_SIZE 2048

#define PCI_COMMAND        0x04
#define PCI_COMMAND_IO     0x0001u
#define PCI_COMMAND_MEMORY 0x0002u
#define PCI_COMMAND_MASTER 0x0004u

typedef struct __attribute__((packed)) {
    uint32_t addr_low;
    uint32_t addr_high;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} e1000_tx_desc_t;

typedef struct __attribute__((packed)) {
    uint32_t addr_low;
    uint32_t addr_high;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} e1000_rx_desc_t;

static e1000_info_t g_e1000;
static e1000_tx_desc_t tx_descs[E1000_TX_DESC_COUNT] __attribute__((aligned(16)));
static uint8_t tx_buffers[E1000_TX_DESC_COUNT][E1000_TX_BUFFER_SIZE] __attribute__((aligned(16)));
static e1000_rx_desc_t rx_descs[E1000_RX_DESC_COUNT] __attribute__((aligned(16)));
static uint8_t rx_buffers[E1000_RX_DESC_COUNT][E1000_RX_BUFFER_SIZE] __attribute__((aligned(16)));
static uint32_t rx_next;

static uint32_t e1000_read32(uint32_t reg) {
    volatile uint32_t *addr = (volatile uint32_t*)(g_e1000.mmio_base + reg);
    return *addr;
}

static void e1000_write32(uint32_t reg, uint32_t value) {
    volatile uint32_t *addr = (volatile uint32_t*)(g_e1000.mmio_base + reg);
    *addr = value;
}

static void e1000_tx_init(void) {
    if (!g_e1000.mmio || !g_e1000.mmio_base || !g_e1000.mac_valid) return;

    memset(tx_descs, 0, sizeof(tx_descs));
    memset(tx_buffers, 0, sizeof(tx_buffers));
    for (uint32_t i = 0; i < E1000_TX_DESC_COUNT; i++) {
        tx_descs[i].addr_low = (uint32_t)&tx_buffers[i][0];
        tx_descs[i].addr_high = 0;
        tx_descs[i].status = E1000_TX_STATUS_DD;
    }

    e1000_write32(E1000_REG_CTRL, e1000_read32(E1000_REG_CTRL) | E1000_CTRL_SLU);
    e1000_write32(E1000_REG_TDBAL, (uint32_t)&tx_descs[0]);
    e1000_write32(E1000_REG_TDBAH, 0);
    e1000_write32(E1000_REG_TDLEN, sizeof(tx_descs));
    e1000_write32(E1000_REG_TDH, 0);
    e1000_write32(E1000_REG_TDT, 0);
    e1000_write32(E1000_REG_TIPG, 0x0060200Au);
    e1000_write32(E1000_REG_TCTL,
                  E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_CT | E1000_TCTL_COLD);
    g_e1000.tx_ready = 1;
    serial_write("e1000 TX ring ready\n");
}

static void e1000_rx_init(void) {
    if (!g_e1000.mmio || !g_e1000.mmio_base || !g_e1000.mac_valid) return;

    memset(rx_descs, 0, sizeof(rx_descs));
    memset(rx_buffers, 0, sizeof(rx_buffers));
    for (uint32_t i = 0; i < E1000_RX_DESC_COUNT; i++) {
        rx_descs[i].addr_low = (uint32_t)&rx_buffers[i][0];
        rx_descs[i].addr_high = 0;
    }
    rx_next = 0;

    e1000_write32(E1000_REG_RCTL, 0);
    e1000_write32(E1000_REG_RDBAL, (uint32_t)&rx_descs[0]);
    e1000_write32(E1000_REG_RDBAH, 0);
    e1000_write32(E1000_REG_RDLEN, sizeof(rx_descs));
    e1000_write32(E1000_REG_RDH, 0);
    e1000_write32(E1000_REG_RDT, E1000_RX_DESC_COUNT - 1);
    e1000_write32(E1000_REG_RCTL,
                  E1000_RCTL_EN |
                  E1000_RCTL_UPE |
                  E1000_RCTL_MPE |
                  E1000_RCTL_BAM |
                  E1000_RCTL_SECRC);
    g_e1000.rx_ready = 1;
    serial_write("e1000 RX ring ready\n");
}

static int e1000_device_id_supported(uint16_t device_id) {
    switch (device_id) {
    case 0x1000:
    case 0x1001:
    case 0x1004:
    case 0x1008:
    case 0x1009:
    case 0x100C:
    case 0x100D:
    case 0x100E:
    case 0x100F:
    case 0x1010:
    case 0x1011:
    case 0x1012:
    case 0x1013:
    case 0x1015:
    case 0x1016:
    case 0x1017:
    case 0x1018:
    case 0x1019:
    case 0x101A:
    case 0x101D:
    case 0x101E:
    case 0x1026:
    case 0x1027:
    case 0x1028:
    case 0x10B5:
    case 0x10D3:
        return 1;
    default:
        return 0;
    }
}

static int e1000_scan_visit(const pci_device_t *device, void *ctx) {
    (void)ctx;

    if (device->vendor_id != 0x8086u) return 0;
    if (device->class_code != 0x02u || device->subclass != 0x00u) return 0;
    if (!e1000_device_id_supported(device->device_id)) return 0;

    g_e1000.present = 1;
    g_e1000.pci = *device;
    return 1;
}

void e1000_init(void) {
    g_e1000.present = 0;
    g_e1000.mmio = 0;
    g_e1000.mac_valid = 0;
    g_e1000.tx_ready = 0;
    g_e1000.rx_ready = 0;
    g_e1000.mmio_base = 0;
    g_e1000.io_base = 0;
    g_e1000.tx_attempts = 0;
    g_e1000.tx_sent = 0;
    g_e1000.tx_errors = 0;
    g_e1000.tx_last_status = 0;
    g_e1000.rx_packets = 0;
    g_e1000.rx_errors = 0;
    g_e1000.rx_last_length = 0;
    g_e1000.rx_last_type = 0;
    g_e1000.rx_last_status = 0;
    g_e1000.rx_last_errors = 0;
    pci_scan(e1000_scan_visit, 0);

    if (g_e1000.present) {
        serial_write("e1000 Ethernet device found\n");
        uint16_t command = pci_config_read16(g_e1000.pci.bus,
                                             g_e1000.pci.slot,
                                             g_e1000.pci.function,
                                             PCI_COMMAND);
        command |= PCI_COMMAND_MASTER;

        if (g_e1000.pci.bar0 & 0x1u) {
            g_e1000.io_base = g_e1000.pci.bar0 & ~0x3u;
            command |= PCI_COMMAND_IO;
        } else {
            g_e1000.mmio = 1;
            g_e1000.mmio_base = g_e1000.pci.bar0 & ~0xFu;
            command |= PCI_COMMAND_MEMORY;
        }

        pci_config_write16(g_e1000.pci.bus,
                           g_e1000.pci.slot,
                           g_e1000.pci.function,
                           PCI_COMMAND,
                           command);

        if (g_e1000.mmio && g_e1000.mmio_base) {
            paging_map_range(g_e1000.mmio_base,
                             g_e1000.mmio_base,
                             E1000_MMIO_SIZE,
                             PAGE_WRITABLE);

            uint32_t ral = e1000_read32(E1000_REG_RAL0);
            uint32_t rah = e1000_read32(E1000_REG_RAH0);
            g_e1000.mac[0] = (uint8_t)(ral & 0xFFu);
            g_e1000.mac[1] = (uint8_t)((ral >> 8) & 0xFFu);
            g_e1000.mac[2] = (uint8_t)((ral >> 16) & 0xFFu);
            g_e1000.mac[3] = (uint8_t)((ral >> 24) & 0xFFu);
            g_e1000.mac[4] = (uint8_t)(rah & 0xFFu);
            g_e1000.mac[5] = (uint8_t)((rah >> 8) & 0xFFu);
            g_e1000.mac_valid = (uint8_t)((rah >> 31) & 0x1u);
            if (g_e1000.mac_valid) {
                serial_write("e1000 MAC address read\n");
            }
            e1000_rx_init();
            e1000_tx_init();
        }
    } else {
        serial_write("No e1000 Ethernet device\n");
    }
}

const e1000_info_t *e1000_info(void) {
    return &g_e1000;
}

int e1000_send_frame(const uint8_t *frame_data, uint16_t length) {
    if (!g_e1000.present || !g_e1000.tx_ready || !g_e1000.mac_valid) {
        g_e1000.tx_errors++;
        g_e1000.tx_last_status = 0;
        return 0;
    }
    if (!frame_data || length > E1000_TX_BUFFER_SIZE) {
        g_e1000.tx_errors++;
        g_e1000.tx_last_status = 0;
        return 0;
    }

    uint32_t tail = e1000_read32(E1000_REG_TDT) % E1000_TX_DESC_COUNT;
    e1000_tx_desc_t *desc = &tx_descs[tail];
    if ((desc->status & E1000_TX_STATUS_DD) == 0) {
        g_e1000.tx_errors++;
        g_e1000.tx_last_status = desc->status;
        return 0;
    }

    uint8_t *frame = tx_buffers[tail];
    uint16_t wire_length = length < 60 ? 60 : length;
    memset(frame, 0, wire_length);
    memcpy(frame, frame_data, length);

    desc->length = wire_length;
    desc->cso = 0;
    desc->cmd = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    desc->status = 0;
    desc->css = 0;
    desc->special = 0;

    g_e1000.tx_attempts++;
    __asm__ volatile ("" : : : "memory");
    e1000_write32(E1000_REG_TDT, (tail + 1) % E1000_TX_DESC_COUNT);

    for (uint32_t spin = 0; spin < 100000; spin++) {
        if (desc->status & E1000_TX_STATUS_DD) {
            g_e1000.tx_last_status = desc->status;
            g_e1000.tx_sent++;
            return 1;
        }
    }

    g_e1000.tx_last_status = desc->status;
    g_e1000.tx_errors++;
    return 0;
}

int e1000_receive_frame(uint8_t *buffer, uint16_t buffer_size, uint16_t *length) {
    if (!g_e1000.present || !g_e1000.rx_ready) {
        g_e1000.rx_errors++;
        g_e1000.rx_last_status = 0;
        g_e1000.rx_last_errors = 0;
        return 0;
    }

    e1000_rx_desc_t *desc = &rx_descs[rx_next];
    if ((desc->status & E1000_RX_STATUS_DD) == 0) {
        g_e1000.rx_last_status = desc->status;
        g_e1000.rx_last_errors = desc->errors;
        if (length) *length = 0;
        return 0;
    }

    g_e1000.rx_last_length = desc->length;
    g_e1000.rx_last_status = desc->status;
    g_e1000.rx_last_errors = desc->errors;
    if (desc->length >= 14) {
        uint8_t *frame = rx_buffers[rx_next];
        g_e1000.rx_last_type = (uint16_t)(((uint16_t)frame[12] << 8) | frame[13]);
        if (buffer && buffer_size) {
            uint16_t copy_length = desc->length;
            if (copy_length > buffer_size) copy_length = buffer_size;
            memcpy(buffer, frame, copy_length);
            if (length) *length = copy_length;
        } else if (length) {
            *length = desc->length;
        }
    } else {
        g_e1000.rx_last_type = 0;
        if (length) *length = 0;
    }

    if (desc->errors || (desc->status & E1000_RX_STATUS_EOP) == 0) {
        g_e1000.rx_errors++;
    } else {
        g_e1000.rx_packets++;
    }

    desc->status = 0;
    desc->errors = 0;
    desc->length = 0;
    e1000_write32(E1000_REG_RDT, rx_next);
    rx_next = (rx_next + 1) % E1000_RX_DESC_COUNT;
    return 1;
}

int e1000_send_test_frame(void) {
    uint8_t frame[60];
    if (!g_e1000.mac_valid) return 0;

    memset(frame, 0, sizeof(frame));
    for (uint32_t i = 0; i < 6; i++) frame[i] = 0xFFu;
    for (uint32_t i = 0; i < 6; i++) frame[6 + i] = g_e1000.mac[i];
    frame[12] = 0x88u;
    frame[13] = 0xB5u;
    memcpy(frame + 14, "MinervaOS e1000 TX", 18);
    return e1000_send_frame(frame, sizeof(frame));
}

int e1000_poll_receive(void) {
    return e1000_receive_frame(0, 0, 0);
}
