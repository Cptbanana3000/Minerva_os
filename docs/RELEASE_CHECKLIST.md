# Release Checklist

Use this before publishing a public build or milestone snapshot.

## Build

- [ ] `make -B` passes.
- [ ] `git diff --check` passes.
- [ ] `boot.bin` is exactly 512 bytes.
- [ ] `kernel.bin` is below the Makefile bootloader load-window guard.
- [ ] `__kernel_end` is below the bootstrap stack guard.
- [ ] `fs.img` is regenerated from `scripts/make_fat32_image.py`.
- [ ] `make smoke` passes.

## Smoke Tests

- [ ] QEMU boots to desktop in an interactive `make run` check.
- [ ] Terminal opens and `help` prints.
- [ ] `ls`, `cat README.TXT`, and `cat SDK.TXT` work.
- [ ] `theme list`, `theme next`, and `theme classic` work.
- [ ] `pkg list`, `pkg install THEME`, `pkg status`, and `pkg remove THEME`
      work.
- [ ] `app list`, `app check`, and `app run EDIT` work.
- [ ] Browser opens `https;//example.com/`.

## Documentation

- [ ] `README.md` matches the current phase.
- [ ] `PHASES.md` roadmap checkboxes are current.
- [ ] `PROGRESS.md` includes the latest milestone notes.
- [ ] Architecture and contribution docs are still accurate.

## Open Source Prep

- [x] License is chosen by the project owner.
- [x] `LICENSE` exists.
- [ ] Repository description and screenshots are current.
- [ ] Known limitations are documented clearly.
- [ ] First public issue labels/milestones are prepared.
- [ ] Public repository/release is published by the project owner.
