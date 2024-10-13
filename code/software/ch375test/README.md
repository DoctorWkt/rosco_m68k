## ch375test

This code tests the interface to the CH375 USB block
device by initialising it, reading one block and
writing another block.

The expansion board with the CH375 interface is documented
[here](https://github.com/DoctorWkt/xv6-rosco-r2/tree/ch375/hardware).

The CH375 lives at these I/O addresses:

```
; CH375 I/O addresses
CHDATARD  equ   $FFF001
CHDATAWR  equ   $FFF001
CHCMDWR   equ   $FFF003
```

and generate an auto-vectored IRQ5 interrupt.
Warren's [rewritten `r68k` emulator](https://github.com/DoctorWkt/rosco_m68k/tree/wkt_r68k/code/tools/r68k)
has a working emulation of the CH375 device at these addresses.

You can test the code as follows:

 - compile the code to make `ch375test`
 - put this file on an SD card image as the file `ROSCODE1.bin`
 - build the real `rosco_v2` ROM image:
   `../../firmware/rosco_m68k_firmware/rosco_m68k.rom`
 - make a pretend USB disk image of 1,024 bytes of so, e.g.
   `$ cat kmain.c > usb.img`
 - build Warren's `r68k` in `../../tools/r68k`

Putting it all together, run the emulator with the ROM,
the SD card and the USB image:

```
$ r68k -R rosco_m68k.rom -S sdcard.img -U usb.img
```

You will probably need to change the filenames to be
the ones that you have, i.e. add a few `../../` etc.

You should see the following:

```

                                 ___ ___ _   
 ___ ___ ___ ___ ___       _____|  _| . | |_ 
|  _| . |_ -|  _| . |     |     | . | . | '_|
|_| |___|___|___|___|_____|_|_|_|___|___|_,_|
                    |_____|  Classic 2.50.DEV

MC68020 CPU @ 7.9MHz with 1048576 bytes RAM
Initializing hard drives... No IDE interface found
Searching for boot media...
SDHC card:
  Partition 1: Loading "/ROSCODE1.BIN".
Loaded 5828 bytes in ~1 sec.

About to initialise the CH375
After set USB mode, status is 0x15
After disk init, status is 0x14
After disk size, status is 0x14
8 bytes to read following the disk size cmd
Ths disk has 12 blocks each sized 512
Block zero read OK
// Code to test the CH375 device
// (c) 2024 Warren Toomey GPL3

#include <stdio.h>
#include <stdint.h>

// These are in the asmcode.asm file
extern void cpu_delay(int ms);
extern void irq3_install();
extern void send_ch375_cmd(uint8_t cmd);
extern void send_ch375_data(uint8_t cmd);
extern uint8_t read_ch375_data(void);
extern uint8_t get_ch375_status(void);

// The buffer for I/O
uint8_t buf[512];

// CH375 commands
#define CMD_RESET_ALL    	0x05
#define CMD_SET_USB_MODE 	0x15
#define CMD_GET_STATUS   	0x2
About to write block one
Block one write OK

CH375 test complete, looping ...
```
