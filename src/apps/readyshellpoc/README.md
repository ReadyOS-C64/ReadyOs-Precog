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
  - `CAT`
  - `PUT`
  - `ADD`
  - `DEL`
  - `REN`
  - `COPY`
- REPL built-ins:
  - `HELP`
  - `VER`
  - `CLEAR`

Still intentionally out of scope in this POC:

- Full reference-shell command set beyond the commands listed here

## 2. Shell Input and Editing

Prompt:

- `> `

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
- Resident BSS: `$8769-$895F` (`503` bytes)
- Resident heap: `$8960-$8DFD` (`1182` bytes)
- High RAM runtime outside the app snapshot: `$CA00-$CFFF`

Overlay policy:

- All eight overlays are boot-preloaded into fixed REU cache slots during shell startup
- Bank `0x40` holds overlays `1`, `2`, `3`, and `5`
- Bank `0x41` holds overlays `4`, `6`, `7`, and `8`
- Each cache slot stores the full overlay window size, not just file bytes
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
| overlay 3 rsdrvilst slot      |
+-------------------------------+ 0x40A800
| overlay 5 rsstv slot          |
+-------------------------------+ 0x40E000
| free tail in bank 0x40        |
+-------------------------------+ 0x40FFFF

bank 0x41
+-------------------------------+ 0x410000
| overlay 4 rsldv slot          |
+-------------------------------+ 0x413800
| overlay 6 rsfops slot         |
+-------------------------------+ 0x417000
| overlay 7 rscat slot          |
+-------------------------------+ 0x41A800
| overlay 8 rscopy slot         |
+-------------------------------+ 0x41E000
| free tail in bank 0x41        |
+-------------------------------+ 0x41FFFF
```

Current overlay set:

- `OVERLAY1` `rsparser.prg`: parser / lexer, `13005` live bytes, cached in bank `0x40` parse slot
- `OVERLAY2` `rsvm.prg`: execution core for `PRT`, `MORE`, `TOP`, `SEL`, `GEN`, `TAP`, `14033` live bytes, cached in bank `0x40` exec slot
- `OVERLAY3` `rsdrvilst.prg`: `DRVI` + `LST`, cached in bank `0x40`
- `OVERLAY4` `rsldv.prg`: `LDV`, cached in bank `0x41`
- `OVERLAY5` `rsstv.prg`: `STV`, cached in bank `0x40`
- `OVERLAY6` `rsfops.prg`: `DEL` + `REN` + `PUT` + `ADD`, cached in bank `0x41`
- `OVERLAY7` `rscat.prg`: `CAT`, cached in bank `0x41`
- `OVERLAY8` `rscopy.prg`: `COPY`, cached in bank `0x41`

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

- Command stage: `PRT ...`, `GEN ...`, `MORE ...`, `TOP ...`, `SEL ...`, `CAT ...`, ...
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

### 8.5 `TOP`

Purpose:

- Keep the first `count` items from a stream
- Optionally skip some items first

Syntax:

```text
TOP <count>
TOP <count>, <skip>
```

Examples:

```text
GEN 10 | TOP 4
LST | TOP 3
LST | TOP 2,1 | SEL "NAME"
```

Notes:

- `TOP 3,1` skips the first item, then emits the next three
- `TOP` is a pass-through filter stage; it does not reformat items

### 8.6 `SEL`

Purpose:

- Project one or more object properties out of each pipeline item

Syntax:

```text
SEL <name>
SEL <name>, <name>, ...
```

Examples:

```text
LST | SEL "NAME"
LST | SEL "NAME","BLOCKS"
DRVI | SEL "DRIVE","DISKNAME"
```

Notes:

- Selecting one property emits that property value directly
- Selecting several properties emits a new object containing just those fields

### 8.7 `DRVI`

Purpose:

- Return drive information as an object

Syntax:

```text
DRVI
DRVI <drive>
```

Examples:

```text
DRVI
DRVI 9
DRVI | SEL "DRIVE","DISKNAME"
```

Notes:

- Defaults to the current/default drive when no argument is supplied

### 8.8 `LST`

Purpose:

- Return a streamed directory listing as objects

Syntax:

```text
LST
LST <drive>
```

Examples:

```text
LST
LST 9
LST | SEL "NAME","TYPE"
```

Notes:

- Each emitted item is an object with fields such as `NAME`, `TYPE`, and `BLOCKS`
- `LST` is useful as a data source for `SEL`, `TOP`, filters, and saved snapshots

### 8.9 `LDV`

Purpose:

- Load a previously saved ReadyShell value from disk

Syntax:

```text
LDV <filename>
```

Examples:

```text
$SNAP = LDV "bigfiles"
PRT $SNAP(0).NAME
```

### 8.10 `STV`

Purpose:

- Save a ReadyShell value to disk

Syntax:

```text
STV <expr>, <filename>
```

Examples:

```text
$DIR = LST | ?[ @.TYPE == "PRG" ] | TOP 10
STV $DIR, "prgdir"
```

### 8.11 `CAT`

Purpose:

- Read a PETASCII text file and emit one string per line

Syntax:

```text
CAT <filename>
```

Examples:

```text
CAT "notes"
CAT "9:readme"
CAT "todo" | MORE | PRT @
```

Notes:

- If the filename does not embed a drive, ReadyShell uses the current/default drive
- `CAT` closes the file after the final line and emits no extra success value

### 8.12 `PUT`

Purpose:

- Consume streamed strings and write a new PETASCII text file

Syntax:

```text
PUT <filename>
```

Examples:

```text
"HELLO" | PUT "notes"
LST | SEL "NAME" | PUT "dirnames"
```

Notes:

- `PUT` creates or replaces the target file
- `PUT` produces no output on success

### 8.13 `ADD`

Purpose:

- Consume streamed strings and append them to a PETASCII text file

Syntax:

```text
ADD <filename>
```

Examples:

```text
"NEXT LINE" | ADD "notes"
LST | SEL "NAME" | ADD "dirnames"
```

Notes:

- `ADD` appends line-terminated PETASCII text
- `ADD` produces no output on success

### 8.14 `DEL`

Purpose:

- Delete a file and return a boolean result

Syntax:

```text
DEL <filename>
```

Examples:

```text
DEL "notes"
DEL "9:notes"
```

Notes:

- Returns `TRUE` on success and `FALSE` on failure
- Uses the current/default drive when the filename does not embed one

### 8.15 `REN`

Purpose:

- Rename a file on one drive and return a boolean result

Syntax:

```text
REN <original>, <destination>
REN <original>, <destination>, <drive>
```

Examples:

```text
REN "notes", "notes.old"
REN "notes", "notes.old", 9
```

Notes:

- For `REN`, names do not embed the drive
- The optional drive applies to both names

### 8.16 `COPY`

Purpose:

- Copy a file and return a boolean result

Syntax:

```text
COPY <source>, <destination>
COPY <source>, <drive>
```

Examples:

```text
COPY "notes", "notes.bak"
COPY "8:notes", "9:notes"
COPY "notes", 9
```

Notes:

- Strings may embed drive numbers, such as `8:notes`
- Same-drive copies must use a different destination name

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
printing, disk commands, file commands, serialization, arrays, comparisons,
`TOP`, and `SEL`, see [ReadyShelltutorial.md](./ReadyShelltutorial.md).
