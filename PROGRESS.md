# MinervaOS Progress Log

This file records how MinervaOS is growing. `PHASES.md` is the checklist; this
log explains the path, the safety rules, and the tests used along the way.

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
