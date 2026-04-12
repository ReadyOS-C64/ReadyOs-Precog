# ReadyShell Guide

This document describes the **current ReadyShell app in ReadyOS**, with examples for language features, commands, pipelines, and shell behavior.

It is intentionally implementation-accurate for this build, not a forward-looking spec.

For a broader recipe-style walkthrough, see [ReadyShelltutorial.md](./ReadyShelltutorial.md).
For the implementation architecture, see [../../docs/ReadyShellArchitecture.md](../../docs/ReadyShellArchitecture.md).
For the generated overlay/runtime inventory, see [../../docs/readyshell_overlay_inventory.md](../../docs/readyshell_overlay_inventory.md).

## 1. Scope: What Is Implemented

Implemented in this POC:

- Expression language (numbers, strings, booleans, vars, arrays, ranges, comparisons)
- Assignment and expression statements
- Pipelines with:
  - command stages
  - expression stages
  - filter stages `?[ ... ]`
  - foreach stages `%[ ... ]`
- Commands:
  - `PRT`
  - `GEN`
  - `MORE`
  - `TAP`
  - `TOP`
  - `SEL`
  - `DRVI`
  - `LST`
  - `LDV`
  - `STV`
- REPL built-ins:
  - `HELP`
  - `VER`
  - `CLEAR`

Still intentionally out of scope in this POC:

- Full reference-shell command set beyond the commands listed here

## 2. Shell Input and Editing

Prompt:

- `RS> `

Line editing behavior:

- Left/right cursor movement
- Insert toggle (`INS`)
- Delete/backspace
- In-line insert/overwrite editing
- Blinking cursor cell
- Held-key repeat is forced off for prompt editing so warp/turbo modes do not
  multiply spaces, letters, or cursor movement

Input line limits:

- Physical input max: **35 chars** per entered line
- Logical command max: **160 chars**

App navigation shortcuts from this shell:

- `CTRL+B` or `RUN/STOP`: return to launcher
- `F2`: next app
- `F4`: previous app

Pipeline typing on C64 keyboard:

- Parser pipeline operator is `|`
- **On C64 keyboard, type `!` to enter `|`**
- Some C64 key variants are also normalized to pipe

Streaming output pause:

- While lines are printing, press space to pause output
- Footer changes to `PAUSED - PRESS ANY KEY`
- Resume by releasing the current key and pressing any key again
- This pauses ReadyShell before it continues feeding more pipeline output to the printer

## 2.1 Runtime Memory Layout

Current release build memory layout:

- Resident app window: `$1000-$C5FF` (`46592` bytes)
- Overlay load-address bytes: `$8DFE-$8DFF`
- Overlay execution window: `$8E00-$C5FF` (`14336` bytes)
- Resident BSS: `$877B-$8971` (`503` bytes)
- Resident heap: `$8972-$8DFD` (`1164` bytes)
- High RAM runtime outside the app snapshot: `$CA00-$CFFF`

Overlay policy:

- Overlays `1` and `2` are still separate files on disk
- They now share one REU cache bank, `0x40`
- Each shared cache slot stores the full overlay window size, not just file bytes
- Overlays `3-5` remain disk-loaded command overlays
- Shared REU metadata, command registry, pause state, command scratch, and the value arena live in bank `0x48`

Shared REU cache layout:

```text
bank 0x40
+-------------------------------+ 0x400000
| overlay 1 parse slot          |
| full window snapshot 0x3800   |
+-------------------------------+ 0x403800
| overlay 2 exec slot           |
| full window snapshot 0x3800   |
+-------------------------------+ 0x407000
| free tail in bank 0x40        |
+-------------------------------+ 0x40FFFF
```

Current overlay set:

- `OVERLAY1` `rsparser.prg`: parser / lexer, `13005` live bytes, cached in bank `0x40` parse slot
- `OVERLAY2` `rsvm.prg`: execution core for `PRT`, `MORE`, `TOP`, `SEL`, `GEN`, `TAP`, `14048` live bytes, cached in bank `0x40` exec slot
- `OVERLAY3` `rsdrvilst.prg`: `DRVI` + `LST`, disk-loaded
- `OVERLAY4` `rsldv.prg`: `LDV`, disk-loaded
- `OVERLAY5` `rsstv.prg`: `STV`, disk-loaded

## 3. Statement Forms

A logical command line can contain one or more statements.

Statement separators:

- Newline
- `;`

Forms:

- Assignment: `$A = <expr-or-pipeline>`
- Pipeline statement: `<stage> | <stage> | ...`
- Expression statement: `<expr>`

Examples:

```text
$A = 1..5
$B = $A(2)
PRT "A=", $A ; PRT "B=", $B
```

## 4. Values and Literals

Supported runtime value categories:

- Boolean: `TRUE`, `FALSE`
- Number: unsigned 16-bit integer
- String: `"TEXT"`
- Array: `[item1,item2,...]` in formatted output
- Object: formatted as `{KEY:VALUE,...}` (can flow through runtime, but no object literal syntax in this POC)

Literal examples:

```text
42
"HELLO"
TRUE
FALSE
```

Notes:

- Commands/identifiers are case-insensitive
- Variable names are case-insensitive (`$a` and `$A` refer to same variable)

## 5. Variables

Assignment:

```text
$A = 5
$B = "READY"
$C = 1,2,3
```

Read:

```text
PRT $A
PRT $B
PRT $C
```

C64 build variable slot limit:

- Up to **24 named variables** (`$NAME`)

When assignment RHS is a pipeline:

- 0 emitted items -> variable becomes `FALSE`
- 1 emitted item -> variable becomes that single value
- 2+ emitted items -> variable becomes array of emitted items

## 6. Expressions

### 6.1 Arrays via comma expressions

Comma-separated expressions form an array in expression contexts:

```text
$A = 1,2,3
PRT $A
```

Output:

```text
[1,2,3]
```

### 6.2 Ranges

Range syntax:

- `start..end` (inclusive)
- Ascending or descending

Examples:

```text
PRT 1..5
PRT 5..1
```

Output:

```text
[1,2,3,4,5]
[5,4,3,2,1]
```

### 6.3 Indexing

Index operator uses parentheses:

```text
$A = 10,20,30
PRT $A(0)
PRT $A(2)
```

Out-of-range index returns `FALSE`.

### 6.4 Property access

Property syntax:

```text
@.NAME
$OBJ.VALUE
```

If target is not an object, or property is missing, result is `FALSE`.

### 6.5 Comparisons

Supported operators:

- `>`
- `<`
- `>=`
- `<=`
- `==`
- `<>` (not-equals)
- `!=` (alias also accepted)

Examples:

```text
PRT 5 > 3
PRT 5 == 5
PRT 5 <> 4
PRT "A" != "B"
```

Notes:

- `==` and not-equals work on value equality
- `> < >= <=` require numeric-compatible operands

## 7. Pipelines

General form:

```text
<stage> | <stage> | <stage>
```

Stage kinds:

- Command stage: `PRT ...`, `GEN ...`, `TAP ...`
- Expression stage: any expression
- Filter stage: `?[ <script> ]`
- Foreach stage: `%[ <script> ]`

Pipeline items stream stage-by-stage, item-by-item.

### 7.1 `@` binding in scripts

Inside `?[ ... ]` and `%[ ... ]`, `@` is the current pipeline item.

### 7.2 Filter `?`

`?[script]` keeps an item only if the script's final value is truthy.

Example:

```text
GEN 10 | ?[ @ <= 4 ] | PRT @
```

Output:

```text
1
2
3
4
```

### 7.3 Foreach `%`

`%[script]` executes script per item, then always passes item through.

Example:

```text
GEN 3 | %[ PRT "ITEM=", @ ] | PRT @
```

Output:

```text
ITEM=1
1
ITEM=2
2
ITEM=3
3
```

### 7.4 Expression stages in pipeline

Expression stages can transform current item:

```text
GEN 5 | @ > 2 | PRT @
```

Output:

```text
FALSE
FALSE
TRUE
TRUE
TRUE
```

### 7.5 Auto-print behavior

Pipeline statements auto-print emitted items **unless** the last stage is `PRT`.

Examples:

```text
GEN 3
GEN 3 | @
GEN 3 | PRT @
```

Behavior:

- First two auto-print outputs
- Last one is explicitly printed by `PRT`, so no extra auto-print duplication
- While auto-printed or `PRT` output is streaming, space pauses the stream

### 7.6 `MORE` paging behavior

`MORE` is a pass-through pipeline stage. It does not collect, transform, or print
items itself. It simply forwards each item and requests a pause every N items.

Examples:

```text
1..50 | MORE
1..50 | MORE | PRT @
1..9 | MORE 5
```

Behavior:

- `MORE` defaults to a page size of `20`
- `MORE 5` pauses after every 5th item
- The triggering item still passes through normally
- After resume, the next page continues from the next item

## 8. Commands

### 8.1 `PRT`

Purpose:

- Print arguments, or print current pipeline item when called without args and `@` exists

Syntax:

```text
PRT
PRT <expr>
PRT <expr>, <expr>, ...
```

Examples:

```text
PRT "HELLO"
PRT "X=", 5
GEN 3 | PRT @
```

Notes:

- Arguments are concatenated in print output
- In a pipeline with current item and no args, `PRT` prints that item and passes it onward

### 8.2 `GEN`

Purpose:

- Emit numbers `1..N` into pipeline

Syntax:

```text
GEN <number>
```

Examples:

```text
GEN 5
GEN 5 | ?[ @ > 3 ]
```

Errors:

- Non-numeric input -> `GEN expects numeric count`

### 8.3 `TAP`

Purpose:

- Append current item to internal tap log and pass item onward

Syntax:

```text
TAP
TAP <tag>
```

Examples:

```text
GEN 3 | TAP
GEN 3 | TAP "PASS1" | ?[ @ > 1 ] | TAP "PASS2"
```

Errors:

- Used without an active pipeline item -> `TAP requires pipeline item`

### 8.4 `MORE`

Purpose:

- Pass current pipeline item onward unchanged
- Trigger the shell pause after every Nth item

Syntax:

```text
MORE
MORE <count>
```

Examples:

```text
1..50 | MORE
1..50 | MORE | PRT "HEY ", @
LST | MORE
1..9 | MORE 5
```

Notes:

- Default page size is `20`
- `MORE` is pipeline-oriented; by itself it does not print anything
- `MORE` pauses by setting the shared shell pause state, so the downstream output path stops until resumed

Errors:

- Zero, non-numeric, or multiple arguments -> `MORE expects count`

## 9. Script Blocks

Script blocks use `[ ... ]`.

Where used:

- Filter stage: `?[ ... ]`
- Foreach stage: `%[ ... ]`
- Also as expression form (evaluates to block's last statement value)

Script features:

- Multiple statements allowed
- Use `;` or newline separators
- Can read/write normal shell variables

Example:

```text
GEN 5 | ?[
  $LAST = @
  @ >= 3
] | PRT @
```

## 10. Worked Pipeline Recipes

Generate + filter + print:

```text
GEN 20 | ?[ @ <= 5 ] | PRT @
```

Capture pipeline result into variable:

```text
$TOP = GEN 10 | ?[ @ <= 4 ]
PRT $TOP
```

Per-item side effect plus pass-through:

```text
GEN 4 | %[ PRT "SEEN ", @ ] | ?[ @ > 2 ] | PRT @
```

Comparison-focused filtering:

```text
$A = 3
$B = 5
GEN 8 | ?[ @ <> $A ] | ?[ @ <= $B ] | PRT @
```

Nested arrays and indexing:

```text
$A = 1..5
$B = "HEY","THERE",$A
PRT $B
PRT $B(2)
PRT $B(2)(2)
```

Multiple statements with `;`:

```text
PRT "A";PRT "B";PRT "C"
```

Multiple statements inside a pipeline script:

```text
1,2,3 | %[ PRT "HEY ", @ ; $B = @ ]
PRT $B
```

Behavior:

- Script runs once per item
- `PRT "HEY ", @` prints per-item
- `$B = @` retains last item after the pipeline

## 11. Built-ins and Version

REPL built-ins (not pipeline commands):

- `HELP` -> concise command summary
- `VER` -> prints the shell core version string
- `CLEAR` -> redraws ReadyOS TUI shell screen

## 12. Errors and Diagnostics

Error format shown by shell:

```text
ERR[<code>] line=<line> col=<col>: <message>
```

Typical messages:

- `unknown command`
- `expected expression`
- `comparison requires numeric operands`
- `index must be numeric`
- `range bounds must be numeric`
- `variable table full`

## 13. More Examples

For a larger, example-heavy guide covering pipelines, filtering, foreach,
printing, disk commands, serialization, arrays, comparisons, `TOP`, and
`SEL`, see [ReadyShelltutorial.md](./ReadyShelltutorial.md).
