# ReadyShell Host Version For Mocking, Unit, And Smoke Testing

This document describes the current host-side ReadyShell test harness in
ReadyOS.

It is not a separate productized host port of ReadyShell. It is a native
`clang` test harness around the real parser, VM, value, serialization, and
external-command protocol code used by the C64 build.

For the user-facing shell guide, see
[../src/apps/readyshell/README.md](../src/apps/readyshell/README.md).
For the full C64 architecture, overlay model, and runtime layout, see
[./ReadyShellArchitecture.md](./ReadyShellArchitecture.md).

## 1. What The Host Harness Is

ReadyShell host testing exists to validate logic quickly without booting VICE
or running on a real C64.

The host harness compiles selected ReadyShell sources as normal native code and
runs small test executables on the host machine. It keeps the same parser, VM,
value, and REU-serialization code paths wherever possible, but replaces the
hardware-facing pieces with narrow mocks.

That means the host harness is best thought of as:

- a fast parser smoke test
- a fast VM and pipeline behavior smoke test
- a fast REU-backed value and serialization unit test
- a protocol-level test for the external overlay command ABI

It is not:

- a full host UI version of ReadyShell
- a cycle-accurate C64 emulator
- a replacement for disk boot testing in ReadyOS
- a substitute for overlay loading, REU DMA, or true-drive verification

## 2. Make Targets

The current host-side targets are:

- `make readyshell-parse-smoke-host`
- `make readyshell-vm-smoke-host`
- `make readyshell-reu-tests-host`

`make verify` currently runs:

- `readyshell-vm-smoke-host`

`make verify` does not currently run:

- `readyshell-parse-smoke-host`
- `readyshell-reu-tests-host`

## 3. End-To-End Shape

```text
native clang build
    |
    +--> parser smoke binary
    |       uses real parser sources
    |
    +--> VM smoke binary
    |       uses real parser + generic VM + value/runtime helpers
    |       with mocked platform callbacks and mocked REU backing
    |
    +--> C64-flavored VM smoke binary
    |       uses real parser + C64 VM dispatch path
    |       with mocked REU backing and mocked overlay command entrypoint
    |
    `--> REU/value test binary
            uses real value, serialization, and LDV-side helpers
            with mocked REU backing
```

## 4. What Each Target Actually Tests

### 4.1 `readyshell-parse-smoke-host`

This target compiles the real parser sources and calls `rs_parse_source()`
directly.

What it covers:

- parsing valid expressions and statements
- parsing simple pipelines
- parser rejection paths
- error code, message, line, and column reporting

What it does not cover:

- execution
- REU use
- overlays
- platform callbacks
- shell UI behavior

### 4.2 `readyshell-vm-smoke-host`

This target has two passes.

The first pass links the generic VM path and injects platform callbacks for
directory listing and drive info.

What it covers:

- expression evaluation
- assignments
- filters
- foreach stages
- `PRT`
- `GEN`
- `TOP`
- `MORE`
- `SEL`
- array and object access
- `LST` through a mocked directory listing provider
- `DRVI` through a mocked drive-info provider
- pause-flag behavior stored in REU metadata

The second pass links the C64-oriented VM dispatch path and exercises the
resident-to-external-command protocol through a mocked
`rs_overlay_command_call()`.

What that second pass adds:

- external command capability handling
- `BEGIN` / `ITEM` iteration flow
- `RUN` command flow
- C64-side external command dispatch shape
- `DRVI` and `LST` through the overlay command ABI instead of the generic
  `RSVMPlatform` callbacks

What this target still does not cover:

- real PRG overlay loading from disk
- REU overlay cache restore and verification
- ROM banking under BASIC
- the actual shell UI loop
- actual file commands like `PUT`, `ADD`, `CAT`, `COPY`, `DEL`, `REN`

### 4.3 `readyshell-reu-tests-host`

This target is closer to a unit test suite for REU-backed value handling.

What it covers:

- drive-prefix parsing and canonicalization helpers
- REU heap reset behavior
- cloning strings into REU-backed storage
- cloning arrays into REU-backed storage
- cloning objects into REU-backed storage
- reading arrays and objects back out
- file-payload serialization
- file-payload deserialization
- equality after round-trip
- the `LDV`-side loader that reconstructs values from the REU scratch area

What it does not cover:

- real file I/O
- real `STV` / `LDV` disk transport
- real overlay PRG loading
- shell UI or command-line interaction

## 5. What Gets Mocked

The host harness deliberately mocks only the edges where the real ReadyShell
implementation meets C64-specific hardware or media.

### 5.1 Mocked REU Backend

The host REU implementation is a 16 MB byte array in process memory.

Behavior:

- `rs_reu_available()` always returns true
- `rs_reu_read()` and `rs_reu_write()` become bounds-checked `memcpy`
  operations
- the same absolute REU offsets and shared metadata regions are still used

What this preserves:

- REU address discipline
- scratch-region conventions
- REU heap layout and metadata validation
- shared pause-flag storage

What this skips:

- DMA timing
- bank-switch edge cases
- transfer failures caused by hardware state
- probe logic for actual REU presence

### 5.2 Mocked VM Platform Callbacks

The generic VM harness injects synthetic implementations for:

- directory listing
- drive information

`LST` always sees a small fixed directory with entries like:

- `alpha`
- `beta`
- `gamma`

`DRVI` always sees predictable drive metadata such as:

- drive number
- disk name `readyos`
- ID `ro`
- free blocks `664`

This lets the host VM test selection, formatting, indexing, and downstream
pipeline behavior without touching IEC or disk images.

### 5.3 Mocked Overlay Command ABI

The overlay-aware host pass does not load real overlay PRGs.

Instead, it replaces `rs_overlay_command_call()` with a stub that simulates:

- `LST` as a `BEGIN` + repeated `ITEM` stream
- `DRVI` as a `RUN` command

This is important because it validates the resident VM's command-protocol
handling:

- command capability bits
- `BEGIN`
- `ITEM`
- `RUN`
- iteration count handling
- result propagation into downstream pipeline stages

It does not validate the real overlay loader or REU cache.

### 5.4 Overlay Debug Marking

The host build turns overlay debug marking into a no-op.

That keeps the signatures satisfied for code that expects the hook, without
pretending the host process is reproducing the real C64 overlay-debug path.

## 6. What Runs Real Code

Large parts of ReadyShell are not mocked in host mode.

The host harness still uses the real:

- parser
- lexer
- error model
- generic VM
- C64-oriented VM dispatcher for the overlay-aware pass
- value model
- array/object access logic
- formatting logic
- serialization logic
- `LDV`-side heap reconstruction helpers
- external command capability model

That is the main value of the harness: logic changes in these areas fail fast
without requiring a full ReadyOS boot.

## 7. How It Deals With C64 Architectural Differences

The host harness does not try to re-create the entire C64 memory map. Instead,
it isolates the portable logic behind small interfaces and substitutes only the
hardware boundary.

### 7.1 REU Access Is Abstracted

ReadyShell core code talks to:

- `rs_reu_available()`
- `rs_reu_read()`
- `rs_reu_write()`

On the real C64 build, these go through REU DMA.

On the host build, they go through the in-memory REU mock.

This keeps the logical REU contract intact while removing hardware timing from
the test path.

### 7.2 VM Platform Services Are Abstracted

The generic VM talks to an `RSVMPlatform` structure for operations like:

- file read
- file write
- directory list
- drive info

The host harness only supplies the callbacks needed for the scenarios under
test. Everything else stays unavailable by design.

That is useful because it makes missing coverage obvious. If a command needs
real file I/O and no callback is provided, the generic VM path reports that the
command is unavailable on that platform.

### 7.3 Fixed C64 RAM State Becomes Plain Host State

The real app has fixed RAM assumptions, for example:

- runtime state at `$CA00-$CFFF`
- REU/session flags in fixed high-RAM bytes
- a much smaller tap-log budget under cc65 constraints

In host mode:

- those fixed-address flags become normal globals
- the process uses the native heap
- some buffers are intentionally larger than the cc65 build

This means host tests preserve logic and data-format contracts, but not the
exact memory pressure or placement constraints of the C64 build.

### 7.4 The Overlay ABI Is Tested, Not The Overlay Loader

The C64 build must:

- map RAM under BASIC
- preserve IRQ state
- load overlay PRGs through KERNAL/CBM paths
- cache overlays into REU
- restore them into the shared window

The host harness does not emulate that mechanism.

Instead, it tests one layer up:

- command protocol correctness
- output/result shaping
- resident/external command dispatch expectations

That is a deliberate tradeoff. It is much faster, but it leaves real overlay
loading and banking to C64/VICE verification.

## 8. Important Limitations

The host harness has real value, but it is intentionally incomplete.

It does not validate:

- shell prompt editing
- keyboard normalization and PETSCII quirks
- screen rendering
- pause/resume key handling in the live UI
- true-drive behavior
- KERNAL disk I/O side effects
- actual disk-mounted file access
- overlay PRG loading from disk
- REU cache restore failures
- BASIC/ROM banking mistakes
- cc65 stack pressure
- C64 heap fragmentation behavior
- timing-sensitive bugs
- launch/shim/resume interactions across the full ReadyOS boot flow

In practice, that means host success proves:

- parser/VM/value logic is probably correct
- REU data-format logic is probably correct
- external command protocol handling is probably correct

It does not prove:

- the feature works inside the shipped ReadyOS app on a real C64 memory map
- the feature survives overlay swaps
- the feature survives real disk and drive behavior

## 9. Coverage Notes By Command Family

Host coverage is strongest for:

- parser behavior
- expression and pipeline semantics
- `PRT`
- `GEN`
- `MORE`
- `TOP`
- `SEL`
- `LST` formatting and downstream selection behavior
- `DRVI` formatting and downstream selection behavior
- REU-backed value persistence and reconstruction

Host coverage is partial or protocol-only for:

- `LDV`
- `STV`
- external command overlay dispatch

Host coverage is effectively absent for real media behavior of:

- `CAT`
- `PUT`
- `ADD`
- `DEL`
- `REN`
- `COPY`

Those still need C64/VICE validation through the normal ReadyOS workflow.

## 10. How To Interpret Failures

Use host failures to narrow the problem layer.

If `readyshell-parse-smoke-host` fails:

- suspect lexer/parser changes first

If the generic part of `readyshell-vm-smoke-host` fails:

- suspect parser-to-VM semantics
- suspect expression/pipeline execution logic
- suspect value formatting or pipeline stage behavior

If only the overlay-aware part of `readyshell-vm-smoke-host` fails:

- suspect external-command protocol handling
- suspect C64 VM dispatch integration
- suspect command capability or frame-shaping logic

If `readyshell-reu-tests-host` fails:

- suspect REU-backed value layout
- suspect serialization/deserialization
- suspect `LDV`-side heap reconstruction helpers

If host tests pass but the app still fails in ReadyOS:

- suspect overlay loading
- suspect memory-map or banking behavior
- suspect KERNAL/IEC disk behavior
- suspect REU DMA or cache restore behavior
- suspect UI-only integration issues

## 11. Recommended Use

The current host harness is best used as a fast first gate:

1. run host checks while iterating on parser/VM/value logic
2. run full `make verify` for the normal repo path
3. run ReadyOS through the supported `run.sh` workflow for C64/VICE validation

That ordering keeps feedback fast without confusing host-level success for full
platform-level correctness.

## 12. Bottom Line

ReadyShell host testing is intentionally a logic harness, not a hardware
emulator.

Its strength is that it executes the real parser, VM, value, and REU-data-path
code very quickly on the host. Its weakness is that it stops at the hardware
boundary and cannot prove correctness of the real C64 overlay, memory-map, or
disk environment.

That separation is deliberate and useful, as long as host-test success is
treated as one layer of evidence rather than final proof.
