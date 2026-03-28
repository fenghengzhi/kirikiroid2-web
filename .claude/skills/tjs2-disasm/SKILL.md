---
name: tjs2-disasm
description: Disassemble compiled TJS2 bytecode files (.tjs) from KiriKiri2 game archives into human-readable VM instructions. Use when you need to understand game script logic, reverse-engineer compiled TJS2 bytecodes, analyze Config.tjs/MainWindow.tjs/Initialize.tjs behavior, find string references in compiled scripts, or trace function call flows in KiriKiri2 games. Also trigger when the user asks to "decompile TJS", "disassemble .tjs", "read compiled script", "dump bytecodes", or wants to understand what a compiled game script does.
---

# TJS2 Bytecode Disassembler

## What this does

Disassembles compiled TJS2 bytecode files (the `.tjs` files inside KiriKiri2 `.xp3` archives) into readable VM instruction listings. These files are NOT plain text — they're compiled bytecodes with the `TJS2100` header.

The output shows:
- All function/class/property contexts in the script
- VM instructions (cp, call, jf, jnf, tt, ceq, etc.)
- String constants, integer constants, object references
- Control flow (jumps, branches, try/catch blocks)

## Prerequisites

The `tjsdump` tool must be built first (native macOS/Linux binary, NOT Emscripten):

```bash
export VCPKG_ROOT=/Users/bytedance/vcpkg
cmake --preset "MacOS Release Config" -DBUILD_TOOLS=ON -DBISON_EXECUTABLE=/opt/homebrew/opt/bison/bin/bison
cmake --build out/macos/release --target tjsdump
```

Binary location: `tools/bin/mac/rel/tjsdump`

## Usage

### Basic disassembly

```bash
tools/bin/mac/rel/tjsdump /path/to/script.tjs
```

### Extract from XP3 first, then disassemble

```bash
# Extract all files from an xp3 archive
tools/bin/mac/rel/xp3 -o /tmp/extracted game.xp3

# Disassemble a specific script
tools/bin/mac/rel/tjsdump /tmp/extracted/game/system/MainWindow.tjs
```

### Search for specific strings in disassembly

```bash
# Find all references to "d3dMode" in MainWindow.tjs
tools/bin/mac/rel/tjsdump /path/to/MainWindow.tjs | grep "d3dMode"

# Dump to file for detailed analysis
tools/bin/mac/rel/tjsdump /path/to/MainWindow.tjs > /tmp/mainwindow_disasm.txt
```

### Find a specific function

```bash
# Find the initD3D function definition and its body
tools/bin/mac/rel/tjsdump /path/to/MainWindow.tjs | sed -n '/^(function) initD3D/,/^([a-z]/p'
```

## Reading the output

### Context headers
```
(top level script) global 0x...     ← top-level code
(function) initD3D 0x...            ← function definition
(class) KAGWindow 0x...             ← class definition
(property) isD3D 0x...              ← property getter/setter
```

### VM instructions
```
*N = (type)"value"    ← constant definition (string, int, object)
NN instruction args   ← VM opcode at address NN
```

Key opcodes:
- `cp %dst, %src` — copy register
- `call %result, %func(args)` — function call
- `tt %reg` / `tf %reg` — test true / test false
- `jf addr` / `jnf addr` — jump if false / jump if not false
- `jmp addr` — unconditional jump
- `ceq %a, %b` — compare equal
- `cgt %a, %b` — compare greater than
- `entry addr, %reg` — try block entry (catch at addr)
- `extry` — exit try block
- `new %result, %class(args)` — create new object
- `chgthis %func, %obj` — bind function to object
- `gpi %result, %obj.%prop` — get property indirect
- `spi %obj.%prop, %value` — set property indirect
- `cl %reg` — clear register (set to void)
- `srv %reg` — set return value
- `ret` — return

### Register conventions
- `%-N` — local variables / function parameters (negative = locals)
- `%N` — temporary registers (positive = temps)
- `%-1` is typically `this`

## Common analysis patterns

### Find what conditions gate a code path
```bash
# Example: what controls D3D initialization?
tjsdump MainWindow.tjs | grep -B5 -A10 "DrawDeviceD3D"
```

### Trace function call chain
```bash
# Find all functions that reference "isD3D"
tjsdump MainWindow.tjs | grep -n "isD3D" | head -20
```

### Find where a script is loaded
```bash
# Search all scripts for references to "D3D.tjs"
for f in /tmp/extracted/**/*.tjs; do
  result=$(tjsdump "$f" 2>&1 | grep "D3D.tjs" | head -1)
  [ -n "$result" ] && echo "=== $(basename $f) ===" && echo "$result"
done
```
