# Security Policy

MinervaOS is an early hobby operating system. It is not hardened, audited, or
safe for sensitive workloads.

## Supported Versions

There are no stable supported releases yet. Security reports should target the
current development branch or the most recent public snapshot once releases
exist.

## Reporting A Vulnerability

Until a private security contact is published, open a GitHub issue only for
non-sensitive security hardening work. For serious vulnerabilities, wait for the
project owner to publish a private reporting address before sharing exploit
details publicly.

## Current Security Limits

- QEMU-first hardware assumptions.
- Early ring-3 and syscall groundwork, not a complete isolation model.
- No multi-user permission model yet.
- No production-grade browser sandbox.
- TLS/browser code is educational and narrow, not a general-purpose web stack.
