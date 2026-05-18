#include <stdint.h>
#include "pci.h"
#include "io.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PCI_ENABLE_BIT     0x80000000u

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t address = PCI_ENABLE_BIT |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) |
                       ((uint32_t)offset & 0xFCu);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, slot, function, offset);
    return (uint16_t)((value >> ((offset & 2u) * 8u)) & 0xFFFFu);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, slot, function, offset);
    return (uint8_t)((value >> ((offset & 3u) * 8u)) & 0xFFu);
}

void pci_config_write32(uint8_t bus,
                        uint8_t slot,
                        uint8_t function,
                        uint8_t offset,
                        uint32_t value) {
    uint32_t address = PCI_ENABLE_BIT |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) |
                       ((uint32_t)offset & 0xFCu);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

void pci_config_write16(uint8_t bus,
                        uint8_t slot,
                        uint8_t function,
                        uint8_t offset,
                        uint16_t value) {
    uint32_t aligned = offset & 0xFCu;
    uint32_t shift = (offset & 2u) * 8u;
    uint32_t current = pci_config_read32(bus, slot, function, (uint8_t)aligned);
    current &= ~(0xFFFFu << shift);
    current |= ((uint32_t)value << shift);
    pci_config_write32(bus, slot, function, (uint8_t)aligned, current);
}

static void pci_fill_device(pci_device_t *device,
                            uint8_t bus,
                            uint8_t slot,
                            uint8_t function,
                            uint16_t vendor_id) {
    uint32_t id = pci_config_read32(bus, slot, function, 0x00);
    uint32_t class_rev = pci_config_read32(bus, slot, function, 0x08);

    device->bus = bus;
    device->slot = slot;
    device->function = function;
    device->vendor_id = vendor_id;
    device->device_id = (uint16_t)(id >> 16);
    device->revision = (uint8_t)(class_rev & 0xFFu);
    device->prog_if = (uint8_t)((class_rev >> 8) & 0xFFu);
    device->subclass = (uint8_t)((class_rev >> 16) & 0xFFu);
    device->class_code = (uint8_t)((class_rev >> 24) & 0xFFu);
    device->bar0 = pci_config_read32(bus, slot, function, 0x10);
}

void pci_scan(pci_visit_fn visit, void *ctx) {
    if (!visit) return;

    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            for (uint32_t function = 0; function < 8; function++) {
                uint16_t vendor = pci_config_read16((uint8_t)bus,
                                                    (uint8_t)slot,
                                                    (uint8_t)function,
                                                    0x00);
                if (vendor == 0xFFFFu) continue;

                pci_device_t device;
                pci_fill_device(&device,
                                (uint8_t)bus,
                                (uint8_t)slot,
                                (uint8_t)function,
                                vendor);
                if (visit(&device, ctx)) return;
            }
        }
    }
}
