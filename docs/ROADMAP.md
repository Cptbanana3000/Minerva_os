# Roadmap Guide

The canonical checklist lives in [PHASES.md](../PHASES.md). This file explains
how to read it.

## Near-Term Phases

Phases 0 through 9 track the current QEMU-first MinervaOS:

- boot and protected mode
- VGA desktop
- FAT32 storage
- multitasking and ring-3 groundwork
- built-in apps
- networking
- browser and HTTPS experiments
- polish, package metadata, app registry, and open-source prep

These phases are intentionally concrete and testable. Most milestones should
have a terminal command, app action, or QEMU smoke test.

## Long-Term Phases

Phases 10 and beyond describe the full OS ambition:

- high-resolution graphics and crisp icons
- broader hardware support
- real userland executables
- POSIX-like APIs
- filesystem evolution
- stronger networking
- application platform and browser growth
- security, multi-user support, distribution, and installer work

These are direction-setting phases, not promises that all work is immediate.

## Current Rule Of Thumb

When choosing the next milestone:

- Prefer small slices that boot and can be tested.
- Preserve existing diagnostics.
- Update `PHASES.md` when a checkbox changes.
- Update `PROGRESS.md` when a meaningful capability lands.
- Keep kernel size and memory-layout guards visible.

## Current Transition

Phase 9 open-source release prep is now mostly complete: docs, templates,
license, support/security notes, changelog, devlog, and `make smoke` exist.
The remaining Phase 9 work is larger ecosystem work such as true third-party
app bundles and the eventual public GitHub release.

Phase 10 has started with the VBE / linear framebuffer plan. The first real
engineering goal is not Full HD immediately; it is safely detecting and entering
a higher-resolution true-color mode while keeping the current VGA path as a
fallback.
