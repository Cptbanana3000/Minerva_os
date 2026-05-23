#!/usr/bin/env python3
import struct
import sys

SECTOR_SIZE = 512
TOTAL_SECTORS = 4096
RESERVED_SECTORS = 32
FAT_SECTORS = 32
SECTORS_PER_CLUSTER = 1
ROOT_CLUSTER = 2

def put16(buf, offset, value):
    buf[offset:offset + 2] = struct.pack("<H", value)


def put32(buf, offset, value):
    buf[offset:offset + 4] = struct.pack("<I", value)


def short_name(name):
    base, _, ext = name.partition(".")
    return base.upper().ljust(8)[:8].encode("ascii") + ext.upper().ljust(3)[:3].encode("ascii")


def make_test_bmp():
    width = 16
    height = 12
    row_stride = ((width * 3 + 3) // 4) * 4
    pixel_size = row_stride * height
    file_size = 54 + pixel_size
    bmp = bytearray(file_size)

    bmp[0:2] = b"BM"
    put32(bmp, 2, file_size)
    put32(bmp, 10, 54)
    put32(bmp, 14, 40)
    put32(bmp, 18, width)
    put32(bmp, 22, height)
    put16(bmp, 26, 1)
    put16(bmp, 28, 24)
    put32(bmp, 34, pixel_size)

    for y in range(height):
        for x in range(width):
            r = (x * 255) // (width - 1)
            g = ((height - 1 - y) * 255) // (height - 1)
            b = 80 if ((x // 4 + y // 3) % 2) else 210
            offset = 54 + y * row_stride + x * 3
            bmp[offset + 0] = b
            bmp[offset + 1] = g
            bmp[offset + 2] = r

    return bytes(bmp)


def make_test_wav():
    sample_rate = 8000
    bits_per_sample = 8
    channels = 1
    sample_count = 256
    data = bytearray(sample_count)

    for i in range(sample_count):
        phase = i % 32
        data[i] = 96 if phase < 16 else 160

    byte_rate = sample_rate * channels * bits_per_sample // 8
    block_align = channels * bits_per_sample // 8
    wav = bytearray(44 + len(data))
    wav[0:4] = b"RIFF"
    put32(wav, 4, 36 + len(data))
    wav[8:12] = b"WAVE"
    wav[12:16] = b"fmt "
    put32(wav, 16, 16)
    put16(wav, 20, 1)
    put16(wav, 22, channels)
    put32(wav, 24, sample_rate)
    put32(wav, 28, byte_rate)
    put16(wav, 32, block_align)
    put16(wav, 34, bits_per_sample)
    wav[36:40] = b"data"
    put32(wav, 40, len(data))
    wav[44:44 + len(data)] = data
    return bytes(wav)


FILES = [
    ("README.TXT", b"Welcome to MinervaOS FAT32.\nTry: ls and cat ABOUT.TXT\n"),
    ("ABOUT.TXT", b"Phase 4 has begun.\nThis file was loaded from a FAT32 disk image.\n"),
    ("PKGS.TXT", b"THEME|THEME.PKG|Theme presets\nWEB|WEB.PKG|HTTPS browser\nSDK|SDK.PKG|App SDK seed\n"),
    ("APPS.TXT", b"TERM|terminal|-|Shell window\nEDIT|editor|NOTE.TXT|Text editor\nVIEW|viewer|TEST.BMP|BMP viewer\nAUDIO|audio|AUDIO.WAV|WAV player\nWEB|browser|https;//example.com/|HTTPS browser\n"),
    ("SDK.TXT", b"Minerva App SDK v1\nAPPS.TXT row: ID|launcher|target|summary\nLaunchers: terminal editor viewer audio browser\nCommands: app add ID launcher target summary; app check; app run ID\n"),
    ("NEAR.TXT", b"A" * 511),
    ("NEAR2.TXT", b"B" * 1023),
    ("TEST.BMP", make_test_bmp()),
    ("AUDIO.WAV", make_test_wav()),
]


def write_file_entry(image, entry_index, name, cluster, data):
    root_offset = (RESERVED_SECTORS + FAT_SECTORS + (ROOT_CLUSTER - 2)) * SECTOR_SIZE
    offset = root_offset + entry_index * 32
    image[offset:offset + 11] = short_name(name)
    image[offset + 11] = 0x20
    put16(image, offset + 20, (cluster >> 16) & 0xFFFF)
    put16(image, offset + 26, cluster & 0xFFFF)
    put32(image, offset + 28, len(data))


def main():
    if len(sys.argv) != 2:
        print("usage: make_fat32_image.py <output>", file=sys.stderr)
        return 1

    image = bytearray(TOTAL_SECTORS * SECTOR_SIZE)

    boot = 0
    image[boot:boot + 3] = b"\xEB\x58\x90"
    image[boot + 3:boot + 11] = b"MINERVA "
    put16(image, boot + 11, SECTOR_SIZE)
    image[boot + 13] = SECTORS_PER_CLUSTER
    put16(image, boot + 14, RESERVED_SECTORS)
    image[boot + 16] = 1
    put16(image, boot + 17, 0)
    put16(image, boot + 19, 0)
    image[boot + 21] = 0xF8
    put16(image, boot + 22, 0)
    put16(image, boot + 24, 63)
    put16(image, boot + 26, 16)
    put32(image, boot + 28, 0)
    put32(image, boot + 32, TOTAL_SECTORS)
    put32(image, boot + 36, FAT_SECTORS)
    put16(image, boot + 40, 0)
    put16(image, boot + 42, 0)
    put32(image, boot + 44, ROOT_CLUSTER)
    put16(image, boot + 48, 1)
    put16(image, boot + 50, 6)
    image[boot + 64] = 0x80
    image[boot + 66] = 0x29
    put32(image, boot + 67, 0x4D4E5256)
    image[boot + 71:boot + 82] = b"MINERVAOS  "
    image[boot + 82:boot + 90] = b"FAT32   "
    image[boot + 510:boot + 512] = b"\x55\xAA"

    fsinfo = SECTOR_SIZE
    put32(image, fsinfo + 0, 0x41615252)
    put32(image, fsinfo + 484, 0x61417272)
    put32(image, fsinfo + 488, 0xFFFFFFFF)
    put32(image, fsinfo + 492, 0xFFFFFFFF)
    image[fsinfo + 510:fsinfo + 512] = b"\x55\xAA"

    fat_offset = RESERVED_SECTORS * SECTOR_SIZE
    fat = image[fat_offset:fat_offset + FAT_SECTORS * SECTOR_SIZE]
    put32(fat, 0, 0x0FFFFFF8)
    put32(fat, 4, 0x0FFFFFFF)
    put32(fat, ROOT_CLUSTER * 4, 0x0FFFFFFF)

    next_cluster = 3
    data_start = RESERVED_SECTORS + FAT_SECTORS
    for index, (name, data) in enumerate(FILES):
        cluster_size = SECTORS_PER_CLUSTER * SECTOR_SIZE
        cluster_count = max(1, (len(data) + cluster_size - 1) // cluster_size)
        first_cluster = next_cluster

        for chunk_index in range(cluster_count):
            cluster = next_cluster
            next_cluster += 1

            next_value = 0x0FFFFFFF
            if chunk_index + 1 < cluster_count:
                next_value = cluster + 1
            put32(fat, cluster * 4, next_value)

            data_offset = (data_start + (cluster - 2) * SECTORS_PER_CLUSTER) * SECTOR_SIZE
            chunk_start = chunk_index * cluster_size
            chunk = data[chunk_start:chunk_start + cluster_size]
            image[data_offset:data_offset + len(chunk)] = chunk

        write_file_entry(image, index, name, first_cluster, data)

    image[fat_offset:fat_offset + FAT_SECTORS * SECTOR_SIZE] = fat

    with open(sys.argv[1], "wb") as f:
        f.write(image)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
