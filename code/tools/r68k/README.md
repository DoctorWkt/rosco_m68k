# r68k

Run native rosco_m68k binaries on your modern computer.

> **Note**: This is not a complete "emulation" or translation layer
> for a full rosco_m68k system. It implements enough of the system
> to run many general purpose programs, and is great for testing /
> iterating on a thing without having to upload to a real machine. 

## Build it

You'll need the rosco_m68k toolchain (or other m68k toolchain of
your choosing) installed, as well as build tools for your platform.
You will also need the `readline` library installed. Now do:

```
make clean all
```

## Run it

Once `r68k` is built, you can run one of the example executables. For
example, assuming you have gone into `code/software/life` and built
`life.bin`, then you can:

```
./r68k ../../software/life/life.bin
```

and run the Game of Life in your terminal.

These example programs are known to work: 2dmaze, adventure, dhrystone,
easy68k-demo, ehbasic, life, memcheck, sdfat_menu and vterm.

## Emulator Usage

```
Usage: ./r68k [flags] executable_file

Flags are:
  -L logfile            Log debug info to this file
  -M mapfile            Load symbols from a map file
  -R romfile            Use the file as the ROM image
  -S sdcardfile         Attach SD card image file
  -b addr [-b addr2]    Set breakpoint(s) at symbol or dec/$hex addr
  -l value              Set dec/$hex bitmap of debug flags
  -m                    Start in the monitor
```

The available debug flags are:

```
0x01   Memory accesses
0x02   Invalid memory accesses
0x04   Instruction disassembly
0x08   Register dump
0x10   Illegal instruction handler
0x20   Interrupt acknowledge handler
0x40   SD Card operations
```

Unless specified, the default flags will disassemble instructions and
dump the registers after each instruction.

If not specified, the default ROM image is `firmware/rosco_m68k.rom`,
i.e. a file relative to the location of the `r68k` program.
There is no default SD card filename.

If your executable was linked using `m68k-elf-ld -T -Map=<name>.map ...`
to create a map file with symbols and addresses, then you can use the
`-M` command-line flag to load these symbols and associated addresses.

You can set breakpoints from the command line, e.g.

```
  -b 23          Decimal
  -b '$1000'     Hexadecimal
  -b _foo        Start of function foo()
  -b _foo+23     An address within foo()
  -b '_foo+$100' An address within foo()
```

The above examples show the use of symbols loaded from a map file.
Note that hexadecimal values given on the command-line need to be in
quotes to stop the shell from interpreting the '$' symbol.

## Monitor Instructions

```
Monitor usage:

s, step <num>             - execute 1 or <num> instructions
x, exit                   - exit the monitor, back to running
q, quit                   - quit the emulation
g, go <addr>              - start execution at address
p, print <addr> [<addr2>] - dump memory in the address range
d, dis <addr> [<addr2>]   - disassemble memory in the address range
w, write <addr> <value>   - overwrite memory with value
b, brk [<addr>]           - set instruction breakpoint at <addr> or
                            show list of breakpoints
wb, wbrk <addr>           - set a write breakpoint at <addr>
nb, nbrk [<addr>]         - remove breakpoint at <addr>, or all

Addresses and Values

Decimal literals start with [0-9], e.g. 23
Hexadecimal literals start with $, e.g. $1234
Symbols start with _ or [A-Za-z], e.g. _printf
Symbols + offset, e.g. _printf+23, _printf+$100
```
