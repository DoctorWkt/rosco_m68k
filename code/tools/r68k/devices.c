#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <err.h>
#include "musashi/m68k.h"
#include "musashi/m68kcpu.h"
#include "devices.h"
#include "main.h"
#include "mapfile.h"
#include "loglevel.h"
#include "sdcard.h"
#include "ch375.h"

// Functions to emulate hardware (DUART, SD card)
// as well as functions that implement system calls.

#define TICK_COUNT 0x408
#define ECHO_ON    0x410
#define PROMPT_ON  0x411
#define LF_DISPLAY 0x412

// UART definitions. These need to
// have more comments added TODO
#define UART_BASE	0x00f00001	// More comments here please!!
#define DUART_MR1A	0x00f00001
#define DUART_SRA	0x00f00003	// UART port A status
#define	DUART_CSRA	0x00f00003
#define	DUART_CRA	0x00f00005
#define DUART_RBA	0x00f00007	// Read from UART port A
#define DUART_TBA	0x00f00007	// Write to UART port A
#define	DUART_ACR	0x00f00009
#define DUART_IMR	0x00f0000a
#define DUART_ISR	0x00f0000b
#define W_CLKSEL_B	0x00f0000b
#define DUART_CTUR	0x00f0000d
#define DUART_CTLR	0x00f0000f
#define DUART_MR1B	0x00f00011
#define DUART_CSRB	0x00f00013
#define DUART_SRB	0x00f00013
#define DUART_CRB	0x00f00015
#define DUART_TBB	0x00f00017
#define DUART_IVR	0x00f00019
#define DUART_OPCR	0x00f0001b
#define R_STARTCNTCMD	0x00f0001d
#define R_STOPCNTCMD	0x00f0001f
#define W_OPR_RESETCMD  0x00f0001f

// SPI defines
#define SPI_OUTBIT	0x00f0001d
#define  SPI_ASSERTCS0	0x04
#define  SPI_OUTMASK	0x40	// This bit inverse of output bit
#define SPI_OUTPUT	0x10	// If set, is a bit send
#define SPI_INBIT	0x00f0001b
#define SPI_INMASK	0x04	// Bit to set if receiving a 1 bit

// Xosera addresses
#define XM_BASEADDR	0x00f80060

// ATA definitions
#define ATA_REG_WR_DEVICE_CTL	0x00f8005c

// CH375 addresses
#define CH375_DATADDR	0x00fff001	// Send/recv CH375 data
#define CH375_CMDADDR	0x00fff003	// Send      CH375 commands

// Other
#define BERR_FLAG       0x1184

char *sdfile = NULL;		// SD card file
FILE *ifs = NULL;		// File handle for this
extern FILE *logfh;
extern int loglevel;

// Terminal handling functions
struct termios originalTermios;

void init_term() {
  struct termios newTermios;

  tcgetattr(STDIN_FILENO, &originalTermios);

  newTermios = originalTermios;
  newTermios.c_lflag &= ~(ICANON | ECHO);
  newTermios.c_iflag &= ~(ICRNL | INLCR);

  tcsetattr(STDIN_FILENO, TCSANOW, &newTermios);

  int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

void reset_term() {
  tcsetattr(STDIN_FILENO, TCSANOW, &originalTermios);
}

int check_char() {
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;

  return (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0);
}

char read_char() {
  char c = 0;
  while (c == 0) {
    read(STDIN_FILENO, &c, 1);
  }
  return (c);
}

// I/O Handling Routines

static uint8_t ivr_value = 0x0f;
static uint8_t spi_outvalue = 0;	// Data sent by CPU via SPI
static uint8_t spi_outcount = 0;	// Count of bits received
static uint8_t spi_invalue = 0;		// Data to be received via SPI
static uint8_t spi_incount = 0;		// Count of bits received
static uint8_t spi_isdata = 0;		// Is there data to receive?

// Print a log message.
void unimplemented_io(unsigned int address, unsigned int value,
		      char *msg, int iswrite) {
  char *sym;
  int offset;

  fprintf(logfh, "Unimplemented I/O %s, ", msg);
  if (iswrite)
    fprintf(logfh, "value 0x%x, ", value);

  sym = get_symbol_and_offset(m68ki_cpu.pc, &offset);


  if (sym != NULL)
    fprintf(logfh, "addr 0x%x, PC %s+$%x (0x%x)\n", address, sym, offset,
	    m68ki_cpu.pc);
  else
    fprintf(logfh, "addr 0x%x, PC 0x%x\n", address, m68ki_cpu.pc);
  exit(1);
}


unsigned int io_read_byte(unsigned int address) {
  unsigned int value;
  uint8_t *dataptr;

  switch (address) {
    // UART
  case DUART_SRA:		// Get the status of port A
    value = 8;			// Port A is writable
    if (check_char())
      value = 9;		// Writeable and ready to read
    return (value);
  case DUART_RBA:		// Read a character from port A
    return (read_char());
  case DUART_IVR:
    return (ivr_value);
  case DUART_SRB:		// Get status of port B
    return (8);			// Writeable, but for now writes
    				// are discarded. To fix later
    				// with a socket.
  case R_STOPCNTCMD:
  case R_STARTCNTCMD:
    return (0);
  case DUART_ISR:		// Counter interrupt
    return (8);

    // Xosera: say that it doesn't exist
  case XM_BASEADDR:
    // Write 1 to address BERR_FLAG to indicate
    // that there is no RAM at this address
    cpu_write_byte(BERR_FLAG, 1);
    return (0);

    // CH375
  case CH375_DATADDR:
    return(read_ch375_data());

    // SPI
  case SPI_INBIT:
    // If there is no data to receive
    if (spi_isdata == 0) {
      // See if there is any in the SD card buffer
      dataptr = spi_get_data();
      if (dataptr == NULL)
	return (0);

      // Get the byte of data to send.
      // We start at bit position 0.
      spi_invalue = *dataptr;
      spi_incount = 0;
      spi_isdata = 1;
    }

    // Most significant bit on?
    if (spi_invalue & 0x80)
      value = SPI_INMASK;
    else
      value = 0;

    // Shift to lose that bit, bump the count
    // and reset if we have sent all eight bits
    spi_invalue = spi_invalue << 1;
    spi_incount++;

    if (spi_incount == 8) {
      spi_incount = 0;
      spi_isdata = 0;
    }
    return (value);
  }

  if (logfh != NULL && (loglevel & LOG_IOACCESS) == LOG_IOACCESS) {
    unimplemented_io(address, 0, "byte read", 0);
  }
  return (0);
}

unsigned int io_read_word(unsigned int address) {
  if (logfh != NULL && (loglevel & LOG_IOACCESS) == LOG_IOACCESS) {
    unimplemented_io(address, 0, "word read", 0);
  }
  return (0);
}

unsigned int io_read_long(unsigned int address) {
  if (logfh != NULL && (loglevel & LOG_IOACCESS) == LOG_IOACCESS) {
    unimplemented_io(address, 0, "long read", 0);
  }
  return (0);
}

void io_write_byte(unsigned int address, unsigned int value) {
  uint8_t result;

  switch (address) {
  // UART
  case DUART_TBA:		// Send a character on port A
    fputc(value & 0xFF, stdout);
    fflush(stdout);
    return;
  case DUART_IVR:
    ivr_value = value & 0xFF;
    return;
  case W_CLKSEL_B:
  case W_OPR_RESETCMD:
  case DUART_CRA:
  case DUART_ACR:
  case DUART_CSRA:
  case DUART_CRB:
  case DUART_CSRB:
  case DUART_MR1A:
  case DUART_MR1B:
  case DUART_OPCR:
  case DUART_CTUR:
  case DUART_CTLR:
  case DUART_TBB:		// Writes to port B discarded for now
    return;

  case DUART_IMR:
    // Turn off the 100Hz heartbeat
    if ((value & 0xff)==0) {
      detach_sigalrm();
    }
    return;

  // CH375: If we get a true result back,
  // then generate a level 3 interrupt.
  case CH375_DATADDR:
    result= send_ch375_data(value & 0xff);
    if (result) m68k_set_irq(3);
    return;
  case CH375_CMDADDR:
    result= send_ch375_cmd(value & 0xff);
    if (result) m68k_set_irq(3);
    return;

  // SPI
  case SPI_OUTBIT:
    // If CS) has been asserted
    if (value & SPI_ASSERTCS0) {
      // Send back an 0xFF data byte
      spi_invalue = 0xff;
      spi_incount = 0;
      spi_isdata = 1;
      return;
    }

    // If there is an SPI output bit
    if (value & SPI_OUTPUT) {
      // Convert to 0 or 1, then
      // shift it into spi_outvalue
      value = 1 - ((value & SPI_OUTMASK) >> 6);
      spi_outvalue = (spi_outvalue << 1) | value;
      spi_outcount++;

      if (spi_outcount == 8) {
	// Send the received byte to the
	// SD card command handler
#if 0
	if (logfh != NULL && (loglevel & LOG_IOACCESS) == LOG_IOACCESS) {
	  if (spi_outvalue != 0xff)
	    fprintf(logfh, "Latched SPI byte 0x%x\n", spi_outvalue);
	}
#endif
	spi_latch_in(spi_outvalue);
	spi_outcount = 0;
	spi_outvalue = 0;
      }
    }
    return;
  }

  if (logfh != NULL && (loglevel & LOG_IOACCESS) == LOG_IOACCESS) {
    unimplemented_io(address, value, "byte write", 1);
  }
}

void io_write_word(unsigned int address, unsigned int value) {
  switch (address) {
  // ATA
  case ATA_REG_WR_DEVICE_CTL:
    // Write 1 to address BERR_FLAG to indicate
    // that there is nothing at this address
    cpu_write_byte(BERR_FLAG, 1);
    return;
  }

  if (logfh != NULL && (loglevel & LOG_IOACCESS) == LOG_IOACCESS) {
    unimplemented_io(address, value, "word write", 1);
  }
}

void io_write_long(unsigned int address, unsigned int value) {
  if (logfh != NULL && (loglevel & LOG_IOACCESS) == LOG_IOACCESS) {
    unimplemented_io(address, value, "long write", 1);
  }
}


int illegal_instruction_handler(int __attribute__((unused)) opcode) {
  m68ki_cpu_core ctx;
  m68k_get_context(&ctx);

  uint32_t d7 = m68k_get_reg(&ctx, M68K_REG_D7);
  uint32_t d6 = m68k_get_reg(&ctx, M68K_REG_D6);
  uint32_t d0 = m68k_get_reg(&ctx, M68K_REG_D0);
  uint32_t d1 = m68k_get_reg(&ctx, M68K_REG_D1);
  uint32_t d2 = m68k_get_reg(&ctx, M68K_REG_D2);
  uint32_t a0 = m68k_get_reg(&ctx, M68K_REG_A0);
  uint32_t a1 = m68k_get_reg(&ctx, M68K_REG_A1);
  uint32_t a2 = m68k_get_reg(&ctx, M68K_REG_A2);
  uint32_t a7 = m68k_get_reg(&ctx, M68K_REG_A7);

  if ((d7 & 0xFFFFFF00) == 0xF0F0F000 && d6 == 0xAA55AA55) {
    // It's a trap!
    // Below leaves Easy68k ops from E0 and up, others from 0 ..
    uint8_t op = d7 & 0x000000FF;
    if (op >= 0xF0) {
      op &= 0x0F;
    }

    uint8_t c;
    bool r;
    int i, ptr;
    int chars_left = 0;
    int chars_read = 0;
    int num = 0;
    int gcount;
    uint8_t buf[512];
    uint8_t row, col;

    fflush(stdout);

    if (logfh != NULL && (loglevel & LOG_ILLINST) == LOG_ILLINST) {
      fprintf(logfh, "illegal_instruction_handler, op %d\n", op);
    }

    switch (op) {
    case 0:
      // Print
      do {
	c = m68k_read_memory_8(a0++);
	if (c) {
	  fputc(c, stdout);
	}
      } while (c != 0);
      fflush(stdout);

      break;
    case 1:
      // println
      do {
	c = m68k_read_memory_8(a0++);
	if (c) {
	  fputc(c, stdout);
	}
      } while (c != 0);

      fputc('\n', stdout);

      break;
    case 2:
      // printchar
      c = (d0 & 0xFF);
      if (c) {
	fputc(c, stdout);
	fflush(stdout);
      }

      break;
    case 3:
      // prog_exit
      m68k_pulse_halt();
      tcsetattr(STDIN_FILENO, TCSANOW, &originalTermios);
      exit(m68k_read_memory_32(a7 + 4));	// assuming called from
						// cstdlib - C will have
						// stacked an exit code
      break;
    case 4:
      // check_char
      r = check_char();
      m68k_set_reg(M68K_REG_D0, r ? 1 : 0);

      break;
    case 5:
      // read_char
      c = read_char();
      fflush(stdout);
      m68k_set_reg(M68K_REG_D0, c);

      break;
    case 6:
      // sd_init
      if (!ifs) {
	m68k_set_reg(M68K_REG_D0, 1);
      } else {
	m68k_write_memory_8(a1 + 0, 1);	 // Initialized
	m68k_write_memory_8(a1 + 1, 2);	 // SDHC
	m68k_write_memory_8(a1 + 2, 0);	 // No current block
	m68k_write_memory_32(a1 + 3, 0); // Ignored (current block num)
	m68k_write_memory_16(a1 + 7, 0); // Ignored (current block offset)
	m68k_write_memory_8(a1 + 9, 0);	 // No partial reads
					 // (_could_ support, just don't yet)
	m68k_set_reg(M68K_REG_D0, 0);	 // Success
      }
      break;
    case 7:
      // sd_read
      if (ifs && m68k_read_memory_8(a1) > 0) {

	fseek(ifs, d1 * 512, SEEK_SET);
	gcount = fread(buf, 1, 512, ifs);
	if (logfh != NULL && (loglevel & LOG_SDCARD) == LOG_SDCARD) {
	  fprintf(logfh, "SD card read block %d\n", d1);
	}

	if (gcount == 512) {
	  for (i = 0; i < 512; i++) {
	    m68k_write_memory_8(a2++, buf[i]);
	  }

	  m68k_set_reg(M68K_REG_D0, 1);	// succeed
	} else {
	  if (logfh != NULL && (loglevel & LOG_SDCARD) == LOG_SDCARD) {
	    fprintf(logfh, "!!! Bad Read\n");
	  }
	  m68k_set_reg(M68K_REG_D0, 0);	// fail
	}
      } else {
	if (logfh != NULL && (loglevel & LOG_SDCARD) == LOG_SDCARD) {
	  fprintf(logfh, "!!! Not init\n");
	}
	m68k_set_reg(M68K_REG_D0, 0);	// fail
      }

      break;
    case 8:
      // sd_write
      if (a2 < 0xe00000 && ifs && m68k_read_memory_8(a1) > 0) {

	for (int i = 0; i < 512; i++) {
	  buf[i] = m68k_read_memory_8(a2++);
	}

	fseek(ifs, d1 * 512, SEEK_SET);
	gcount = fwrite(buf, 1, 512, ifs);

	if (logfh != NULL && (loglevel & LOG_SDCARD) == LOG_SDCARD) {
	  fprintf(logfh, "SD card write block %d\n", d1);
	}

	if (gcount == 512) {
	  m68k_set_reg(M68K_REG_D0, 1);	// succeed
	} else {
	  if (logfh != NULL && (loglevel & LOG_SDCARD) == LOG_SDCARD) {
	    fprintf(logfh, "!!! Bad Write\n");
	  }
	  m68k_set_reg(M68K_REG_D0, 0);	// fail
	}
      } else {
	if (logfh != NULL && (loglevel & LOG_SDCARD) == LOG_SDCARD) {
	  fprintf(logfh, "!!! Not init or out of bounds\n");
	}
	m68k_set_reg(M68K_REG_D0, 0);	// fail
      }

      break;
      // Start of Easy68k traps
    case 0xD0:
    case 0xD1:
      // PRINT_LN_LEN / PRINT_LEN
      do {
	c = m68k_read_memory_8(a1++);
	if (c) {
	  fputc(c, stdout);
	}
      } while ((c != 0) && ((--d1 & 0xFF) > 0));

      if (op == 0xD0) {
	fputc('\n', stdout);
      } else {
	fflush(stdout);
      }

      break;
    case 0xD2:
      // READSTR
      if (m68k_read_memory_8(PROMPT_ON) == 1) {
	fputs("Input$> ", stdout);
	fflush(stdout);
      }

      chars_read = 0;
      ptr = a1;			// save start of input buffer

      while (chars_read++ < 80) {
	c = read_char();
	fflush(stdout);

	if (c == 0x0D) {
	  break;
	}

	if (m68k_read_memory_8(ECHO_ON) == 1) {
	  fputc(c, stdout);
	  fflush(stdout);
	}

	m68k_write_memory_8(a1++, c);
      }

      m68k_write_memory_8(a1++, 0);

      if (m68k_read_memory_8(LF_DISPLAY) == 1) {
	fputc('\n', stdout);
      } else {
	fflush(stdout);
      }

      m68k_set_reg(M68K_REG_D1, (chars_read - 1));
      m68k_set_reg(M68K_REG_A1, ptr);

      break;
    case 0xD3:
      // DISPLAYNUM_SIGNED
      printf("%d", d1);
      fflush(stdout);

      break;
    case 0xD4:
      // READNUM
      if (m68k_read_memory_8(PROMPT_ON) == 1) {
	fputs("Input#> ", stdout);
	fflush(stdout);
      }

      while (--chars_left) {
	c = read_char();
	fflush(stdout);

	if (c == 0x0D) {
	  break;
	}

	if ((c >= '0') && (c <= '9')) {
	  num = (num * 10) + (c - '0');
	  if (m68k_read_memory_8(ECHO_ON) == 1) {
	    fputc(c, stdout);
	  }
	}
      }
      if (m68k_read_memory_8(LF_DISPLAY) == 1) {
	fputc('\n', stdout);
      }
      m68k_set_reg(M68K_REG_D1, num);

      break;
    case 0xD5:
      // READCHAR
      fflush(stdout);
      c = read_char();
      fflush(stdout);
      m68k_set_reg(M68K_REG_D1, c);

      break;
    case 0xD6:
      // SENDCHAR
      fputc(d1 & 0xFF, stdout);
      fflush(stdout);

      break;
    case 0xD7:
      // CHECKINPUT
      r = check_char();
      m68k_set_reg(M68K_REG_D1, r ? 1 : 0);

      break;
    case 0xD8:
      // GETUPTICKS
      m68k_set_reg(M68K_REG_D1, m68k_read_memory_16(TICK_COUNT));

      break;
    case 0xD9:
      // TERMINATE
      m68k_pulse_halt();
      tcsetattr(STDIN_FILENO, TCSANOW, &originalTermios);
      exit(0);

      break;
      // case 0xDA:
      // Not implemented

    case 0xDB:
      // MOVEXY: Yes I'm using ANSI escape sequences
      // instead of using termcap or curses. Later ...
      if ((d1 & 0xFFFF) == 0xFF00) {
	// clear screen
	printf("\x1B[2J");
	fflush(stdout);
      } else {
	// Move X, Y
	row = d1 & 0xff;
	col = (d1 >> 8) & 0xff;
	printf("\x1B[%d;%dH", row, col);
	fflush(stdout);
      }

      break;
    case 0xDC:
      // SETECHO
      if (d1 == 0) {
	m68k_write_memory_8(ECHO_ON, 0);
      }
      if (d1 == 1) {
	m68k_write_memory_8(ECHO_ON, 1);
      }

      break;
    case 0xDD:
    case 0xDE:
      // PRINTLN_SZ / PRINT_SZ
      do {
	c = m68k_read_memory_8(a1++);
	if (c) {
	  fputc(c, stdout);
	}
      } while (c != 0);

      if (op == 0xDD) {
	fputc('\n', stdout);
      } else {
	fflush(stdout);
      }

      break;
    case 0xDF:
      // PRINT_UNSIGNED
      printf("%u %u", d1, d2);
      fflush(stdout);

      break;
    case 0xE0:
      // SETDISPLAY
      if (d1 == 0) {
	m68k_write_memory_8(PROMPT_ON, 0);
      }
      if (d1 == 1) {
	m68k_write_memory_8(PROMPT_ON, 1);
      }
      if (d1 == 2) {
	m68k_write_memory_8(LF_DISPLAY, 0);
      }
      if (d1 == 3) {
	m68k_write_memory_8(LF_DISPLAY, 1);
      }

      break;
    case 0xE1:
      // PRINTSZ_NUM
      do {
	c = m68k_read_memory_8(a1++);
	if (c) {
	  fputc(c, stdout);
	}
      } while (c != 0);

      printf("%d", d1);
      fflush(stdout);

      break;
    case 0xE2:
      // PRINTSZ_READ_NUM
      do {
	c = m68k_read_memory_8(a1++);
	if (c) {
	  fputc(c, stdout);
	  fflush(stdout);
	}
      } while (c != 0);

      chars_left = 10;

      while (--chars_left) {
	c = read_char();
	fflush(stdout);

	if (c == 0x0D) {
	  break;
	}

	if ((c >= '0') && (c <= '9')) {
	  num = (num * 10) + (c - '0');
	  if (m68k_read_memory_8(ECHO_ON) == 1) {
	    fputc(c, stdout);
	  }
	}
      }
      if (m68k_read_memory_8(LF_DISPLAY) == 1) {
	fputc('\n', stdout);
      }
      m68k_set_reg(M68K_REG_D1, num);

      break;
      // case 0xE3:
      // Not implemented

    case 0xE4:
      // PRINTNUM_SIGNED_WIDTH
      // cout << setw(d2) << (int) d1 << flush;
      printf("%d", d1);
      fflush(stdout);
      break;

    default:
      fprintf(stderr,
	      "<UNKNOWN OP %x; D7=0x%x; D6=0x%x: IGNORED>\n", op, d7, d6);
    }
  }

  return 1;
}

int interrupt_ack_handler(unsigned int irq) {

  if (logfh != NULL && (loglevel & LOG_INTACK) == LOG_INTACK) {
    fprintf(logfh, "interrupt_ack_handler, irq %d\n", irq);
  }

  switch (irq) {
  case DUART_IRQ:
    // DUART timer tick - vector to 0x45
    m68k_set_irq(0);
    return DUART_VEC;
  case CH375_IRQ:
    // CH375 interrupt - vector to CH375_VEC (irq3)
    m68k_set_irq(0);
    return CH375_VEC;
  default:
    fprintf(stderr,
	    "WARN: Unexpected IRQ %d; Autovectoring, but machine will probably lock up!\n",
	    irq);
    return M68K_INT_ACK_AUTOVECTOR;
  }
}
