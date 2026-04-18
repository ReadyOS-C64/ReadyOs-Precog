# ReadyShell Tutorial

ReadyShell is a small expression language plus a streaming pipeline shell.
It works well for quick transformations, directory queries, saved snapshots,
and lightweight scripting inside ReadyOS.

This tutorial is example-first. Every example is written in ReadyShell syntax.

## Quick Notes

- Commands and variable names are case-insensitive.
- On the C64 keyboard, type `!` to enter the pipeline operator `|`.
- `PUT` and `ADD` use direct `COMMAND <expr>, <filename>` form. They are not
  pipeline consumer stages.
- When an assignment captures pipeline output:
  - `0` items becomes `FALSE`
  - `1` item becomes that item directly
  - `2+` items become an array
- `SEL "NAME"` returns the property value itself for each item, so directory
  entries become plain strings.
- `SEL "NAME", "TYPE"` returns a new object containing only those properties.
- Object values usually come from commands like `LST` and `DRVI`.
- Missing properties and out-of-range indexes evaluate to `FALSE`.

## Quick Start

```ruby
VER
PRT "HELLO"
1..5
1..5 | ?[ @ > 3 ]
$A = 1..10
PRT $A(4)
```

## Values and Expressions

### Numbers, strings, booleans

```ruby
42
"READY"
TRUE
FALSE
```

### Variables

```ruby
$A = 5
$B = "READY"
PRT $A
PRT $B
```

### Arrays from comma expressions

```ruby
$A = 10,20,30
PRT $A
PRT $A(0)
PRT $A(2)
$A(2)
```

### Ranges

```ruby
PRT 1..5
PRT 5..1
$R = 1..8
PRT $R(3)
```

### Nested values

```ruby
$A = 1..5
$B = "HEAD","TAIL",$A
PRT $B(0)
PRT $B(2)
$B(2)(4)
```

## Comparisons

Numeric comparisons:

```ruby
PRT 5 > 3
PRT 5 >= 5
PRT 2 < 1
PRT 9 <> 4
```

String equality is case-insensitive:

```ruby
PRT "yo" == "YO"
PRT "PRG" <> "SEQ"
```

Comparison-driven filtering:

```ruby
$A = 3
$B = 5
1..8 | ?[ @ <> $A ] | ?[ @ <= $B ]
```

## Printing

`PRT` concatenates its arguments exactly as formatted:

```ruby
PRT "X=", 5
PRT "NAME=", "alpha", " TYPE=", "PRG"
```

Inside pipelines, `PRT` prints the current item when called with `@`:

```ruby
1..3 | PRT @
```

If a pipeline does **not** end in `PRT`, the final emitted items auto-print:

```ruby
GEN 3
GEN 3 | @
GEN 3 | PRT @
```

## Pipelines

Pipelines stream one item at a time:

```ruby
GEN 5 | ?[ @ > 2 ] | PRT @
```

Expression stages can transform each item:

```ruby
GEN 5 | @ > 2
GEN 5 | @ <= 3
```

Assignments can capture pipeline output:

```ruby
$TOP = GEN 10 | ?[ @ <= 4 ]
PRT $TOP
```

## Filtering with `?[ ... ]`

Filters keep only items whose script result is truthy.

Simple numeric filter:

```ruby
GEN 10 | ?[ @ > 7 ]
```

Filter with multiple statements:

```ruby
GEN 5 | ?[ $LAST = @; @ >= 3]
```

Filter on object properties:

```ruby
LST | ?[ @.TYPE == "PRG" ] | SEL "NAME"
LST | ?[ @.BLOCKS > 10 ] | SEL "NAME","BLOCKS"
```

## Foreach with `%[ ... ]`

Foreach runs a script for each item and then passes the original item onward.

Per-item logging:

```ruby
1..3 | %[ PRT "ITEM ", @ ]
```

Side effects plus later filtering:

```ruby
1..5 | %[ $LAST = @; PRT "SEEN ", @] | ?[ @ > 3 ]
```

Directory walk with inspection:

```ruby
LST | %[ PRT "NAME=", @.NAME, " BLOCKS=", @.BLOCKS] | ?[ @.TYPE == "PRG" ] | SEL "NAME"
```

## Arrays, Indexing, and Properties

Index arrays with parentheses:

```ruby
$A = 100,200,300
PRT $A(1)
$A(9)
```

Objects come from shell commands:

```ruby
$INFO = DRVI
PRT $INFO.DRIVE
$INFO.DISKNAME
```

Directory entries are objects inside arrays:

```ruby
$DIR = LST
PRT $DIR(0).NAME
PRT $DIR(1).TYPE
$DIR(2).BLOCKS
```

Chaining index and property access:

```ruby
$DIR = LST
PRT $DIR(1).NAME
PRT $DIR(1).MISSING
```

## `TOP`

`TOP` keeps only the first `count` items in a pipeline.

```ruby
1..10 | TOP 5
LST | TOP 3
```

`TOP count,skip` skips some items first:

```ruby
1..10 | TOP 3,2
LST | TOP 3,1
LST | TOP 2,1 | SEL "NAME"
```

## `SEL`

`SEL` projects properties out of object pipeline items.

Select one property. Each pipeline item becomes just that property value:

```ruby
LST | SEL "NAME"
LST | SEL "BLOCKS"
$NAMES = LST | TOP 3 | SEL "NAME"
PRT $NAMES(0)
```

Select several properties. Each pipeline item becomes a new object containing
just those fields:

```ruby
LST | SEL "NAME","TYPE"
LST | SEL "NAME","BLOCKS"
$ROWS = LST | TOP 3 | SEL "NAME","TYPE"
PRT $ROWS(0).NAME
```

Combine with filters:

```ruby
LST | ?[ @.TYPE == "SEQ" ] | SEL "NAME","TYPE"
LST | ?[ @.BLOCKS >= 20 ] | SEL "NAME","BLOCKS"
```

## Disk Commands

### `DRVI`

Drive info defaults to drive `8` when no argument is supplied:

```ruby
DRVI
DRVI 9
DRVI | SEL "DRIVE","DISKNAME"
```

### `LST`

Directory listing also defaults to drive `8`:

```ruby
LST
LST 9
LST "t*"
LST "t?"
LST "9:t*"
LST "*.PRG", "PRG"
LST "t*", 9, "PRG,SEQ"
LST | SEL "NAME"
LST | SEL "NAME","TYPE"
```

Pattern strings use normal CBM wildcards such as `*` and `?`. Type filters are
case-insensitive and accept comma-separated values such as `PRG`, `SEQ`, `USR`,
`REL`, `DIR`, `CBM`, and `DEL`.

Use `LST` as data:

```ruby
$DIR = LST
PRT $DIR(0).NAME
$PRGS = LST | ?[ @.TYPE == "PRG" ]
PRT $PRGS(0).NAME
$NAMES = LST | TOP 3 | SEL "NAME"
PRT $NAMES(1)
```

## Saving and Loading Values

`STV` serializes a value to disk.

Save a range result:

```ruby
$A = 1..20
STV $A, "range20"
STV $A, "range20", 9
```

Save a filtered directory snapshot:

```ruby
$DIR = LST | ?[ @.TYPE == "PRG" ] | TOP 10
STV $DIR, "prgdir"
STV $DIR, "9:prgdir"
STV $DIR, "prgdir", 9
```

Load it back later:

```ruby
$SNAP = LDV "prgdir"
$BACKUP = LDV "9:prgdir"
$ALT = LDV "prgdir", 9
PRT $SNAP(0).NAME
PRT $SNAP(0).TYPE
```

Load a scalar snapshot:

```ruby
STV 42, "answer"
STV 42, "answer", 9
$ANSWER = LDV "answer"
$BACKUP = LDV "answer", 9
PRT $ANSWER
```

The `RSV1` serialized value format used by `STV` and `LDV` is documented in
`docs/readyshell_rsv1_format.md`.

Loading a missing file yields `FALSE`:

```ruby
$MISSING = LDV "doesnotexist"
PRT $MISSING
```

## Text File Commands

### `CAT`

`CAT` reads a PETASCII text file and emits one string per line:

```ruby
CAT "notes"
CAT "9:readme"
CAT "notes" | MORE | PRT @
```

### `PUT`

`PUT` creates or replaces a PETASCII `SEQ` file. Strings write one line;
arrays write one line per element:

```ruby
PUT "HELLO", "notes"
$LINES = "HEY", "THERE"
PUT $MSG, "notes"
PUT $LINES, "dirnames"
```

Notes:
- each written line is CR-terminated on disk
- `PUT` currently accepts strings and arrays of strings

### `ADD`

`ADD` appends to an existing PETASCII `SEQ` file. Strings append one line;
arrays append one line per element. If the file is missing, `ADD` creates it:

```ruby
$MORELINES = "ONE", "TWO"
ADD $NEXT, "notes"
ADD $MORELINES, "dirnames"
```

Notes:
- each appended line is CR-terminated on disk
- existing targets must be `SEQ`
- `ADD` currently accepts strings and arrays of strings

### `DEL`

`DEL` scratches a file and returns a boolean:

```ruby
DEL "notes"
DEL "9:notes"
```

### `REN`

`REN` renames a file on one drive:

```ruby
REN "notes", "notes.old"
REN "notes", "notes.old", 9
```

### `COPY`

`COPY` duplicates a file, either to a new name or a new drive:

```ruby
COPY "notes", "notes.bak"
COPY "8:notes", "9:notes"
COPY "notes", 9
```

## Useful Recipes

Keep only the first few large files:

```ruby
LST | ?[ @.BLOCKS > 10 ] | TOP 5 | SEL "NAME","BLOCKS"
```

Capture a reusable working set:

```ruby
$WORK = LST | ?[ @.TYPE == "PRG" ] | SEL "NAME","BLOCKS"
PRT $WORK(0).NAME
```

Inspect and save the same stream:

```ruby
$SNAP = LST | %[PRT "SEEN ", @.NAME] | ?[ @.TYPE == "PRG" ] | TOP 8
STV $SNAP, "seenprgs"
```

Reload and continue filtering:

```ruby
$SNAP = LDV "seenprgs"
$SNAP | ?[ @.BLOCKS >= 20 ] | SEL "NAME","BLOCKS"
```

Build a boolean mask stream:

```ruby
1..8 | %[ PRT "VALUE ", @ ] | @ > 4
```

Use multiple statements on one line:

```ruby
$A = 3; $B = 5; 1..8 | ?[ @ <> $A ] | ?[ @ <= $B ]
```

Build a text snapshot from the directory:

```ruby
$DIRNAMES = ["READYOS", "README", "LAUNCHER"]
PUT $DIRNAMES, "dirnames"
CAT "dirnames" | MORE | PRT @
```

Append more lines to the same file:

```ruby
$MORE = "RSFOPS", "RSCAT"
ADD $MORE, "dirnames"
CAT "dirnames" | MORE | PRT @
```

## A Small Session

```ruby
VER
$DIR = LST
PRT "FIRST=", $DIR(0).NAME
$BIG = $DIR | ?[ @.BLOCKS > 10 ] | SEL "NAME","BLOCKS"
PRT $BIG
STV $BIG, "bigfiles"
$RESTORE = LDV "bigfiles"
PRT $RESTORE(0).NAME
```

That covers the core ReadyShell workflow: inspect data, filter it, project it,
save it, reload it, and keep working with it as arrays and objects.

For the compact on-disk quick reference, use:

```ruby
HELP
CAT "RSHELP" | MORE
```
