# MinervaOS Progress Log

This file records how MinervaOS is growing. `PHASES.md` is the checklist; this
log explains the path, the safety rules, and the tests used along the way.

The roadmap now has two layers: near-term phases for the current QEMU hobby OS
and a long-term full-OS roadmap. The long-term section explicitly tracks goals
such as Full HD graphics, true-color compositing, crisp icons, hardware support,
real userland executables, POSIX-like APIs, security, packaging, distribution,
and open-source project maturity.

---

## Current Shape

MinervaOS is a 32-bit x86 hobby OS that boots through a custom real-mode
bootloader, switches to protected mode, enters VGA mode 13h, and runs a small
graphical desktop with windows, icons, a taskbar, mouse input, keyboard input,
serial logging, a heap allocator, paging, and a terminal window.

Phase 4 is complete: persistent FAT32 storage supports list, read, create,
write, append, truncate, delete, and rename for root-directory 8.3 files.

Phase 5 is complete: the kernel has scheduler-owned kernel tasks, a process
table, preemptive round-robin switching for kernel tasks, GDT/TSS-backed ring-3
entry, syscall return to kernel, scheduler-owned user launches, and a recovered
user-mode page-fault proof that demonstrates privilege separation.

Phase 6 has begun with a graphical text editor skeleton: the desktop now has an
`Edit` icon, the editor opens as a window, accepts focused keyboard input, keeps
an in-memory text buffer, renders a cursor, and marks the buffer dirty when
edited.

The editor now opens and saves a fixed root-directory file, `NOTE.TXT`. On
open, it loads existing file contents if present or starts as a new empty note.
`Esc` saves through `fs_write(... FS_WRITE_CREATE | FS_WRITE_TRUNCATE)`, the
status label reports load/save state, and the dirty marker clears after a
successful save. This keeps the first application file workflow narrow while
exercising the Phase 4 write API from a graphical app.

The terminal can now launch the editor on any root-directory 8.3 filename with
`edit NAME`. The editor window is reused if it is already open, the selected
filename is shown in the status row, and `Esc` saves back to that same file.
The desktop `Edit` icon remains a convenient shortcut for `NOTE.TXT`.

The first `edit NAME` implementation exposed an app-lifecycle bug: the terminal
could create the editor singleton, but the desktop loop still only routed render
and keyboard events through the pointer set by the desktop icon path. The editor
module now exposes `text_editor_active()`, so windows opened from either the
terminal or the icon are rendered and receive focused keyboard input correctly.

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
131072 bytes
```

The kernel originally loaded at `0x1000`, which capped it at 27648 bytes before
it would collide with the boot sector area. Before attempting preemptive
scheduling, the bootloader and linker were moved to load/link the kernel at
`0x8000`, giving a conservative 128 KiB kernel window below the `0x90000` stack.

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

## Phase 5 Multitasking Growth

Phase 5 grew from a non-invasive scheduler skeleton into protected userland:

- `scheduler_init()` sets up an in-kernel task table.
- `scheduler_create_kernel_task()` registers kernel tasks.
- The PIT calls `scheduler_tick()` at 100 Hz.
- The main loop calls `scheduler_poll()` to perform cooperative round-robin
  dispatch.
- Each registered kernel task now has its own small kernel stack.
- `scheduler_context_switch()` saves/restores ESP and callee-saved registers.
- Tasks run one step on their own stack, then yield back to the kernel loop.
- The terminal `tasks` command lists task IDs, names, run counts, and scheduler
  switch count.
- `tasks` also shows timer schedule requests, proving IRQ0 is driving the
  scheduler decision point before true interrupt-frame switching is attempted.
- The interrupt frame layout is now public in `interrupts.h`, and IRQ0 frames
  are sampled by the scheduler so the future preemptive switch code can be
  built against the real saved-register shape.
- `interrupt_handler()` now returns a possible replacement ESP, and the common
  ISR stub will restore through that ESP if it is nonzero. For now all paths
  return `0`, so this is plumbing only and does not switch tasks inside IRQ0.
- IRQ0 now calls `scheduler_on_timer_interrupt(frame)`, which is the future
  preemptive scheduling decision point. It currently records the frame and
  returns `0`, preserving behavior.
- Each demo task now also has a manufactured interrupt-frame context with
  kernel code/data segments and `eflags=0x202`. These frames are not restored
  from IRQ0 yet, but they match the common ISR epilogue shape.
- The timer interrupt path can now choose a next task candidate when a quantum
  expires, and the terminal `tasks` command shows this as `Cand`. The
  `SCHED_PREEMPTIVE_ENABLED` gate is still `0`, so IRQ0 records candidates but
  deliberately returns `0` instead of switching stacks.
- A runtime `preempt on` / `preempt off` gate was added for controlled testing.
  The IRQ0 scheduler path now refuses to switch when the interrupted code is
  not already running on a task stack, and `tasks` reports those refusals as
  `Block`. This protects the desktop/main loop until it becomes a real
  schedulable context.
- The desktop/main loop now owns an explicit scheduler slot, registered via
  `scheduler_register_main_task("desktop")` just before the event loop. The
  slot has no entry function, no kernel stack, and no IRQ frame, so it is
  invisible to both the cooperative switch path and the IRQ0 preemption
  candidate picker. It appears in `tasks` purely as bookkeeping. `scheduler_poll`
  was reworked into a bounded skip-loop so the entry-less main slot is never a
  context-switch target.
- A separate kernel process table now exists as the first non-invasive process
  model slice. Existing scheduler slots for `task-a`, `task-b`, and `desktop`
  are registered as kernel processes with PIDs, parent PIDs, linked task IDs,
  names, and states. The terminal `ps` command lists those records while the
  scheduler behavior remains unchanged.
- `ps` now derives the displayed `run` state from the scheduler's effective
  running slot. When the desktop loop is active outside a kernel task, the
  desktop process is shown as running; when IRQ preemption is inside a demo
  task, that task is shown as running.
- Scheduler CPU state is now grouped into an explicit per-task context object:
  cooperative ESP, IRQ-frame ESP, synthetic resume IRQ-frame ESP, and stack
  bounds live together instead of as loose task fields. The desktop main loop's
  cooperative ESP now lives in its scheduler context too, replacing the old
  standalone `main_esp`. The terminal `ctx` command lists compact context
  pointers (`ID ESP IRQ`) for smoke testing. This completes the kernel-task
  context-switching checklist item; ring 3/TSS/user privilege work remains
  separate.
- User-mode groundwork has begun without jumping to ring 3 yet. The kernel now
  installs its own GDT after entering C, preserving kernel code/data selectors
  while adding ring-3 code/data descriptors and a 32-bit TSS descriptor. `ltr`
  loads the TSS with `ss0=0x10` and a dedicated kernel `esp0`, ready for future
  privilege transitions. The terminal `tss` command reports user selectors,
  TSS selector, loaded state, `ss0`, and `esp0`.
- Syscall interrupt plumbing is now present before any ring-3 jump. Vector
  `0x80` has its own ISR stub and is installed as a DPL 3 IDT gate, so future
  user code can legally call it. The handler currently records a syscall count
  and the incoming EAX value, then returns the count in EAX. The terminal
  `syscall` command shows the counters, and `syscall test` invokes `int 0x80`
  from kernel mode as a safe first proof.
- A controlled ring-3 entry test now exists behind the terminal `usertest`
  command. The helper saves the current kernel ESP, builds an iret frame with
  `CS=0x1B` and `SS=0x23`, enters a tiny user stub, and the stub calls
  `int 0x80` with EAX=`0x55534552` (`USER`). The syscall handler records the
  user CS/EAX, rewrites the saved interrupt frame to a kernel landing pad,
  and returns to the original terminal stack. This proves the ring transition
  and syscall path without abandoning the desktop.
- The first `usertest` attempt exposed the expected paging boundary: the
  identity map was supervisor-only, so ring 3 faulted as soon as it tried to
  fetch the test stub. The test code now lives in a page-aligned `.usertext`
  section and the test stack is page-aligned too; `usermode_init()` maps only
  those pages with `PAGE_USER`, leaving the rest of the kernel supervisor-only.
- The process table now distinguishes kernel and user processes. `ps` shows a
  compact `ker` / `usr` kind column, and a successful `usertest` upserts one
  `usertest` user-process record with no scheduler task ID, the user entry
  address, user stack top, syscall count, and a completed/zombie state. The
  terminal `uproc` command prints the user-process metadata without making
  ring-3 code schedulable yet.
- User-process metadata is now grouped as an explicit user context:
  EIP, ESP, CS, SS, EFLAGS, last syscall EAX/CS, and syscall count. `usertest`
  fills this context after returning through the syscall path. The terminal
  `uctx` command prints the saved user interrupt-frame shape that future
  schedulable user tasks will restore.
- User contexts can now be prepared and run as process-table state. `userprep`
  creates a READY `usertest` user process without entering ring 3. `userrun`
  loads that saved context, enters ring 3 through the generic context runner,
  returns through `int 0x80`, and marks the user process ZOMBIE afterward.
  This still avoids scheduler integration, but the process table is now the
  source of truth for launching the ring-3 test.
- User launch is now scheduler-owned without being timer-preempted directly.
  A `user-sched` kernel task sits in the normal scheduler task table and stays
  idle until armed. The terminal `usersched` command arms it; on its next
  scheduler turn it finds a READY user process, marks it RUNNING, enters ring 3
  through the saved context, returns through the syscall landing pad, and marks
  the process ZOMBIE. The launcher temporarily lowers the IRQ preemption gate
  while it is inside the ring-3 test, so this does not yet imply timer-preempted
  userland. `usched` reports the launcher task's armed/runs/launch counters
  plus the last PID/result, so this path can be tested independently from the
  older direct `userrun` command.
- The scheduler-owned user launch is now restartable and easier to diagnose.
  `userreset` rebuilds the saved `usertest` ring-3 context and marks it READY
  again after a completed run. `usersched` refuses cleanly when no READY user
  process exists, and `usched` now reports `NoReady` alongside idle and launch
  counters so failed arm attempts are visible without inspecting code.
- User-mode fault recovery now proves the first hard privilege boundary. A new
  ring-3 `userfault` stub deliberately reads from supervisor-only kernel memory
  at `0x00008000`. The page-fault handler recognizes `#PF` from CPL 3, captures
  the fault vector, faulting EIP, CR2 address, error code, and user CS, rewrites
  the saved frame to a kernel landing pad, and returns to the terminal instead
  of using the fatal crash screen. The `userfault` command runs this proof and
  records a `userfault` process as ZOMBIE with fault metadata; `ufault` prints
  the last captured user fault directly.
- IRQ0 now captures the main loop's interrupt frame into a dedicated
  `main_captured_esp` static (separate from `tasks[main].irq_esp`) and bumps a
  `main_capture_count` counter, each time a timer tick fires while the main loop
  is the interrupted context. The capture is deliberately stored OUTSIDE the
  task table so the main slot remains invisible to `scheduler_next_active_irq_task`
  — until the return-to-main path exists, promoting the capture into the task
  slot would let a preempted task be "restored" to a stale main frame with no
  way to land safely. The terminal `tasks` view shows the capture as `MainCap`,
  which climbs in lockstep with `Block` whenever `preempt on` is set, proving
  the timer path correctly identifies the main loop and saves its frame.
- The desktop event loop was moved off the bootloader stack onto a dedicated
  8 KiB kernel stack (`main_stack[2048]`, 16-byte aligned). A tiny asm helper
  `scheduler_run_on_main_stack(entry, new_esp)` swaps ESP and `call`s the
  extracted `desktop_main_loop()` function; if that ever returns the helper
  halts. `kernel_main` now ends with that call and the boot stack is abandoned
  past that point. This is purely foundational — no observable behavior
  change — but it is the prerequisite for safe return-to-main, because
  captured main frames will now sit on a stack region that nothing else writes
  to while main is paused. Verified in QEMU: desktop, terminal, window drag,
  and the `tasks` / `preempt on` counters all behave identically.

The preemptive round-robin slice is now working behind runtime gates. When
`preempt on` and `mainsw on` are enabled, IRQ0 can atomically capture the
desktop main loop's interrupt frame, switch to a kernel task inside the same
ISR return path, and later return safely to the paused main loop. The terminal
`tasks` command reports the round-trip counters:

```text
M->T
Y->M
```

Manual QEMU verification showed both counters advancing in lockstep while
`task-a` and `task-b` run counts increased, with no crash screen.

---

## Phase 5 Bug Fixed — Atomic Main↔Task Round-Trip Crash

The atomic capture-and-switch path was implemented, gated behind a new
`mainsw on` / `mainsw off` shell command (default off). The first version
deterministically triggered a General Protection Fault within a tick or two.
That crash is fixed; this section records the failure and the repair.

### What was added

- `kernel/switch.asm`: `scheduler_context_switch_iret(old_esp, new_esp)` — same
  cooperative-save semantics as `scheduler_context_switch` for the yielding
  side, but `new_esp` must point at a valid `interrupt_frame_t`; the routine
  executes the ISR epilogue inline (`popa; pop ds/es/fs/gs; add esp,8; iretd`)
  to land in whatever context that frame represents.
- `kernel/switch.asm`: `scheduler_resume_from_saved_esp` and update variants
  of the cooperative and iret switch helpers. These keep each task's synthetic
  IRQ resume frame pointed at the latest saved cooperative ESP.
- `kernel/scheduler.c`:
  - `main_switch_enabled` runtime gate (`mainsw on` / `mainsw off`); default 0.
  - `main_paused_via_irq` flag tracking whether main was IRQ-paused vs.
    cooperatively-paused.
  - `resume_irq_esp` per task, backed by the existing `irq_stacks[]` storage.
    Synthetic IRQ frames now enter `scheduler_resume_from_saved_esp` instead of
    entering the C trampoline directly on the small IRQ frame tail.
  - `scheduler_next_active_irq_task` widened to consider any active slot with
    a non-zero `irq_esp` (including the main slot once it is paused-via-IRQ),
    and tightened to skip the current slot to avoid self-pick.
  - `scheduler_on_timer_interrupt` split into two branches: the `!in_task`
    branch atomically captures main's frame into `tasks[main].irq_esp` and
    switches to a chosen task; the in-task branch can now ferry execution back
    to main when the picker chooses the main slot.
  - `scheduler_yield` gained a dual path: when `main_paused_via_irq` is set,
    it calls the new iret helper to restore main's saved frame instead of the
    cooperative `main_esp`.
  - `cli` was added around the bookkeeping in the iret yield path to close
    a race where a PIT tick mid-bookkeeping would see `in_task = 0` and
    mis-classify the task stack as the main loop.

### Failure mode

With `preempt on` alone the system stays alive (the `mainsw` gate keeps the
new path inert — main's `irq_esp` stays 0, so the picker never sees it and
the iret yield branch's condition never fires).

With `mainsw on` added, the kernel faults within ~10 ms. The fault renders
through the crash handler in `interrupts/interrupts.c`, which paints the
framebuffer red and draws bit-bars for the interrupt number and the saved
EIP / err_code (later: CS, DS, and a `sched_debug_marker` global).

The interrupt number bars decode to **interrupt 13 (General Protection
Fault)** — bits 0, 2, 3.

### Methodology

Several debugging affordances were added because the host environment has no
serial console wired into the QEMU `run` target:

1. **Runtime gate (`mainsw`)** — splits "preemption enabled" from "atomic
   main↔task switching enabled" so the regressions are bisected from the
   prior known-good behavior.
2. **Bit-bar crash diagnostic** — the fallback `interrupt_handler` paints the
   `int_no`, `eip`, `err_code`, `cs`, and `ds` as colored bars across the
   framebuffer (bit 0 leftmost, 10 px spacing). EIP bits decode against the
   `kernel.elf` symbol table to locate the faulting instruction. The build
   was also extended to produce `kernel.elf` alongside `kernel.bin` so `nm`
   and `objdump` can be used for this mapping.
3. **`sched_debug_marker`** — a 32-bit global written at every meaningful
   checkpoint in the scheduler:
   - `0x11110000 | next`  M→T atomic switch in IRQ handler
   - `0x22220000 | from`  I→M (task→main) via IRQ picker
   - `0x33330000 | from<<8 | next`  task→task IRQ switch
   - `0x44440000 | old`   yield's iret-to-main path
   - `0x55550000`          first instruction of `scheduler_task_trampoline`
   - `0x66660000 | id`    just before calling the task's entry
   - `0x77770000 | id`    just after the entry returned
4. **Bisection by gate** — confirmed `preempt on` alone (no `mainsw`) keeps
   the marker chain unchanged from prior behavior, so the regression is
   strictly within the new atomic-switch / iret-yield code.

### Root Cause

The crash screen consistently shows the marker stuck at `0x11110000` with
`next = 0` (task-a chosen by the round-robin picker, given that
`current_task` was 1 when PIT fired). That means:

- The M→T branch in `scheduler_on_timer_interrupt` ran, wrote the marker,
  set `tasks[main].irq_esp`, set `main_paused_via_irq`, and returned
  `tasks[next].irq_esp` to the ISR stub.
- The ISR stub's `mov esp, eax; popa; pop ds/es/fs/gs; add esp,8; iretd`
  chain either failed mid-stream or succeeded but trampoline's very first
  marker write (`movl $0x55550000, sched_debug_marker` at the head of
  `scheduler_task_trampoline`) faulted before completing — because we never
  see `0x55550000` in the marker even though the next checkpoint after
  M→T is exactly that trampoline write.

The EIP bars (10 bits, pattern `cluster_of_3 + cluster_of_2 + single +
cluster_of_3 + single`) match `0xbac7` — which is the trampoline's first
marker write instruction in the current build. This places the fault on a
write through DS to a normal kernel-BSS address (`0x1f120`).

The synthetic task IRQ frame was valid only once. After a task returned to the
IRQ-paused main loop, the task's `irq_esp` still pointed at that consumed frame.
On the next main→task switch, the ISR epilogue restored from stale stack
contents, so segment pops could load garbage before landing near the trampoline.

The fix gives each task a persistent synthetic resume frame whose EAX slot is
refreshed every time the task yields. The synthetic frame now irets into
`scheduler_resume_from_saved_esp`, which loads the saved cooperative ESP and
returns through the normal task stack. Real IRQ preemption frames can still be
stored in `tasks[id].irq_esp` when a task is interrupted mid-run.

### Known related correctness gaps (pre-existing, not introduced)

- The cooperative `scheduler_poll` cooperative switch has an in-task
  window between `in_task = 1` and the actual stack swap inside
  `scheduler_context_switch`. With `preempt on`, a PIT tick in that
  window can stash a mid-switch frame into `tasks[X].irq_esp`. Latent;
  not yet a confirmed crash source.

### Verification

Verification performed after the fix:

```text
make -B
git diff --check
wc -c kernel.bin
```

A temporary test build also forced `preemptive_enabled = 1` and
`main_switch_enabled = 1` during `scheduler_init()`, then booted in headless
QEMU for 5 seconds with interrupt logging enabled. The serial log reached
`MinervaOS booting...` and `FAT32 filesystem mounted`, and the QEMU interrupt
log contained no `v=0d`, `v=0e`, `check_exception`, or triple-fault entries.

Manual QEMU smoke test:

```text
preempt on
mainsw on
tasks
```

Observed result:

```text
M->T:45
Y->M:45
MSw:1
0 task-a 152
1 task-b 100
2 desktop 0
```

This confirms the main↔task IRQ round-trip is stable enough to mark the
preemptive round-robin scheduler checklist item complete. The gates remain in
place for controlled testing while the next Phase 5 pieces are built.

## Phase 5 Completion

Phase 5 is now complete. The final userland smoke tests are:

```text
userprep
usersched
usched
ps
uctx
userreset
usersched
usched
userfault
ufault
```

Expected results:

- `usersched` launches a READY `usertest` through the `user-sched` kernel task.
- `ps` shows `usertest` returning to `zombie` after launch.
- `uctx` shows the ring-3 context with user selectors (`CS=0x1B`, `SS=0x23`).
- `userfault` returns to the terminal instead of the fatal crash screen.
- `ufault` reports vector 14, user CS `0x1B`, CR2 address `0x00008000`, and a
  page-fault error code indicating a user-mode access to a present supervisor
  page.

This proves the Phase 5 boundary: user code can run in ring 3 and call into the
kernel through `int 0x80`, while supervisor-only kernel pages remain protected
and recoverable user faults are captured as process metadata.

## Phase 6 Applications Growth

Phase 6 started with the first graphical application beyond terminal/about:

- `drivers/text_editor.c` and `include/text_editor.h` define a small editor app.
- The desktop has an `Edit` icon that opens an `Editor` window.
- Focused keyboard input is routed to the editor when its window is on top.
- The editor supports printable characters, Enter, and Backspace.
- The text buffer is in-memory only for now, with a simple cursor and dirty
  marker rendered in the window.
- `NOTE.TXT` is loaded when the editor opens and saved with `Esc`.
- `edit NAME` opens or creates a chosen root-directory file in the graphical
  editor.

Smoke test:

```text
make run
```

Open the `Edit` icon, type text, use Enter and Backspace, press `Esc` to save,
close/reopen the editor, and confirm the note reloads. Then focus the terminal
again to confirm keyboard routing follows the active window.

Additional smoke test:

```text
edit TODO.TXT
```

Type text in the editor, press `Esc`, then return to the terminal and run:

```text
cat TODO.TXT
```

### Image Viewer Skeleton

The image viewer app shell is now present:

- `drivers/image_viewer.c` and `include/image_viewer.h` define the viewer app.
- The desktop has a `View` icon that opens an `Image` window for `IMAGE.BMP`.
- The terminal command `view NAME` opens the viewer on any root-directory 8.3
  filename.
- The viewer reuses a single active window across icon and terminal launches,
  matching the editor lifecycle pattern.
- The current slice probes file existence and size, displays filename/status,
  and renders a placeholder preview area; actual BMP decoding remains the next
  image-viewer step.

Smoke test:

```text
view TEST.BMP
view IMAGE.BMP
```

Expected result: the `Image` window opens, comes to front, and reports either
`MISSING` or `READY` with a byte count depending on whether the file exists.

### BMP Rendering

The image viewer now has a first real decode path:

- `scripts/make_fat32_image.py` generates a tiny `TEST.BMP` into `fs.img`.
- The BMP is 16x12, uncompressed, 24-bit RGB, and fits the current 1 KiB
  one-buffer viewer read path.
- `drivers/image_viewer.c` validates the BMP header, checks dimensions and
  compression, converts BGR pixels through `graphics_rgb()`, and scales the
  image into the preview area.
- Unsupported files still show status text such as `NOT BMP`, `UNSUP`,
  `TOO BIG`, or `MISSING`.

Smoke test:

```text
view TEST.BMP
```

Expected result: the image viewer reports `BMP24` and displays the generated
color test bitmap instead of the placeholder.

The first smoke attempt reported `TOO BIG` because the generated `TEST.BMP`
is 630 bytes and the viewer buffer was still 512 bytes. The viewer buffer is
now 1 KiB, which keeps the decoder simple while allowing the generated test
asset to load fully.

### Audio Player Metadata

The audio player app shell is now present:

- `scripts/make_fat32_image.py` generates `AUDIO.WAV`, a tiny 8-bit mono PCM
  test file.
- `drivers/audio_player.c` and `include/audio_player.h` define the audio app.
- The desktop has an `Aud` icon that opens an `Audio` window for `AUDIO.WAV`.
- The terminal command `play NAME` opens the audio app on any root-directory
  8.3 filename.
- The WAV parser validates RIFF/WAVE, finds `fmt ` and `data` chunks, and
  reports PCM metadata: sample rate, channels, bit depth, and data bytes.

Smoke test:

```text
play AUDIO.WAV
play TEST.BMP
```

Expected result: `AUDIO.WAV` reports `PCM`, `rate 8000`, `ch 1`, `bits 8`,
and a data size. Non-WAV files report `NOT WAV` or another validation status.
Actual audio output remains a future slice after choosing a playback device
path such as PC speaker, Sound Blaster, or another emulated audio target.

### PC Speaker WAV Preview

The first playback path now exists through the PC speaker:

- `drivers/speaker.c` and `include/speaker.h` drive PIT channel 2 and port
  `0x61`.
- `play NAME` still opens the audio app, but now also attempts a short playback
  preview when the file is 8-bit mono PCM WAV.
- The preview maps WAV sample amplitudes to short PC-speaker square-wave tones.
  This is intentionally crude, but it proves the kernel can parse audio data
  and drive a real audio output path.
- Unsupported WAV formats keep the metadata path but report `NO PLAY`.

Smoke test:

```text
play AUDIO.WAV
```

Expected result: the audio window status changes to `PLAYED`. Depending on QEMU
audio backend settings, the PC-speaker tones may or may not be audible on the
host, but the OS-side playback path runs.

## Phase 6 Completion

Phase 6 is now complete. MinervaOS has a window-based application layer with:

- Terminal app with shell commands and filesystem/process/networking diagnostics.
- Text editor that opens, edits, and saves root-directory 8.3 files.
- Image viewer that opens files and renders uncompressed 24-bit BMP images.
- Audio player that parses WAV metadata and runs a PC-speaker playback preview
  for 8-bit mono PCM WAV files.

The next phase is Phase 7 networking, starting with PCI/e1000 discovery before
packet receive/transmit.

## Phase 7 Start: PCI and e1000 Discovery

MinervaOS now has the first networking foundation layer:

- `include/io.h` supports 32-bit port I/O through `inl()` and `outl()`.
- `drivers/pci.c` can read PCI config space and scan bus/slot/function entries.
- `drivers/e1000.c` finds Intel e1000-class Ethernet devices exposed by QEMU.
- `make run` now adds QEMU user networking with an e1000 adapter.

Terminal command:

```text
net
```

Expected result with `make run`: `e1000:yes`, followed by the PCI bus/device/
function, vendor ID, device ID, and BAR0. This proves the OS can discover the
virtual network card before we start touching the e1000 MMIO registers.

## Phase 7: e1000 MMIO and MAC Address

The e1000 path now enables PCI memory space/bus mastering, maps the device BAR0
MMIO window into the current page tables, and reads receive address register 0
from the e1000 register block.

Terminal command:

```text
net
```

Expected result: the network diagnostics now include the BAR type and MAC
address. With QEMU's e1000 device this should show `TYPE:MMIO` and a six-byte
MAC address, proving MinervaOS can safely touch the NIC's register space.

## Phase 7: e1000 Transmit Ring

The e1000 driver now initializes a small transmit descriptor ring backed by
kernel-resident packet buffers, enables the e1000 transmit engine, and exposes a
manual raw-frame transmit smoke test.

Terminal command:

```text
net tx
```

Expected result: `TX sent`, with the attempt/sent/error counters updated. The
test packet is a padded 60-byte Ethernet broadcast frame using ethertype
`0x88B5` and the NIC's own MAC address as the source. This does not require an
Ethernet cable; QEMU carries the virtual NIC traffic through the host network.

## Phase 7: e1000 Receive Ring

The e1000 driver now initializes a receive descriptor ring with 2 KiB packet
buffers, enables broadcast/unicast/multicast receive filtering for early
diagnostics, and exposes a manual receive poll command.

Terminal command:

```text
net rx
```

Expected result when no traffic is waiting: `RX none`, with `RX:ready` still
visible in `net`. When QEMU delivers a packet, the command reports the packet
counter, length, ethertype, descriptor status, and descriptor error byte.

## Phase 7: Packet API and ARP Probe

The e1000 driver now exposes generic packet send/receive helpers, and a small
network layer sits above it with MinervaOS' first Ethernet/ARP behavior.

Current static QEMU user-network identity:

```text
MinervaOS IP: 10.0.2.15
Gateway IP:   10.0.2.2
```

Terminal command:

```text
net arp
```

Expected result with QEMU user networking: `ARP ok`, request/reply counters,
and the gateway MAC address. The plain `net` command now also prints the static
IP, gateway IP, and cached ARP MAC if one has been learned.

## Phase 7: IPv4, UDP, and DNS Smoke Test

The network layer can now build minimal IPv4 packets, send UDP datagrams, parse
incoming IPv4/UDP frames, and decode a simple DNS A-record response.

Terminal command:

```text
net dns
```

Expected result with QEMU user networking and host internet access: `DNS ok`
and an A record for `example.com`. This command ARPs QEMU's gateway
(`10.0.2.2`) if needed, sends a checksummed UDP DNS query to QEMU's DNS server
(`10.0.2.3`) through that gateway MAC, then polls the receive ring for the DNS
reply. The command also prints the last seen ethertype, UDP ports, and DNS
header ID/flags to make reply parsing failures visible.

## Phase 7: TCP Handshake Smoke Test

MinervaOS now has a minimal TCP packet path for outbound connection setup. It
can build a TCP SYN packet, compute the TCP checksum, send it through QEMU's
gateway MAC to the last DNS-resolved `example.com` address, parse an incoming
SYN-ACK, and send the final ACK.

Terminal command:

```text
net tcp
```

Expected result: `TCP ok`, with SYN/SYN-ACK/ACK counters updated. The SYN uses
common TCP options (MSS, SACK permitted, window scale) and a real TCP checksum.
This proves the TCP state machine can perform the first real connection
handshake before we add payload transfer and an HTTP GET.

The TCP diagnostics now include `Tgt:` so we can tell which endpoint was tested.
There is also a local QEMU probe:

```text
net tcp dns
```

This tries a TCP handshake to QEMU's DNS endpoint at `10.0.2.3:53`, which helps
separate raw TCP correctness from external HTTP reachability.

## Phase 7: HTTP GET Smoke Test

The TCP path can now send application payloads and recognize payloads received
from the server. The first HTTP client smoke test connects to `example.com:80`,
sends a small HTTP/1.0 GET request, polls for TCP response data, parses the
first `HTTP/...` status code, and acknowledges received payload bytes.

Terminal command:

```text
net http
```

Expected result: `HTTP ok`, a nonzero `Code:` such as `200` or `301`, and
nonzero TCP payload receive bytes.

Follow-up fix: TCP connection attempts now rotate the local source port starting
at `49153`, and the terminal diagnostics print `LP:`. This avoids reusing the
same TCP four-tuple across `net tcp`, `net tcp dns`, and `net http` probes.

The DNS parser now keeps up to four A records from a response. `net http` tries
each resolved address until a TCP handshake succeeds, and diagnostics print the
number of `IPs:` plus the selected index `Sel:`.

Payload sends now retry the e1000 TX path briefly while polling RX. `net http`
prints `PErr:` so a failed GET send can be distinguished from a missing HTTP
response.

Follow-up diagnostics: `net http` now prints `Stage:`, `Len:`, and `AckN:`.
The TCP connect path reports success after a matching SYN-ACK and a transmitted
final ACK, which keeps the smoke test moving to payload transfer while still
showing the peer's ACK number. The HTTP status parser was also fixed to read
the three status digits after the space in responses like `HTTP/1.1 200`.

The `net http` terminal output is now compact enough to fit in the small
terminal window. The first diagnostic row shows `S:`, payload send errors
(`P:`), transmit bytes (`Tx:`), and receive bytes (`Rx:`).

HTTP connection setup now defers the third handshake ACK and sends it together
with the GET payload as a `PSH+ACK` segment. The standalone `net tcp` probes
still send a separate final ACK, but HTTP can move directly from SYN-ACK into
request transmission.

TCP diagnostics now only update after a packet matches the active target IP and
local port. Connection attempts also drain stale RX frames before each SYN, wait
longer for external SYN-ACKs, and let `net http` retry the resolved A records
twice before giving up.

The HTTP smoke test now retries the full GET transaction on each resolved A
record instead of stopping at the first successful TCP connect. It also tracks
how many request bytes the peer ACKed (`A:` in the compact output) and waits
longer for the response payload.

Verified result: `net http` now reaches `HTTP ok` with stage `S:7`. In the
successful QEMU user-net smoke test, MinervaOS sent a 56-byte HTTP GET, saw all
56 bytes ACKed by the server, received 798 bytes of response payload, and parsed
HTTP status `200`.

## Phase 7: HTTP Response Preview

The network layer now keeps the first 512 bytes of HTTP response payload and
detects the body offset after the HTTP header terminator. The terminal command:

```text
net page
```

prints a compact preview of the most recent HTTP response, fetching
`example.com` first if no response is cached yet. This is the first bridge from
raw HTTP diagnostics toward a real browser/viewer layer.

The preview now performs a tiny HTML text pass: it starts at `<body>` when that
tag is present, skips markup tags, decodes a few common entities, and collapses
whitespace so `net page` reads more like page text than raw source.

Follow-up renderer fix: the preview skips `<style>` and `<script>` contents
instead of displaying CSS/JavaScript text, and the HTTP capture buffer was
expanded to 1024 bytes so small pages are more likely to include visible body
text.

## Phase 7/8: Browser Window Shell

MinervaOS now has a desktop Browser app. The `Web` icon opens a browser window,
fetches `example.com` if no HTTP response is cached, and renders the captured
HTTP body as cleaned page text inside the GUI. This completes the Phase 7 HTTP
response viewer/browser integration checkpoint and starts Phase 8 with a real
browser surface, even though full HTML/CSS layout is still future work.

The Browser window now shows network diagnostics when no page is available,
including the HTTP stage and transmit/ACK/receive counters. The terminal also
has a `browser` command that opens/refocuses the Browser and attempts the same
fetch path as the desktop icon.

Browser-launched fetches now tolerate slow DNS startup better: DNS waits longer,
HTTP retries DNS resolution up to three times, and Browser refresh attempts the
full HTTP fetch twice before reporting `WAIT`.

## Phase 8: Basic URL Targets

The HTTP client is no longer hardcoded to `example.com`. It now parses plain
HTTP targets into host/path, encodes the selected host into the DNS query, and
builds the `GET` request with the matching `Host:` header.

New terminal commands:

```text
net http example.com/
browser example.com/
```

The default `net http` and `browser` commands still use `example.com/`.
Supported targets are plain HTTP only for now; `https://` and custom ports are
rejected until TLS and richer URL handling exist.

## Phase 8: Basic HTML Rendering

The Browser renderer now recognizes a first tiny set of HTML structure instead
of flattening everything as one plain text stream. It treats headings,
paragraphs, divs, and line breaks as block layout hints, keeps style/script
contents hidden, decodes common entities, and draws links in a distinct
underlined color. This is still a text-mode HTML renderer, but the Browser now
has the first real page-shaping layer that future CSS and clickable navigation
can build on.

## Phase 8: Basic CSS Styling

The Browser now has a tiny CSS color pass. It can parse simple `color`,
`background`, and `background-color` values from body inline styles and captured
style blocks, including common color names plus `#rgb` and `#rrggbb` hex
colors. The page panel uses the discovered background color, body text can pick
up a page-level color, and inline `style="color:..."` on rendered elements is
tracked with a small nesting stack.

This is not a real selector cascade yet, but it completes the first CSS
checkpoint: basic styling can now influence the Browser output instead of being
discarded entirely.

## Phase 8: First Link Navigation

The desktop now exposes a content-click callback, letting apps receive clicks
inside their window body after normal focus, drag, minimize, and close handling.
The Browser uses that to track the first rendered anchor rectangle, capture its
`href`, resolve root-relative and simple relative URLs against the current host,
and navigate when the user clicks the link text.

Plain HTTP links are loaded through the existing HTTP client. HTTPS links are
recognized but guarded with an `HTTP ONLY` status until TLS support exists, so
the Browser does not silently pretend secure web navigation is implemented.

Follow-up navigation fix: the Browser now tracks up to eight rendered link
hitboxes instead of only one global anchor rectangle. Each visible anchor gets
its own URL and bounding box during the render pass, and click handling scans
that table to choose the link under the pointer.

## Phase 8: TLS Probe Groundwork

The first HTTPS milestone is now a diagnostic probe rather than a fake browser
success. The network layer has a `net_tls_probe_url()` path that accepts plain
host/path targets or `https://` URLs, resolves the host through the existing DNS
client, and attempts a TCP connection to port 443.

New terminal commands:

```text
net tls
net tls example.com/
net tls https://example.com/
net tls https;//example.com/
```

Success means the TCP socket path to HTTPS is reachable. The command still
prints `Handshake todo` because TLS ClientHello, ServerHello parsing,
certificates, and crypto are separate milestones.

The URL parser also accepts `http;//` and `https;//` as terminal-friendly
aliases for `http://` and `https://`, since the current keyboard map can make
typing `:` awkward.

## Phase 8: TLS ClientHello Transmit

The TLS probe now sends a real, minimal ClientHello after the TCP connection to
port 443 succeeds. The ClientHello includes SNI for the requested host, a small
set of TLS 1.2-compatible cipher suites, supported groups, EC point formats, and
signature algorithms. It is intentionally fixed-size and deterministic enough
for early kernel debugging.

`net tls` now reports the ClientHello length, transmitted bytes, ACKed bytes,
received TLS bytes, and the first TLS record header:

```text
Rec:16 Ver:00000303
Len:... Hs:02
```

Record type `16` with handshake type `02` means the server answered with a
ServerHello. Full ServerHello parsing, certificates, key exchange, and crypto
remain future milestones.

Follow-up diagnostic fix: TLS capture now ignores non-record payload fragments
and preserves the first valid TLS record header instead of letting later bytes
overwrite the display. It also sets `ServerHello:1` when a handshake record with
handshake type `02` is seen. This completes the header-level ServerHello parse;
parsing the actual ServerHello fields is still next.

## Phase 8: TLS ServerHello Fields

The TLS diagnostic path now parses the first useful fields from a ServerHello:
the server legacy version, selected cipher suite, session ID length, and
extension block length. `net tls` prints them as:

```text
SV:00000303 CS:00001301
SID:32 Ext:...
```

This confirms what the server negotiated at the first handshake layer. The next
TLS milestone is capturing and identifying the Certificate handshake message,
before any certificate validation or key exchange crypto is attempted.

## Phase 8: TLS Certificate Capture

The TLS receive path now walks every complete TLS record in a received TCP
payload and scans handshake messages inside handshake records. After sending the
ClientHello, `net tls` keeps polling past ServerHello so the next handshake
messages can arrive.

`net tls` now reports whether a Certificate handshake was seen:

```text
Cert:1 CL:...
```

This is still diagnostic only: MinervaOS is not validating certificates or
performing key exchange yet. The next TLS step is buffering multi-packet
handshake data so large certificates and fragmented records are handled
reliably.

## Phase 8: TLS Fragment Byte Tracking

The TLS diagnostics now keep lightweight state for a handshake message that
continues beyond the current TCP payload. This lets the Certificate path report
both the advertised handshake length and the number of certificate bytes seen so
far:

```text
Cert:1 CL:6655
CRx:... Rem:...
```

This is not a full TLS stream reassembler yet, but it confirms fragmented
records are being followed instead of discarded after the first packet.

## Phase 8: TLS Certificate Fragment Completion

`net tls` now waits past the first Certificate fragment and keeps polling until
the advertised Certificate handshake length has been accounted for. TLS stage
`7` means the certificate body byte count reached `CL`.

The receive code can continue a pending handshake inside the same TLS record or
across a following TLS handshake record, which is enough for the current
diagnostic path to prove the certificate chain arrived.

## Phase 8: TLS Certificate List Header

The Certificate parser now reads the TLS 1.2 certificate list header from the
Certificate handshake body. `net tls` reports the total certificate-chain list
length and the first certificate length:

```text
CList:... C1:...
```

This still does not parse ASN.1/X.509. It is the next small boundary before
buffering and decoding the first certificate.

## Phase 8: TLS First Certificate DER Envelope

The TLS diagnostic path now checks the first certificate blob for the ASN.1 DER
outer SEQUENCE tag and decodes its DER length field. `net tls` reports:

```text
DER:1 DL:...
```

`DER:1` means the first certificate starts with a plausible DER SEQUENCE, and
`DL` should match the first certificate length. This is the first X.509-shaped
check before parsing certificate fields such as issuer, subject, validity, and
public key.

## Phase 8: TLS X.509 TBS/Serial Envelope

The first certificate parser now steps inside the outer X.509 Certificate
SEQUENCE and identifies the `TBSCertificate` SEQUENCE. It also skips the
optional version wrapper and reads the serial-number length.

`net tls` reports:

```text
TBS:1 TL:... Ser:...
```

This confirms the certificate structure is being walked past the outer DER
envelope. The next X.509 milestones are decoding issuer/subject names,
validity timestamps, and the public-key algorithm.

## Phase 8: TLS X.509 Signature Algorithm OID

The X.509 parser now advances past the serial number and reads the
`signature` AlgorithmIdentifier OID inside `TBSCertificate`. `net tls` reports:

```text
Sig:<code> OID:<len>
```

Codes currently used by the diagnostic display:

- `1` = sha256WithRSAEncryption
- `2` = sha384WithRSAEncryption
- `3` = ecdsa-with-SHA256
- `4` = ecdsa-with-SHA384

This helps identify what kind of certificate signature the browser path will
eventually need to verify.

## Phase 8: TLS X.509 Validity Envelope

After the signature algorithm, the X.509 parser now skips the issuer `Name` and
reads the certificate `Validity` SEQUENCE. It reports the DER time tags and
lengths for `notBefore` and `notAfter`:

```text
Val:1 NB:17:13 NA:17:13
```

Tag `17` means UTCTime and tag `18` means GeneralizedTime. This still does not
convert the timestamps to calendar dates, but it proves the parser reached the
certificate validity window cleanly.

## Phase 8: TLS X.509 Validity Date Decode

The validity parser now decodes UTCTime and GeneralizedTime date prefixes into
compact `YYYYMMDD` integers:

```text
NBd:20250115 NAd:20260415
```

This still does not compare against a system clock, but it gives the browser
path human-readable certificate date boundaries.

## Phase 8: TLS X.509 Public-Key Algorithm OID

After validity, the X.509 parser now skips the subject `Name` and reads the
SubjectPublicKeyInfo algorithm OID. `net tls` reports:

```text
PK:<code> POID:<len>
```

Codes currently used by the diagnostic display:

- `1` = RSA public key
- `2` = EC public key

This identifies the public-key family needed for eventual certificate and
handshake verification.

## Phase 8: Dedicated Certificate Diagnostics

The terminal now has a `net cert` command. `net tls` stays focused on connection
and handshake status, while `net cert` prints the deeper X.509 fields from the
last TLS probe:

```text
Host:example.com
Chain:3652 Leaf:1982
DER:1 DL:1982
TBS:1 TL:893 Ser:16
Sig:3 OID:8
Val:1 NB:17:13 NA:17:13
NBd:20260402 NAd:20260701
PK:2 POID:7
CertRx:3655/3655
```

This gives future certificate parsing more screen room without bloating the
basic TLS command.

## Phase 8: TLS X.509 Issuer/Subject Common Names

The X.509 parser now extracts compact Common Name strings from the issuer and
subject `Name` fields in the leaf certificate. `net cert` reports:

```text
Iss:<issuer common name>
Sub:<subject common name>
```

The strings are capped at 31 printable characters for the terminal. This is a
first human-readable identity check before adding full Subject Alternative Name
DNS matching.

## Phase 8: TLS X.509 First SAN DNS Name

The X.509 parser now scans the certificate extensions for Subject Alternative
Name (`2.5.29.17`) and extracts the first DNSName entry. `net cert` reports:

```text
SAN:<first DNS name>
```

This is the field modern hostname validation should use, instead of relying on
the legacy subject Common Name.

## Phase 8: TLS X.509 SAN Host Match

`net cert` now reports whether the requested TLS host matches the parsed SAN DNS
name:

```text
HostOK:1
```

The matcher supports exact names and simple one-label wildcards such as
`*.example.com`. This is still diagnostic only, but it is the first hostname
validation decision in the HTTPS stack.

## Phase 8/11: RTC Date and Certificate Date Check

MinervaOS now has a small CMOS RTC reader plus a terminal `date` command. The
certificate diagnostics use that RTC date to compare the leaf certificate
validity range:

```text
DateOK:1 Now:20260521
```

This promotes the Phase 11 real-time clock item just enough to support HTTPS
certificate checks. It is still a simple CMOS date read, not full timekeeping or
timezone handling.

## Phase 8: TLS X.509 Basic Constraints

The certificate parser now scans extensions for Basic Constraints (`2.5.29.19`)
and reports whether the extension exists and whether the certificate claims CA
authority:

```text
HostOK:1 BC:1 CA:0
```

For a normal server leaf certificate, `CA:0` is expected. This is another
diagnostic trust marker before full chain validation.

## Phase 8: TLS X.509 Key Usage

The certificate parser now scans extensions for Key Usage (`2.5.29.15`) and
reports a compact bit summary on `net cert`:

```text
KU:<bits>
```

Diagnostic bits:

- `1` = digitalSignature
- `2` = keyEncipherment
- `4` = keyCertSign

For an ECDSA server leaf certificate, `KU:1` is a sensible result.

## Phase 8: TLS X.509 Extended Key Usage

The certificate parser now scans extensions for Extended Key Usage (`2.5.29.37`)
and reports a compact bit summary on `net cert`:

```text
EKU:<bits>
```

Diagnostic bits:

- `1` = serverAuth
- `2` = clientAuth

For a normal HTTPS server certificate, `EKU:1` is expected.

## Phase 8: TLS Leaf Trust Summary

The terminal now has a compact `net trust` command that summarizes the local
leaf-certificate checks without flooding the tiny terminal:

```text
Trust:PARTIAL
H:1 D:1 L:1
KU:1 EKU:1
Need:CA+SIG
```

`PARTIAL` means the host, date, leaf CA flag, Key Usage, and Extended Key Usage
checks passed. It is intentionally not `TRUSTED` yet because MinervaOS still
needs a CA store plus certificate-chain signature verification.

## Phase 8: TLS Known Issuer Diagnostic

The leaf certificate parser now marks a small built-in set of recognized issuer
Common Names. For the current `example.com` path this recognizes:

```text
Cloudflare TLS Issuing ECC CA 1
```

`net cert` reports `IssOK:<0|1>`, and `net trust` includes the compact issuer
bit:

```text
I:1 KU:1 EKU:1
```

This is only a stepping stone toward a real CA bundle. Full browser trust still
requires parsing the full chain and verifying certificate signatures.

## Phase 8: TLS Chain Link Diagnostic

The TLS certificate-list parser now steps beyond the leaf certificate and reads
the second certificate's subject Common Name. `net cert` reports the second
certificate size and whether the leaf issuer matches that subject:

```text
C2:<bytes> Link:<0|1>
CSub:<second-cert-subject>
```

`net trust` now includes `C:<0|1>`. When the chain link passes, the remaining
trust work is narrowed to actual signature verification:

```text
I:1 C:1
KU:1 EKU:1
Need:SIG
```

Follow-up fix: certificate parsing now assembles the TLS Certificate handshake
body into an internal buffer before parsing cert #1 and cert #2. This lets the
chain-link diagnostic work when the second certificate arrives in a later TLS
fragment instead of only counting the bytes.

## Phase 8: TLS Certificate Signature Metadata

The leaf X.509 parser now steps back out of `TBSCertificate` and reads the
certificate wrapper's outer `signatureAlgorithm` plus `signatureValue` BIT
STRING metadata. `net cert` reports:

```text
OSig:<code> OID:<len>
SVal:<bytes> U:<unused-bits>
```

This still does not verify the signature. It prepares the exact fields needed
for the next crypto milestone: hashing the TBS bytes and verifying the signature
with the intermediate certificate public key.

## Phase 8: TLS TBSCertificate SHA-256 Digest

MinervaOS now has a small SHA-256 implementation and hashes the complete leaf
`TBSCertificate` DER bytes when the certificate uses a SHA-256 signature
algorithm. `net cert` reports a compact fingerprint prefix:

```text
TBSH:<first-32-bits> H:1
```

`H:1` means SHA-256. This is still not a signature verification result, but it
turns the parsed certificate into the digest input that ECDSA/RSA verification
will consume next.

## Phase 8: Compact TLS Signature Diagnostics

The terminal now has a compact `net sig` command for the signature-verification
gate. It fits the important fields into the small terminal window:

```text
Sig:<inner> OS:<outer>
SVal:<bytes> U:<unused>
TBS:<bytes> H:<hash-alg>
Hash:<first-32-bits>
Chain:<0|1> Need:VERIFY
```

This keeps the full `net cert` command available while making the next TLS
crypto smoke tests easier to read.

## Phase 8: TLS Intermediate EC Public-Key Diagnostic

The second certificate parser now reads SubjectPublicKeyInfo for the
intermediate certificate and reports the key family, named curve, public key
length, and compact coordinate prefixes through `net sig`:

```text
IK:<alg> CV:<curve>
KLen:<bytes>
X:<first-32-bits>
Y:<first-32-bits>
```

Codes:

- `IK:2` = EC public key
- `CV:1` = P-256 / prime256v1
- `CV:2` = P-384 / secp384r1
- `CV:3` = P-521 / secp521r1

This gives the verifier the public-key metadata needed for the next ECDSA
milestone.

## Phase 8: TLS ECDSA Signature R/S Diagnostic

The leaf certificate signature parser now decodes the ECDSA `signatureValue`
BIT STRING as a DER SEQUENCE of two INTEGERs. `net sig` reports compact
prefixes and normalized integer lengths:

```text
R:<first-32-bits> L:<bytes>
S:<first-32-bits> L:<bytes>
```

For the current P-256 path, lengths near 32 bytes are expected after stripping
DER sign-padding zeroes. This leaves the verifier with digest, public key, and
the ECDSA `r/s` signature components available.

## Phase 8: TLS P-256 Verifier Operand Capture

The parser now stores the full 32-byte operands needed by a P-256 ECDSA
verifier:

- SHA-256 digest of the leaf `TBSCertificate`
- ECDSA signature `r`
- ECDSA signature `s`
- intermediate public-key `x`
- intermediate public-key `y`

`net sig` reports:

```text
Chain:1 VIn:1
Need:P256MATH
```

`VIn:1` means the verifier inputs are complete and normalized for the current
P-256/SHA-256 certificate path. The next step is implementing the P-256 finite
field and point math.

## Phase 8: P-256 Field Arithmetic Self-Test

MinervaOS now has the first P-256 math module with 256-bit field add, subtract,
multiply, and modular reduction over the NIST P-256 prime. The terminal command:

```text
net p256
```

prints the arithmetic self-test flags:

```text
P256 flags:0000000F
Add:1 Sub:1
Mul:1 Red:1
```

This is intentionally a small, testable math slice. The next verifier milestone
is point doubling/addition and scalar multiplication on top of these field
operations.

Follow-up: `net p256` now also self-tests modular inverse using Fermat
exponentiation (`a^(p-2) mod p`):

```text
P256 flags:0000001F
Inv:1
```

This gives affine point formulas the division operation they need. The
implementation is simple rather than optimized, so later performance work can
replace it with a tuned addition chain.

Follow-up: `net p256` now checks the official P-256 base point and affine point
doubling against the known `2G` test vector:

```text
P256 flags:0000007F
G:1 Dbl:1
```

`G:1` means the built-in base point satisfies the curve equation. `Dbl:1` means
the point doubling formula produced the expected P-256 `2G` coordinates.

Follow-up: `net p256` now checks affine point addition and a tiny scalar
multiply path by comparing both `2G + G` and `3 * G` against the known `3G`
test vector:

```text
P256 flags:000001FF
PAdd:1 SMul:1
```

This proves the double/add pieces compose. The next cryptographic step is a
full-width P-256 scalar multiply suitable for ECDSA verification, so `net sig`
now points at `Need:P256FULL` instead of the older broad math placeholder.

Follow-up: the scalar multiply path now accepts a full 32-byte big-endian scalar
input. `net p256` checks both `3 * G` through that 256-bit API and `0 * G`
returning the point at infinity:

```text
P256 flags:000007FF
S256:1 Z:1
```

This gives the ECDSA verifier the right scalar input shape. The remaining work
is speed and completeness: replacing affine per-step inversions with a
projective/full-width path that can handle real certificate signatures without
stalling the GUI.

Follow-up: `net p256` now also tests scalar arithmetic modulo the P-256 group
order `n`, which is the modulus ECDSA uses for `r`, `s`, `z`, `u1`, and `u2`:

```text
P256 flags:00007FFF
NRed:1 NAdd:1
NMul:1 NInv:1
```

This keeps coordinate-field math (`mod p`) separate from signature-scalar math
(`mod n`), which is required before wiring the certificate signature verifier
for real.

Follow-up: `net sig` now derives the first ECDSA verifier scalars from the
captured certificate signature:

```text
Sc:1 W:<prefix>
U1:<prefix> U2:<prefix>
Need:P256POINT
```

`W` is `s^-1 mod n`, `U1` is `z*w mod n`, and `U2` is `r*w mod n`. This proves
the signature, hash, and issuer-key inputs are crossing into the verifier math.
The next blocker is the point equation `u1*G + u2*Q`.

Follow-up: `net sig` now validates the issuer certificate EC public key as a
real P-256 point before it can be used as `Q`:

```text
Sc:1 Q:1
Need:P256MUL
```

`Q:1` means the captured uncompressed public key coordinates are below the
field prime and satisfy the P-256 curve equation. The remaining cryptographic
blocker is fast enough scalar multiplication for `u1*G + u2*Q`.

Follow-up: `net p256` now includes a Jacobian/projective scalar multiplication
self-test. It compares projective `3 * G` against the same known `3G` vector:

```text
P256 flags:0000FFFF
S256:1 Z:1 PJ:1
```

`PJ:1` means the projective double/add ladder agrees with the affine test
vectors while avoiding a field inverse at every scalar bit.

Follow-up: `net sig` now runs the ECDSA P-256 point equation on demand:

```text
P:1 V:<prefix>
SigOK:1
```

It computes `u1*G + u2*Q`, reduces the resulting X coordinate modulo the P-256
group order, and compares that value with signature `r`. `P:1` means the point
equation completed and `SigOK:1` means the certificate signature matched.

Follow-up: `net trust` now consumes the ECDSA verification result instead of
stopping at a partial certificate verdict:

```text
Trust:OK
I:1 C:1 S:1
Ready:HTTPS
```

`S:1` is the signature gate from `net sig`. A certificate is only promoted to
`Trust:OK` when hostname, RTC date validity, leaf constraints, issuer, chain,
key usage, extended key usage, and the ECDSA signature check all pass.

Follow-up: the TLS probe now keeps reading past the certificate until it sees
the TLS 1.2 `ServerKeyExchange` message. The new terminal command:

```text
net kex
```

prints the ECDHE envelope:

```text
Kex:1 L:<len>
CurveT:3 C:00000017
KeyL:65 V:1
Sig H:<hash> A:3
```

`CurveT:3` means named curve, `C:00000017` is P-256, `KeyL:65` is an
uncompressed EC point, and `V:1` means the ephemeral server key is on the P-256
curve. This is the next HTTPS bridge after certificate trust: we now have the
server's ECDHE key and the signature envelope that must be verified before
client key exchange and record encryption can begin.

Follow-up fix: the ClientHello now offers only P-256 in the supported-groups
extension. Some servers prefer X25519 when it is offered, which produced
`C:0000001D` and a 32-byte key that our current P-256 verifier cannot use yet.
For this stage we steer the handshake toward P-256 so `net kex` can validate the
ephemeral server key with the curve code already present in the kernel.

Follow-up: `net kex` now verifies the TLS 1.2 `ServerKeyExchange` signature.
The verifier hashes `ClientHello.random || ServerHello.random ||
ServerECDHParams` with SHA-256, then checks the ECDSA signature with the leaf
certificate public key:

```text
LeafK:2 C:1
KH:<hash-prefix> V:<verify-prefix>
KSig:1 In:1
```

`KSig:1` means the server's ephemeral ECDHE key was signed by the certificate
key we already validated through `net trust`. That closes the trust gap between
certificate validation and key exchange, preparing the next step:
`ClientKeyExchange`.

Follow-up: MinervaOS can now transmit a TLS 1.2 ECDHE `ClientKeyExchange`.
The new command:

```text
net cke
```

verifies `KSig` first, creates a deterministic P-256 client ephemeral public
key, sends the `ClientKeyExchange` handshake record, and waits briefly for the
TCP ACK:

```text
CKE:1 Ack:1
Len:75 Stage:12
Pub:1 X:<prefix>
```

This is still not encrypted HTTPS yet. It is the final plaintext handshake
message before ChangeCipherSpec/Finished and the TLS key schedule.

Follow-up: `net keys` now derives the first TLS secrets. It multiplies the
server's verified P-256 ECDHE public key by MinervaOS's client ephemeral scalar
to get the shared secret, then runs the TLS 1.2 SHA-256 PRF for the 48-byte
master secret:

```text
Shared:1 X:<prefix>
Master:1 M0:<prefix>
M1:<prefix> Stage:13
```

Only short prefixes are displayed. The next TLS milestone is expanding the
master secret into the key block and using it for ChangeCipherSpec/Finished.

Follow-up: `net keys` now also expands the TLS 1.2 key block for the negotiated
AES-128-GCM suite:

```text
KB:1 L:40
CW:<client-key> SW:<server-key>
CIV:<client-iv> SIV:<server-iv>
```

The 40-byte block holds the client/server write keys plus fixed IV prefixes.
This is still diagnostic only; AES-GCM record encryption/decryption comes next.

Follow-up: MinervaOS now tracks the TLS 1.2 handshake transcript and computes
the client Finished `verify_data`:

```text
net fin
TH:<hash-prefix> TL:<transcript-bytes>
Ovf:0 Fin:1
F0:<prefix> F1:<prefix>
F2:<prefix> Stage:14
```

The transcript includes ClientHello, ServerHello, Certificate,
ServerKeyExchange, ServerHelloDone, and ClientKeyExchange. `Fin:1` means the
12-byte Finished value was derived from the master secret and handshake hash.
The next step is AES-GCM so ChangeCipherSpec/Finished can actually be sent.

Follow-up: MinervaOS now has enough AES-128-GCM record encryption to send the
client side of the TLS transition:

```text
net finish
CCS:1 FinTx:1
Ack:1 RL:40
Tag:<prefix> Stage:15
```

The command sends a plaintext ChangeCipherSpec followed by an encrypted
Finished handshake record using the client write key and IV from `net keys`.
This is the first encrypted TLS record MinervaOS transmits. The next checkpoint
is receiving, decrypting, and verifying the server Finished record.

Follow-up: `net finish` now waits for the server side of the handshake, decrypts
the AES-128-GCM server Finished record, and compares its verify-data:

```text
SCCS:1 SFin:1
Dec:1 Ver:1
```

`Ver:1` means the server proved it derived the same TLS master secret and
accepted our encrypted Finished. This completes the TLS handshake checkpoint;
the next work is encrypted application-data records for HTTPS GET.

Follow-up: MinervaOS can now encrypt and transmit a TLS application-data record
after the Finished exchange:

```text
net app
AppTx:1 Ack:1
Plain:<http-get-bytes> RL:<tls-record-bytes>
Tag:<prefix> Stage:17
```

The command builds the normal HTTP GET for the active TLS host/path, encrypts
it with AES-128-GCM using the client write key and sequence number 1, sends the
record as TLS application data, and waits for the TCP ACK. This proves the
post-handshake record crypto path; the next checkpoint is decrypting the
server's encrypted HTTPS response into the browser/HTTP capture buffer.

Follow-up: `net app` now handles the first encrypted server application-data
record too:

```text
AppRx:1 Dec:1
HTTP:200 Bytes:<captured>
Resp:<plaintext-bytes> RTag:<prefix>
```

The receive path authenticates the server record with AES-128-GCM using the
server write key and sequence number 1, decrypts the plaintext HTTP response,
parses the status line, and feeds the bytes into the existing page/browser
capture buffer. This completes the first real HTTPS GET loop for the
`example.com` smoke target.

Follow-up: the Browser app now drives the HTTPS path directly. Opening:

```text
browser https;//example.com/
```

uses the TLS probe, certificate/key-exchange checks, encrypted Finished,
encrypted GET, and decrypted response capture before rendering the page text in
the browser window. HTTPS links are no longer blocked by the old `HTTP ONLY`
guard; relative links from HTTPS pages keep the HTTPS scheme.

Follow-up: the Browser address bar is now editable. Clicking the URL field puts
the browser into `EDIT` mode, printable keys append to the URL, Backspace
deletes, Enter fetches the typed address, and Escape cancels edit mode. This
works with the keyboard-friendly `https;//example.com/` form as well as plain
host/path targets.

Follow-up: the Browser now follows simple HTTP redirects. When a fetched page
returns `301`, `302`, `303`, `307`, or `308`, the browser parses the
`Location:` response header, resolves relative redirects against the current
URL, updates the address bar, and retries up to two redirects. This is the
first step toward handling normal public-web navigation such as
`https;//google.com/` redirecting to another host/path.

Follow-up: HTTPS application-data receive now buffers fragmented TLS records.
Large public sites can split one encrypted TLS record across several TCP
segments; the Browser now tracks pending app-record bytes, waits for the full
record, then authenticates/decrypts it into the HTTP page buffer. The capture
buffer was expanded to 8 KiB so larger first responses can render instead of
showing `TLS` with `Bytes 0`.

Follow-up: the Browser renderer now has a readable-text fallback. If the normal
HTML body pass draws no visible characters, it scans the decrypted response
again, skips tags plus script/style blocks, decodes the simplest entities, and
draws any readable text it can extract. This keeps modern pages from becoming a
blank white window just because the first useful content is outside the tiny
layout engine's current comfort zone.

Follow-up: blank rendered pages now show compact HTTP header diagnostics. When
no readable text can be extracted, the Browser displays `Content-Type`,
`Content-Encoding`, and `Location` when those headers are present. This makes
modern-site failures easier to classify: compressed body, redirect, script-only
HTML, or simply markup beyond the current renderer.

Follow-up: the Browser now has a raw source preview toggle. With the Browser
focused and the address bar not in edit mode, pressing `v` switches between the
normal rendered view and a compact source view of the captured response body.
This gives us direct visibility into decrypted HTML bytes when a modern page
does not render cleanly yet.

Follow-up: blank pages now try a document-summary fallback before giving up.
The Browser extracts `<title>` text and meta description content
(`name="description"` or `property="og:description"`) from the decrypted HTML
and displays those alongside the header diagnostics. This gives script-heavy
pages a useful identity even before the layout engine or JavaScript engine can
render their real UI.

Follow-up: source preview is now scrollable. In Browser source mode, `j`, `n`,
or Space advances through captured HTML bytes; `k`, `p`, or Backspace moves
back; and `g` jumps to the beginning. The source pane shows its byte offset so
large modern pages such as Google can be inspected beyond the first screenful
without increasing the kernel capture buffer again.

Follow-up: source preview now has marker jumps. With source mode active, `t`
jumps to `<title`, `m` to `<meta`, `b` to `<body`, `l` to the next `href`, and
`s` to `<script`. The compact status label shows the marker that was found, or
`MISS` if that marker is not present in the captured response slice.

Follow-up: source preview now has a tiny find mode. In source mode, `/` opens a
search prompt on the bottom row, printable keys build a short query, Backspace
edits it, Enter jumps to the next match with wraparound, and Escape cancels.
This is better than hardcoding every modern-page marker because it can search
for whatever appears in the captured HTML, such as `bran`, `body`, `google`,
`href`, or `script`.

Follow-up: normal Browser rendering now has a head metadata summary fallback.
If a captured page has no readable body text, MinervaOS still tries to extract
useful `<head>` context: the HTML `itemtype` shortened to names such as
`WebPage`, the document `lang`, and meta `http-equiv="Content-Type"` content.
This makes head-heavy modern pages show a useful identity in normal mode
instead of immediately dropping to `No readable text`.

Follow-up: the fallback page-info panel is now more compact and less redundant.
It shows the current host first, keeps page type and language, and condenses
content type values such as `text/html; charset=UTF-8` into a shorter
`html UTF-8` document line. If that meta content exists, the Browser no longer
prints a duplicate HTTP `Content-Type` line below it.

Follow-up: the Browser now keeps a one-step history slot. Navigating through a
link, opening a new URL, or entering URL edit mode remembers the previous
address. In normal browser mode, `h` or Backspace loads that previous address
and swaps the current page into the history slot, giving a tiny but useful Back
button behavior for bad edits, redirects, and quick page experiments.

Follow-up: the Browser can now reload the current page. In normal browser mode,
`r` refetches the current address, clears source/search state, and reports
`RLD` on success. Clicking the small status badge in the browser chrome performs
the same reload action, giving the GUI a visible control for retrying a page
without retyping the URL.

Follow-up: the Browser now has a home action. The default Browser launch opens
`https;//example.com/`, and normal browser mode key `o` returns to that
known-good HTTPS page while preserving the previous URL in the one-step history
slot. This gives testing a reliable reset point after experimenting with larger
or stranger public sites.

## Phase 9: Theme Palette Foundation

MinervaOS now has a first shared theme palette in `include/theme.h`. The desktop
wallpaper, taskbar, task buttons, icon labels, default window colors, titlebar
buttons, and focus/idle borders all read from named theme colors instead of
hardcoded scattered palette indexes. The visual output should stay familiar for
now; the win is architectural, giving future themes and icon/UI polish one
central place to grow from.

Follow-up: the terminal now has a compact `theme` command. It prints the current
classic palette indexes for wallpaper, taskbar, window chrome, titlebar, focus
border, idle border, close/minimize buttons, and icon text. This makes the
theme foundation inspectable from inside MinervaOS before runtime theme
switching exists.

Follow-up: theme colors are now runtime-selected through `drivers/theme.c`.
`theme classic` restores the original palette and `theme night` switches the
shared desktop/window chrome to a darker palette. Running `theme` reports the
active theme name and palette indexes. Existing app-specific interiors still use
their own colors, but wallpaper, taskbar, task buttons, default chrome, borders,
and titlebar controls now read from the active theme.

Follow-up: default window chrome is now theme-linked. Windows start with themed
background/title colors and continue to follow runtime theme switches until an
app calls `window_set_bg_color()` or `window_set_title_color()`. This preserves
custom app styling while making generic/default windows respond correctly to
`theme classic` and `theme night`.

Follow-up: theme switching now has discovery and cycling commands. `theme list`
prints the available theme names, and `theme next` advances through them without
typing the exact name. This gives the tiny runtime theme system a more usable
in-OS control surface as more palettes are added.

Follow-up: the early static kernel heap was reduced from 256 KiB to 224 KiB to
restore breathing room below the bootstrap stack guard. The browser/TLS work
made `.text` large enough that aligned `.bss` sections were landing close to the
Makefile's `__kernel_end <= 0x98000` safety check. Keeping the heap fixed but
slightly smaller is the least invasive short-term fix; a later dynamic/page-
backed heap can grow beyond this without burning permanent low-memory space.

## Phase 9: Package Manager Seed

MinervaOS now has a first package-manager surface backed by a tiny manifest file
in the FAT32 image:

```text
pkg
pkg list
pkg info THEME
pkg install THEME
```

`PKGS.TXT` lists seed packages, `pkg list` shows them with `*` for installed
entries, `pkg info NAME` prints the marker file and summary, and
`pkg install NAME` writes a small `*.PKG` receipt into the root directory. This
is intentionally not real executable installation yet; it establishes the
package discovery/install workflow that later app bundles, repositories, and
signatures can plug into.

Follow-up: packages can now be inspected and removed:

```text
pkg status
pkg remove THEME
```

`pkg status` prints every manifest entry as installed or available, and
`pkg remove NAME` deletes the installed receipt file. This turns the first
package workflow into a reversible loop before real package contents exist.

## Phase 9: App Registry Seed

MinervaOS now has the first piece of an app SDK surface: a FAT32-backed app
manifest and a stable launcher command.

```text
app
app list
app info EDIT
app run EDIT
```

`APPS.TXT` maps app IDs to launcher IDs and summaries. `app list` and
`app info NAME` inspect that registry, while `app run NAME` launches known
built-in apps through the registry (`EDIT`, `VIEW`, `AUDIO`, `WEB`, and `TERM`).
This is still not third-party executable loading, but it starts the boundary an
SDK will need: apps have IDs, metadata, and a launcher contract separate from
desktop icons or one-off shell commands.

Follow-up: the bootloader kernel load window was expanded from 128 KiB to
192 KiB. The loader already reads one sector at a time using CHS, and the
kernel clears `.bss` itself in `kernel_entry.asm`, so loading extra zero-padded
sectors is safe while giving the growing Phase 8/9 kernel more initialized-code
room. The separate `__kernel_end <= 0x98000` guard still protects the bootstrap
stack area.

Follow-up: app manifest entries now include launch targets:

```text
NAME|launcher|target|summary
```

The built-in launchers use that target as the editor filename, viewer bitmap,
audio file, or browser URL. That means the registry is no longer just a list of
hardcoded app names; it is the first small contract for app metadata that can
point existing handlers at package-provided files later.

Follow-up: the app registry can now be extended from inside MinervaOS:

```text
app add NOTES editor NOTE.TXT Notes
app list
app run NOTES
```

The command appends a new `NAME|launcher|target|summary` row to `APPS.TXT`.
This is still limited to known built-in launcher types, but it proves package or
user-created metadata can register a new launchable app entry at runtime.

Follow-up: app registry edits are now reversible:

```text
app remove NOTES
```

The command rewrites `APPS.TXT` without the selected entry, keeping the tiny app
database editable from inside MinervaOS instead of append-only.

Follow-up: editable app metadata can now be validated:

```text
app check
```

The command scans `APPS.TXT`, verifies each entry uses a known launcher, and
checks target basics such as browser URLs being present and media-file launchers
pointing at readable files. This gives the app registry its first built-in
sanity check for user-added or package-added entries.

Follow-up: the app registry contract is now documented inside the OS. The FAT32
image includes `SDK.TXT`, and the terminal command:

```text
sdk
```

prints the current App SDK v1 row format, known launcher names, and the basic
commands for adding, checking, and running registry entries.

## Phase 9: Open Source Documentation Refresh

The public-facing project docs have been refreshed for the current MinervaOS
shape. `README.md` now describes the modern kernel, desktop, networking,
browser, package, theme, and app-registry state instead of the old early-phase
prototype.

New docs:

- `docs/ARCHITECTURE.md`
- `docs/CONTRIBUTING.md`
- `docs/RELEASE_CHECKLIST.md`

These do not complete the final public release by themselves, but they give the
project a clearer open-source landing surface and a repeatable release checklist
for future snapshots.

Follow-up: repository collaboration scaffolding is now in place:

- `.github/ISSUE_TEMPLATE/bug_report.md`
- `.github/ISSUE_TEMPLATE/feature_request.md`
- `.github/PULL_REQUEST_TEMPLATE.md`
- `docs/ROADMAP.md`
- `docs/LICENSE_DECISION.md`

The project still needs an explicit final license choice before it should be
called fully open-source licensed, but contributors now have clearer issue, PR,
roadmap, and release-prep guidance.

Follow-up: release-facing project scaffolds were added:

- `CHANGELOG.md`
- `docs/DEVLOG.md`
- `SECURITY.md`
- `SUPPORT.md`

These give future public snapshots a place for release notes, short devlog
updates, security expectations, and support boundaries without changing kernel
behavior.

Follow-up: the manual serial smoke test is now automated through:

```text
make smoke
```

The target runs `scripts/smoke_qemu.sh`, boots QEMU headlessly, accepts the
expected timeout as normal, and verifies that the key serial initialization
lines appeared. This gives contributors and release checks a repeatable
pass/fail boot smoke without opening a GUI window.

Follow-up: MinervaOS now has an explicit open-source license. The project owner
selected MIT, so `LICENSE` has been added, the README license section points to
it, and the license decision note now records MIT as the active project license.

## Phase 10: VBE / Linear Framebuffer Plan

Phase 10 has started with a written implementation plan for moving beyond VGA
mode 13h. The new `docs/VBE_PLAN.md` captures the first safe engineering path:
probe VESA BIOS Extensions in real mode, collect candidate true-color linear
framebuffer modes, pass selected mode information into the protected-mode
kernel, and add a graphics backend transition layer so the current 320x200 path
can remain as a fallback.

The roadmap now distinguishes completed open-source release preparation from
the future public GitHub release step, and the progress table marks networking
and browser work as complete, Phase 9 as in progress, and Phase 10 as started.
