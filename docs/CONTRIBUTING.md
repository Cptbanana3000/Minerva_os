# Contributing

MinervaOS is growing in small, testable slices. Keep changes narrow, readable,
and aligned with the existing C/ASM style.

## Workflow

1. Read [PHASES.md](../PHASES.md) and [PROGRESS.md](../PROGRESS.md).
2. Make one focused change.
3. Build from scratch:

```bash
make -B
```

4. Check whitespace:

```bash
git diff --check
```

5. Run a QEMU smoke test when code or image generation changes.

```bash
make smoke
```

## Code Style

- Prefer simple C and explicit fixed-size buffers.
- Keep diagnostics compact enough for the small terminal window.
- Avoid hidden heap use in interrupt paths and early boot paths.
- Prefer adding a visible terminal command for new low-level subsystems.
- Update `PHASES.md` and `PROGRESS.md` when a roadmap item changes.

## Safety Rules

- Do not remove existing diagnostics without replacing their observability.
- Watch `kernel.bin` size and `__kernel_end` guard output from `make`.
- Keep bootloader changes conservative; `boot.bin` must remain 512 bytes.
- Treat QEMU success as a smoke test, not proof of broad hardware support.

## Manual Smoke

`make smoke` runs a headless QEMU boot and checks for serial lines similar to:

```text
MinervaOS booting...
e1000 Ethernet device found
e1000 MAC address read
e1000 RX ring ready
e1000 TX ring ready
FAT32 filesystem mounted
```
