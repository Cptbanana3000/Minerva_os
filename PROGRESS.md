# MinervaOS Progress Log

This file records how MinervaOS is growing, especially the filesystem work in
Phase 4. `PHASES.md` is the checklist; this log explains the path, the safety
rules, and the tests used along the way.

---

## Current Shape

MinervaOS is a 32-bit x86 hobby OS that boots through a custom real-mode
bootloader, switches to protected mode, enters VGA mode 13h, and runs a small
graphical desktop with windows, icons, a taskbar, mouse input, keyboard input,
serial logging, a heap allocator, paging, and a terminal window.

Phase 4 is now complete: persistent FAT32 storage supports list, read, create,
write, append, truncate, delete, and rename for root-directory 8.3 files.

---

## Foundation Already Built

Before filesystem work, MinervaOS had:

- BIOS bootloader loading the kernel from a floppy image.
- 32-bit protected mode with a GDT.
- IDT, ISR stubs, PIC remapping, PIT timer interrupts.
- VGA text and VGA mode 13h graphics.
- PS/2 keyboard and mouse.
- Double-buffered graphics primitives.
- Window manager with Z-order, focus, close, minimize, taskbar, and desktop icons.
- Terminal window with shell commands.
- Heap allocator, physical memory bitmap, paging, and a basic libc.
- ATA PIO read support for reading sectors from the FAT32 disk image.

---

## Phase 4 Filesystem Growth

### 1. Read-Only FAT32

The first filesystem milestone was read-only FAT32 support:

- Mount FAT32 by reading the BPB.
- Locate the FAT and data region.
- Walk the root directory cluster chain.
- Convert FAT 8.3 names into display names.
- Read file contents by following FAT cluster chains.

This enabled:

```text
ls
cat README.TXT
cat ABOUT.TXT
```

The read path was verified in QEMU before write support was attempted.

### 2. VFS-Style Read API

Instead of making applications call the raw `fs_read_file()` helper forever, a
small file-handle API was added:

- `fs_file_t`
- `fs_open()`
- `fs_read()`
- `fs_file_size()`
- `fs_tell()`
- `fs_close()`

`fs_read_file()` now wraps this API for compatibility. This kept existing shell
commands working while giving future apps a cleaner file surface.

### 3. ATA Sector Write

Before FAT32 could mutate anything, the ATA driver gained a single-sector write:

- ATA command `0x30` for PIO write.
- 256 word writes to the data port.
- ATA flush command `0xE7`.

This was kept as a low-level primitive: no filesystem policy lives in the ATA
driver.

### 4. Create Empty FAT32 Files

The first real FAT32 mutation was deliberately tiny:

- Root directory only.
- Strict FAT 8.3 names only.
- Create an archive directory entry.
- File size is `0`.
- First cluster is `0`.
- No FAT allocation yet.

Terminal command:

```text
touch TEST.TXT
ls
```

Expected result:

```text
TEST.TXT 0
```

This proved directory entry mutation worked without touching FAT chains.

### 5. Write One-Cluster Empty Files

Next, MinervaOS learned to write content into an existing empty file:

- File must already exist.
- File must have size `0` and no cluster.
- Allocate one free cluster.
- Write data into that cluster.
- Mark the FAT entry as EOC.
- Update the directory entry first cluster and size.
- Refuse data larger than one cluster.

Terminal command:

```text
touch NOTE.TXT
write NOTE.TXT hello
cat NOTE.TXT
ls
```

Expected result:

```text
hello
NOTE.TXT 5
```

### 6. Overwrite One-Cluster Files

After creating and writing worked, `write` was extended to overwrite an existing
single-cluster file:

- Reuse the existing cluster.
- Refuse multi-cluster files.
- Zero-fill the whole cluster before writing new bytes.
- Update the file size.

This prevents shorter overwrites from leaking old file contents.

Terminal command:

```text
write NOTE.TXT hello
write NOTE.TXT bye
cat NOTE.TXT
ls
```

Expected result:

```text
bye
NOTE.TXT 3
```

### 7. Append Within One Cluster

Then `append` was added:

- File must have exactly one cluster.
- Appended data must fit inside that cluster.
- Existing content is preserved.
- New bytes are written at the current file size.
- Directory size is updated.

Terminal command:

```text
touch LOG.TXT
write LOG.TXT hello
append LOG.TXT world
cat LOG.TXT
ls
```

Expected result:

```text
helloworld
LOG.TXT 10
```

### 8. Extend Append Into a Second Cluster

The first FAT chain extension was added next:

- Start from a valid one-cluster file.
- Append may grow the file up to two clusters.
- If the append crosses the first cluster boundary:
  - allocate one new free cluster;
  - write overflow bytes into the new cluster;
  - mark the new cluster EOC;
  - link the old cluster to the new cluster;
  - update file size.

The FAT32 image generator now includes `NEAR.TXT`, a 511-byte file, so this can
be tested without typing hundreds of characters.

Terminal command:

```text
ls
append NEAR.TXT zz
ls
cat NEAR.TXT
append NEAR2.TXT zz
ls
cat NEAR2.TXT
```

Expected result:

```text
NEAR.TXT 513
```

`cat NEAR.TXT` shows many `A` characters followed by `zz`. The terminal window
scrolls, so only the tail of the output is visible.

### 9. General Multi-Cluster Append

The append path was then generalized from a two-cluster proof into a real chain
appender:

- Walk the FAT chain to the cluster that contains the current file end.
- Append into remaining space in that final cluster first.
- Allocate new clusters one at a time while data remains.
- Write each new cluster, mark it EOC, then link the previous tail to it.
- Update the directory entry size after the data and FAT path succeeds.

The FAT32 image generator was also updated to create multi-cluster seed files.
`NEAR2.TXT` is a 1023-byte file that can cross into a third cluster with a tiny
append command:

```text
append NEAR2.TXT zz
ls
cat NEAR2.TXT
```

Expected result:

```text
NEAR2.TXT 1025
```

### 10. Truncate and Free Cluster Chains

Once append could grow FAT chains, the next step was learning how to free them:

- Find the root directory entry.
- Walk the file's FAT chain.
- Write `0` to every FAT entry in that chain.
- Leave the directory entry in place.
- Set first cluster to `0`.
- Set size to `0`.

Terminal command:

```text
truncate NEAR2.TXT
ls
write NEAR2.TXT reset
cat NEAR2.TXT
```

Expected result:

```text
reset
NEAR2.TXT 5
```

This gives MinervaOS a clean way to reuse multi-cluster files without requiring
overwrite-across-chain support yet.

### 11. File Write Mode API

The proven low-level write pieces were wrapped in a stable public API:

```c
int fs_write(const char *name, const uint8_t *buffer, uint32_t size, uint32_t flags);
```

Supported flags:

- `FS_WRITE_CREATE`
- `FS_WRITE_TRUNCATE`
- `FS_WRITE_APPEND`
- `FS_WRITE_EXCL`

This lets callers express intent directly:

- Create a file only if it does not exist.
- Truncate and rewrite an existing file.
- Append to an existing file.
- Create missing files when desired.

The terminal `write` and `append` commands now call this public mode API instead
of calling the lower-level helpers directly.

### 12. Delete Files

Delete was added after truncate because it could reuse the same safe
free-chain behavior:

- Find the root directory entry.
- Reject directories.
- Free the file's FAT cluster chain.
- Mark the directory entry deleted with `0xE5`.
- Write the directory sector back.

Terminal command:

```text
delete NAME
```

Smoke test:

```text
write GONE.TXT bye
ls
delete GONE.TXT
ls
cat GONE.TXT
```

Expected result: `GONE.TXT` appears before delete, disappears after delete, and
`cat GONE.TXT` fails.

### 13. Rename Files

Rename was the final Phase 4 filesystem mutation:

- Find the source root directory entry.
- Reject directories.
- Validate the destination as a strict 8.3 name.
- Reject the rename if the destination already exists.
- Replace only the 11-byte short name in the existing directory entry.
- Leave file data and FAT chains untouched.

Terminal command:

```text
rename OLD.TXT NEW.TXT
```

Smoke test:

```text
write OLD.TXT hello
ls
rename OLD.TXT NEW.TXT
ls
cat NEW.TXT
cat OLD.TXT
```

Expected result: `OLD.TXT` disappears, `NEW.TXT` appears with the same size,
`cat NEW.TXT` prints `hello`, and `cat OLD.TXT` fails.

### 14. Chunked `cat`

The original `cat` used a fixed 512-byte buffer, which was fine for tiny files
but not enough once a test file crossed a cluster boundary.

`cat` now streams through:

- `fs_open()`
- repeated `fs_read()` calls
- `fs_close()`

This also exercises the public file-handle API in normal shell use.

---

## Safety Rules Used for FAT32 Write Work

The FAT32 write path was built with strict limits:

- Root directory only.
- FAT 8.3 names only.
- No long filename support.
- No directory creation yet.
- No arbitrary overwrite across cluster chains yet.
- Every feature is tested in QEMU before moving to the next slice.

This keeps the dangerous parts of FAT32 isolated: cluster allocation, FAT chain
updates, directory entry updates, and partial-write behavior.

---

## Verification Workflow

The usual development loop is:

```sh
make -B
git diff --check
wc -c kernel.bin
```

The kernel must stay below the bootloader load limit:

```text
27648 bytes
```

The project now builds C code with `-Os` to keep the kernel inside that window.
This avoided expanding the bootloader load area while filesystem code was still
being stabilized.

QEMU is used for behavioral verification. Important smoke tests so far:

```text
ls
cat README.TXT
cat ABOUT.TXT
touch TEST.TXT
write TEST.TXT hello
cat TEST.TXT
write TEST.TXT bye
cat TEST.TXT
append TEST.TXT !!!
cat TEST.TXT
append NEAR.TXT zz
ls
cat NEAR.TXT
append NEAR2.TXT zz
ls
cat NEAR2.TXT
truncate NEAR2.TXT
ls
write NEAR2.TXT reset
cat NEAR2.TXT
write GONE.TXT bye
ls
delete GONE.TXT
ls
cat GONE.TXT
write OLD.TXT hello
ls
rename OLD.TXT NEW.TXT
ls
cat NEW.TXT
cat OLD.TXT
```

---

## Files Touched During Phase 4

- `fs/fat32.c`: FAT32 mount, list, read, create, write, append, FAT helpers.
- `include/fs.h`: public filesystem API.
- `drivers/ata.c`: ATA sector read and write.
- `include/ata.h`: ATA public API.
- `drivers/term_window.c`: shell commands and chunked `cat`.
- `scripts/make_fat32_image.py`: generated FAT32 image and test files.
- `Makefile`: size-optimized build flags.
- `PHASES.md`: phase checklist.

---

## Next Likely Step

Phase 4 is complete. The next roadmap phase is Phase 5: multitasking.

The first safe Phase 5 slice is likely a kernel-side process/task table with a
single registered kernel task, followed by a round-robin scheduler that can
switch between kernel tasks before attempting ring 3 userland.
