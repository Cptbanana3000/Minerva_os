# MinervaOS

MinervaOS is a from-scratch 32-bit x86 hobby operating system. It boots with a
custom BIOS boot sector, enters protected mode, runs a small graphical desktop,
and now includes FAT32 storage, preemptive kernel-task scheduling, ring-3
groundwork, applications, e1000 networking, a tiny HTTPS-capable browser path,
themes, package metadata, and an app registry seed.

It is not Linux-based. The goal is to grow this into a fully open-source OS with
modern graphics, applications, developer tooling, and eventually real
third-party software support.

## Current Highlights

- BIOS boot sector loads a protected-mode kernel at `0x8000`.
- VGA mode 13h desktop with windows, taskbar, icons, mouse, and keyboard.
- FAT32 read/write support for root-directory 8.3 files.
- Built-in terminal, editor, BMP viewer, WAV preview player, and browser.
- e1000 network driver with ARP, IPv4, UDP, DNS, TCP, HTTP, and TLS 1.2 HTTPS
  smoke path for `example.com`.
- Runtime themes through `theme`, `theme list`, `theme next`.
- Package metadata through `pkg list/info/install/status/remove`.
- App registry through `app list/info/run/add/remove/check`.

See [PHASES.md](PHASES.md) for the roadmap and [PROGRESS.md](PROGRESS.md) for
the running build log.

## Build

Install the toolchain on Ubuntu or WSL Ubuntu:

```bash
sudo apt update
sudo apt install -y nasm gcc make qemu-system-x86
```

Build the OS image and FAT32 disk image:

```bash
make
```

Run in QEMU:

```bash
make run
```

Run the headless boot smoke:

```bash
make smoke
```

Useful terminal commands inside MinervaOS:

```text
help
ls
cat README.TXT
theme list
pkg list
app list
browser https;//example.com/
sdk
```

## Development Docs

- [Architecture Overview](docs/ARCHITECTURE.md)
- [Contributing](docs/CONTRIBUTING.md)
- [Release Checklist](docs/RELEASE_CHECKLIST.md)
- [Roadmap Guide](docs/ROADMAP.md)
- [VBE / Linear Framebuffer Plan](docs/VBE_PLAN.md)
- [License Decision](docs/LICENSE_DECISION.md)
- [Devlog](docs/DEVLOG.md)
- [Changelog](CHANGELOG.md)
- [Security Policy](SECURITY.md)
- [Support](SUPPORT.md)

## Notes

The project is still an early OS-dev system. Expect sharp edges, QEMU-first
hardware assumptions, 320x200 graphics, 8.3 FAT32 filenames, and many temporary
diagnostic commands. Those constraints are intentional stepping stones.

## License

MinervaOS is licensed under the [MIT License](LICENSE).
