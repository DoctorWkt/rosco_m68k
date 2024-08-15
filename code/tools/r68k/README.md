# r68k

`r68k` provides two forms of emulation:

1. It emulates many of the ROM system calls (traps 13, 14 and 15).
   Using the firmware in the `firmware/` directory, you can run
   native rosco_m68k binaries on your modern computer: examples
   are the binaries created in the `../../software` sub-directories
   such as 2dmaze, adventure, dhrystone, easy68k-demo, ehbasic, life,
   memcheck, sdfat_menu and vterm.

2. `r68k` also emulates the DUART hardware including the 100Hz clock
   tick and the SPI bit-banged SD card. Using the real `rosco_m68k.rom`
   you can boot the system up as if it was the real hardware.

## Build it

You'll need the rosco_m68k toolchain (or other m68k toolchain of
your choosing) installed, as well as build tools for your platform.
You will also need the `readline` library installed. Now do:

```
make clean all
```

## Running Native rosco_m68k Binaries

Once `r68k` is built, you can run one of the example executables. For
example, assuming you have gone into `../../software/life` and built
`life.bin`, then you can:

```
$ ./r68k ../../software/life/life.bin
```

and run the Game of Life in your terminal.

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
0x80   I/O Access operations
```

Unless specified, the default debug flags will disassemble instructions and
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

## Emulating the Real Hardware

All of the above assumes that you are using the firnware in the
`firmware/` directory which sends system calls to `r68k`.

If, however, you want to run an emulation of real hardware, then
you should invoke `r68k` with the real rosco_m68k ROM and with
a bootable SD card image, e.g.

```
$ ./r68k -R ../../firmware/rosco_m68k_firmware/rosco_m68k.rom -S sdcard.img
```

You should then see:

```
                                 ___ ___ _   
 ___ ___ ___ ___ ___       _____|  _| . | |_ 
|  _| . |_ -|  _| . |     |     | . | . | '_|
|_| |___|___|___|___|_____|_|_|_|___|___|_,_|
                    |_____|  Classic 2.50.DEV

MC68020 CPU @ 6.5MHz with 1048576 bytes RAM
Initializing hard drives... No IDE interface found
Searching for boot media...
SDHC card:
  Partition 1: Loading "/ROSCODE1.BIN"........
Loaded 32668 bytes in ~2 sec.
```

followed by the execution of whatever binary you placed on the SD card
with the name `ROSCODE1.BIN`.

## Example Script to Build a Bootable SD Card

Here is the script that I use to make a bootable SD card. Run it from
the `../../software` directory.

```
#!/bin/sh
image=sdcard.img
size=36                         # in Megabytes
dd if=/dev/zero of=$image bs=1M count=$size
/sbin/parted $image mklabel msdos
/sbin/parted $image mkpart primary fat32 1MB 100%
/sbin/parted $image print
/sbin/mkfs.vfat -F 32 --offset=2048 $image
for i in `find . -name '*.bin'`
do echo $i
   mcopy -i "$image"@@1M $i ::
done
mcopy -i "$image"@@1M sdfat_menu/sdfat_menu.bin ::RosCode1.bin
mdir -i "$image"@@1M
```

You will need the `parted`, `dosfstools` and `mtools` packages installed
on your Linux box.

