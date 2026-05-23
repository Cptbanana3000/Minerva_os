# VBE / Linear Framebuffer Plan

Phase 10 starts the move from 320x200 VGA mode 13h toward crisp modern
graphics. The first target is VESA BIOS Extensions (VBE) with a linear
framebuffer.

## Goal

Boot MinervaOS with enough video-mode information to support higher-resolution
true-color drawing without depending on VGA mode 13h forever.

## First Engineering Slices

1. Collect VBE controller information in real mode before protected mode.
2. Enumerate candidate video modes.
3. Prefer simple true-color linear-framebuffer modes, starting with 800x600 or
   1024x768 before attempting Full HD.
4. Store selected mode info in a small boot-info structure below 1 MiB.
5. Pass that boot-info pointer to the protected-mode kernel.
6. Add a graphics backend abstraction so mode 13h and LFB drawing can coexist
   during the transition.
7. Add a diagnostic command that prints detected resolution, bpp, pitch, and
   framebuffer address.

## Bootloader Constraint

The current boot sector is a single 512-byte BIOS sector. VBE probing and mode
selection may not fit there cleanly. The likely path is a small second-stage
loader or a deliberately tiny real-mode probe before the protected-mode jump.

## Initial Mode Policy

Start conservative:

- 800x600x32 if available.
- 1024x768x32 if the first target works.
- 1280x720 and 1920x1080 later.

The early UI can still scale from the old 320x200 coordinate system while the
graphics primitives are generalized.

## Risks

- BIOS/VBE behavior differs between emulators and hardware.
- Linear framebuffer physical addresses must be mapped before use once paging
  is active.
- Higher resolutions increase redraw cost, so dirty rectangles become more
  important soon after the first LFB mode works.
