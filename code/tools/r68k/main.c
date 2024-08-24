// Command-line handling, system initialisation
// and memory decoding for the r68k emulator.

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <termios.h>
#include <err.h>
#include "musashi/m68k.h"
#include "musashi/m68kcpu.h"
#include "devices.h"
#include "loglevel.h"
#include "mapfile.h"
#include "monitor.h"
#include "sdcard.h"

// Global variables
uint8_t *g_ram;			    // RAM memory
uint8_t *g_rom;			    // ROM memory
uint8_t zerolong[4]= {0, 0, 0, 0};  // 4 bytes of zeroes
FILE    *logfh= NULL;		    // Logging filehandle
int	loglevel= 0;		    // Log level
char    *ch375file= NULL;	    // CH375 file name

// Static variables
static char *romfile= "firmware/rosco_m68k.rom";

// If 1, we hit a write breakpoint
static unsigned write_brkpt= 0;

// External variables
extern char *sdfile;		// SD card file
extern FILE *ifs;		// File handle for this

// Close the log file if it is open
void close_logfile() {
  if (logfh!=NULL) {
    fflush(logfh);
    fclose(logfh);
  }
}

// Given a filename and a base address, open and
// load the binary data in the file at that address
void ReadBinaryData(const char *filename, uint8_t * base) {
  FILE *in;
  int cnt;

  in = fopen(filename, "r");
  if (in == NULL) errx(EXIT_FAILURE, "Cannot open %s", filename);
  while (1) {
    cnt = fread(base, 1, 4096, in);
    if (cnt <= 0) break;
    base += cnt;
  }
  fclose(in);
}

// Size of ROM/RAM and their locations
// in the MC68010 address space. Also
// the base of the I/O area.
#define RAM_SIZE	1024 * 1024
#define RAM_BASE	0x00000000
#define ROM_SIZE	1024 * 1024
#define ROM_BASE	0x00e00000

#define IO_BASE		0x00f00000

// Allocate memory for ROM and RAM, and
// read in the ROM image.
void initialise_memory(const char *romfilename) {
  g_rom = (uint8_t *) calloc(1, ROM_SIZE);
  if (g_rom == NULL) err(EXIT_FAILURE, NULL);
  g_ram = (uint8_t *) calloc(1, RAM_SIZE);
  if (g_ram == NULL) err(EXIT_FAILURE, NULL);

  if (logfh != NULL && (loglevel & LOG_MEMACCESS) == LOG_MEMACCESS) {
    fprintf(logfh, "Initialized with %d bytes RAM and %d bytes ROM\n",
						RAM_SIZE, ROM_SIZE);
  }

  ReadBinaryData(romfilename, g_rom);

  // Copy eight bytes from the start of ROM to RAM
  // to give the CPU the initial PC and SP values
  memcpy(g_ram, g_rom, 8);
}

// Given an address, return a pointer to the
// location in g_ram or g_rom which is the
// base of this address
uint8_t * baseAddress(uint32_t address) {
  // Note: -Wall complains about if (address >= RAM_BASE
  // buf if RAM_BASE is ever >0 then we should put it back in.
  if (address < RAM_BASE + RAM_SIZE) {
    if (logfh != NULL && (loglevel & LOG_MEMACCESS) == LOG_MEMACCESS) {
      fprintf(logfh, "RAM Relative address is: 0x%x\n", address);
    }
    return(&(g_ram[address]));
  } else if (address >= ROM_BASE && address < ROM_BASE + ROM_SIZE) {
    if (logfh != NULL && (loglevel & LOG_MEMACCESS) == LOG_MEMACCESS) {
      fprintf(logfh, "ROM Relative address is: 0x%x\n", address - ROM_BASE);
    }
    return(&(g_rom[address - ROM_BASE]));
  } else {
    if (logfh != NULL && (loglevel & LOG_BUSERROR) == LOG_BUSERROR) {
      fprintf(logfh, "BUSERROR at address 0x%X\n", address);
    }
    // Return a pointer to a word full of zeroes
    return(zerolong);
  }

  return(NULL);		// Keep -Wall happy
}

// Read/write macros
#define READ_BYTE(BASE) (BASE)[0]
#define READ_WORD(BASE) (((BASE)[0]<<8)  | (BASE)[1])
#define READ_LONG(BASE) (((BASE)[0]<<24) | ((BASE)[1]<<16) | \
                         ((BASE)[2]<<8)  | (BASE)[3])

#define WRITE_BYTE(BASE, VAL) (BASE)[0] = (VAL)       & 0xff
#define WRITE_WORD(BASE, VAL) (BASE)[0] = ((VAL)>>8)  & 0xff; \
                              (BASE)[1] = (VAL)       & 0xff
#define WRITE_LONG(BASE, VAL) (BASE)[0] = ((VAL)>>24) & 0xff; \
                              (BASE)[1] = ((VAL)>>16) & 0xff; \
                              (BASE)[2] = ((VAL)>>8)  & 0xff; \
                              (BASE)[3] = (VAL)       & 0xff

// Functions to read data from memory. It used to be so
// neat and tidy before we implemented the I/O space, sigh!
unsigned int cpu_read_byte(unsigned int address) {
  uint8_t *base;

  if (address >= IO_BASE) return(io_read_byte(address));
  base= baseAddress(address);
  return READ_BYTE(base);
}

unsigned int cpu_read_word(unsigned int address) {
  uint8_t *base= baseAddress(address);

  if (address >= IO_BASE) return(io_read_word(address));
  base= baseAddress(address);
  return READ_WORD(base);
}

unsigned int cpu_read_long(unsigned int address) {
  uint8_t *base;

  if (address >= IO_BASE) return(io_read_long(address));
  base= baseAddress(address);
  return READ_LONG(base);
}

unsigned int cpu_read_word_dasm(unsigned int address) {
  uint8_t *base= baseAddress(address);
  return READ_WORD(base);
}

unsigned int cpu_read_long_dasm(unsigned int address) {
  uint8_t *base= baseAddress(address);
  return READ_LONG(base);
}

// Write data to memory
void cpu_write_byte(unsigned int address, unsigned int value) {
  uint8_t *base;

  if (address >= IO_BASE) { io_write_byte(address, value); return; }
  base= baseAddress(address);
  if (address >= RAM_BASE + RAM_SIZE) {
    if (logfh != NULL && (loglevel & LOG_BUSERROR) == LOG_BUSERROR) {
      fprintf(logfh, "Attempted to write %02x to RAM address %08x\n",
					value & 0xff, address);
    }
  } else {
    WRITE_BYTE(base, value);
    if (is_breakpoint(address, BRK_WRITE)) {
          write_brkpt= 1;
          printf("Write at $%04X\n", address);
    }
  }
}

void cpu_write_word(unsigned int address, unsigned int value) {
  uint8_t *base;

  if (address >= IO_BASE) { io_write_word(address, value); return; }
  base= baseAddress(address);
  if (address >= RAM_BASE + RAM_SIZE) {
    if (logfh != NULL && (loglevel & LOG_BUSERROR) == LOG_BUSERROR) {
      fprintf(logfh, "Attempted to write %04x to RAM address %08x\n",
						value & 0xffff, address);
    }
  } else {
    WRITE_WORD(base, value);
    if (is_breakpoint(address, BRK_WRITE)) {
          write_brkpt= 1;
          printf("Write at $%04X\n", address);
    }
  }
}

void cpu_write_long(unsigned int address, unsigned int value) {
  uint8_t *base;

  if (address >= IO_BASE) { io_write_long(address, value); return; }
  base= baseAddress(address);
  if (address >= RAM_BASE + RAM_SIZE) {
    if (logfh != NULL && (loglevel & LOG_BUSERROR) == LOG_BUSERROR) {
      fprintf(logfh, "Attempted to write %08x to RAM address %08x\n",
							value, address);
    }
  } else {
    WRITE_LONG(base, value);
    if (is_breakpoint(address, BRK_WRITE)) {
          write_brkpt= 1;
          printf("Write at $%04X\n", address);
    }
  }
}

// Called when the CPU pulses the RESET line
void cpu_pulse_reset(void) {
}

// Set a timer to expire in 0.01 seconds
void set_timer() {
  struct itimerval itv;

  itv.it_interval.tv_sec = 0;
  itv.it_interval.tv_usec = 0;
  itv.it_value.tv_sec = 0;
  itv.it_value.tv_usec = 1000;
  setitimer(ITIMER_REAL, &itv, NULL);
}

// The periodic timer handler
void timer_interrupt() {

  m68k_set_irq(DUART_IRQ);
  set_timer();
}

// Attach the timer_interrupt() to SIGALRM
void attach_sigalrm() {
  struct sigaction sa;

  sa.sa_handler= timer_interrupt;
  sa.sa_flags= 0;
  sigemptyset(&(sa.sa_mask));
  if (sigaction(SIGALRM, &sa, NULL)==-1) {
    warn("Unable to attach a SIGALRM handler");
  }
}

// Detach the timer_interrupt() from SIGALRM
void detach_sigalrm() {
  struct sigaction sa;

  sa.sa_handler= SIG_IGN;
  sa.sa_flags= 0;
  sigemptyset(&(sa.sa_mask));
  if (sigaction(SIGALRM, &sa, NULL)==-1) {
    warn("Unable to ignore SIGALRMs");
  }
}

// Disassembler
void make_hex(char *buff, unsigned int pc, unsigned int length) {
  char *ptr = buff;

  for (; length > 0; length -= 2) {
    sprintf(ptr, "%04x", cpu_read_word_dasm(pc));
    pc += 2;
    ptr += 4;
    if (length > 2)
      *ptr++ = ' ';
  }
}

// Given a filehandle, print the register
// values to the filehandle
void print_regs(FILE *fh) {
  fprintf(fh, "D0-D7: %08X %08X %08X %08X %08X %08X %08X %08X\n",
	m68ki_cpu.dar[0], m68ki_cpu.dar[1], m68ki_cpu.dar[2],
	m68ki_cpu.dar[3], m68ki_cpu.dar[4], m68ki_cpu.dar[5],
	m68ki_cpu.dar[6], m68ki_cpu.dar[7]);
  fprintf(fh, "A0-A7: %08X %08X %08X %08X %08X %08X %08X %08X\n",
	m68ki_cpu.dar[8],  m68ki_cpu.dar[9],  m68ki_cpu.dar[10],
	m68ki_cpu.dar[11], m68ki_cpu.dar[12], m68ki_cpu.dar[13],
	m68ki_cpu.dar[14], m68ki_cpu.dar[15]);
  fprintf(fh, "PC:    %08X  VBR:    %08X                                ",
		REG_PC, REG_VBR);
  fprintf(fh, "USP: %08X\n", REG_USP);
  fprintf(fh, "SFC:        %03X  DFC:         %03X\n", REG_SFC, REG_DFC);
  fprintf(fh, "Status: mode %c, int %d, %c%c%c%c\n",
		(FLAG_S) ? 'S' : 'U',
		FLAG_INT_MASK,
		(FLAG_N) ? 'N' : ' ',
		(FLAG_Z) ? 'Z' : ' ',
		(FLAG_V) ? 'V' : ' ',
		(FLAG_C) ? 'C' : ' ');
  fprintf(fh, "\n");
}

void usage(char *name) {
  fprintf(stderr, "\nUsage: %s [flags] executable_file\n\n", name);
  fprintf(stderr, "Flags are:\n");
  fprintf(stderr, "  -L logfile            Log debug info to this file\n");
  fprintf(stderr, "  -M mapfile            Load symbols from a map file\n");
  fprintf(stderr, "  -R romfile            Use the file as the ROM image\n");
  fprintf(stderr, "  -S sdcardfile         Attach SD card image file\n");
  fprintf(stderr, "  -U USB_image          Attach USB image file\n");
  fprintf(stderr, "  -b addr [-b addr2]    Set breakpoint(s) at symbol or dec/$hex addr\n");
  fprintf(stderr, "  -l value              Set dec/$hex bitmap of debug flags\n");
  fprintf(stderr, "  -m                    Start in the monitor\n");
  fprintf(stderr, "\nIf -R used, executable_file is optional.\n\n");
  exit(1);
}

// The main loop
int main(int argc, char *argv[]) {
  int opt;
  int i, brkcnt=0;
  int other_romfile=0;
  int breakpoint;
  int offset;
  char *sym;
  int start_in_monitor=0;
  char **brkstr;                // Array of breakpoint strings
  int pc;
  unsigned int instr_size;
  char buff[100];
  char buff2[100];

  if (argc < 2) usage(argv[0]);

  // Create an array to hold any breakpoint string pointers
  brkstr= (char **)malloc(argc * sizeof(char *));
  if (brkstr==NULL) err(EXIT_FAILURE, NULL);

  // Get the command-line arguments
  while ((opt = getopt(argc, argv, "L:M:R:S:U:b:l:m")) != -1) {
    switch (opt) {
    case 'L':
      logfh= fopen(optarg, "w+");
      if (logfh==NULL)
	errx(EXIT_FAILURE, "Unable to open %s\n", optarg);
      // Set a default log level if not already set
      if (loglevel==0) loglevel = LOG_INSTDECODE;
      atexit(close_logfile);
      break;
    case 'M':
      read_mapfile(optarg);
      break;
    case 'R':
      romfile = optarg;
      // We also note that a non-default ROM
      // has been loaded. As it could be
      // the real hardware ROM, we won't
      // require the user to name an executable
      // on the command line
      other_romfile=1;
      break;
    case 'S':
      sdfile = optarg;
      ifs= fopen(optarg, "r+");
      if (ifs==NULL)
	errx(EXIT_FAILURE, "Unable to open %s\n", optarg);

      break;
    case 'U':
      ch375file = strdup(optarg);
      break;
    case 'b':
      // Cache the pointer for now
      brkstr[brkcnt++]= optarg;
      break;
    case 'l':
      loglevel= parse_addr(optarg, NULL);
      break;
    case 'm':
      start_in_monitor=1;
      break;
    default:
	usage(argv[0]);
    }
  }

  // If we haven't loaded a non-default
  // ROM and there is no executable named
  // on the command line, it's an error
  if (other_romfile==0 && optind==argc)
    usage(argv[0]);

  // Initialise the monitor
  monitor_init();

  // Now that we might have a map file,
  // parse any breakpoint strings and set them
  for (i=0; i<brkcnt; i++) {
    breakpoint= parse_addr(brkstr[i], NULL);
    if (breakpoint != -1)
      set_breakpoint(breakpoint, BRK_INST);
  }

  // Set up the memory
  initialise_memory(romfile);

  // Load the program at address 0x40000 in RAM.
  // Only do this if we actually have a filename.
  if (optind<argc)
    ReadBinaryData(argv[optind], &(g_ram[0x40000]));

  // Initialise the terminal
  init_term();
  atexit(reset_term);

  // Initialise the SD card variables
  sdcard_init();

  // Initialise the CPU
  m68k_set_cpu_type(M68K_CPU_TYPE_68010);
  m68k_init();
  m68k_pulse_reset();

  // Start in the monitor if needed
  if (start_in_monitor) {
    pc= monitor(m68ki_cpu.pc);
    // Change the start address if the monitor says so
    if (pc!=-1)
      m68ki_cpu.pc= pc;
  }

  // Attach the routine that handles
  // the periodic timer interrupts
  attach_sigalrm();

  // Start the timer running
  set_timer();

  while (1) {
    pc= m68ki_cpu.pc;

    // Log the disassembly of the next instruction
    if (logfh != NULL && (loglevel & LOG_INSTDECODE) == LOG_INSTDECODE) {

      // Disassemble the instruction and
      // get the instruction bytes in hex
      instr_size = m68k_disassemble(buff, pc, M68K_CPU_TYPE_68010);
      make_hex(buff2, pc, instr_size);

      // See if we have a symbol at this address
      sym=NULL;
      if (mapfile_loaded)
        sym= get_symbol_and_offset(pc, &offset);
      if (sym!=NULL)
        fprintf(logfh, "%12s+%04X: ", sym, offset);
      else
        fprintf(logfh, "%04X: ", pc);

      // Now print the memory and the instruction
      fprintf(logfh, "%-20s: %s\n", buff2, buff);
    }

    // If the PC is a breakpoint, or we hit a write
    // breakpoint, fall into the monitor
    if (write_brkpt==1 || is_breakpoint(pc, BRK_INST)) {
      write_brkpt=0;
      pc= monitor(pc);

      // If we have a new PC from the monitor, set it
      if (pc != -1)
        m68ki_cpu.pc= pc;
    }

    m68k_execute(1);

    // Dump the registers after the instruction
    if (logfh != NULL && (loglevel & LOG_REGDUMP) == LOG_REGDUMP) {
      print_regs(logfh);
    }
  }

  if (logfh != NULL) fclose(logfh);
  return(0);
}
