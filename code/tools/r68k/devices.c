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
#include "loglevel.h"

// Functions to deal with the DUART,
// and the SD card, along with Easy68k
// helper functions.

#define TICK_COUNT 0x408
#define ECHO_ON    0x410
#define PROMPT_ON  0x411
#define LF_DISPLAY 0x412

char *sdfile=NULL;		// SD card file
FILE *ifs=NULL;			// File handle for this
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

    return(select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0);
}

char read_char() {
    char c = 0;
    while (c == 0) {
        read(STDIN_FILENO, &c, 1);
    }
    return(c);
}

#if 0
#define BUF_LEN 78
#define BUF_MAX BUF_LEN - 2
static uint8_t buf[BUF_LEN];

static uint8_t digit(unsigned char digit) {
    if (digit < 10) {
        return (char)(digit + '0');
    } else {
        return (char)(digit - 10 + 'A');
    }
}
#endif

#if 0
static char* print_unsigned(uint32_t num, uint8_t base) {
    if (base < 2 || base > 36) {
        buf[0] = 0;
        return (char *)buf;
    }

    unsigned char bp = BUF_MAX;

    if (num == 0) {
        buf[bp--] = '0';
    } else {
        while (num > 0) {
            buf[bp--] = digit(num % base);
            num /= base;
        }
    } 

    return ((char*)&buf[bp+1]);
}
#endif

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

    // below leaves Easy68k ops from E0 and up, others from 0 ..
    uint8_t op = d7 & 0x000000FF;
    if (op >= 0xF0) {
      op &= 0x0F;
    }

printf("op is %d d1 %d\n", op, d1);
print_regs(stdout);

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
      exit(m68k_read_memory_32(a7 + 4));	// assuming called from cstdlib - C will have stacked an exit code
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
	m68k_write_memory_8(a1 + 0, 1);	// Initialized
	m68k_write_memory_8(a1 + 1, 2);	// SDHC
	m68k_write_memory_8(a1 + 2, 0);	// No current block
	m68k_write_memory_32(a1 + 3, 0);	// Ignored (current block num)
	m68k_write_memory_16(a1 + 7, 0);	// Ignored (current block offset)
	m68k_write_memory_8(a1 + 9, 0);	// No partial reads (_could_ support, just don't yet)
	m68k_set_reg(M68K_REG_D0, 0);	// Success
      }
      break;
    case 7:
      // sd_read
      if (ifs && m68k_read_memory_8(a1) > 0) {
	
	fseek(ifs, d1 * 512, SEEK_SET);
	gcount= fread(buf, 512, 1, ifs);
#ifdef DEBUG_LOG_IO
	fprintf(stderr, "READ %x\n", d1*512);
#endif

	if (gcount == 512) {
	  for (i=0; i<512; i++) {
	    m68k_write_memory_8(a2++, buf[i]);
	  }

	  m68k_set_reg(M68K_REG_D0, 1);	// succeed
	} else {
	  printf("!!! Bad Read\n");
#ifdef DEBUG_LOG_IO
	  fprintf(stderr, "!!! Bad Read\n");
#endif
	  m68k_set_reg(M68K_REG_D0, 0);	// fail
	}
      } else {
	printf("!!! Not init\n");
#ifdef DEBUG_LOG_IO
	fprintf(stderr, "!!! Not init\n");
#endif
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
	gcount= fwrite(buf, 512, 1, ifs);

#ifdef DEBUG_LOG_IO
	fprintf(stderr, "WRITE %x\n", d1 * 512);
#endif

	if (gcount == 512) {
	  m68k_set_reg(M68K_REG_D0, 1);	// succeed
	} else {
	  printf("!!! Bad Write\n");
#ifdef DEBUG_LOG_IO
	  fprintf(stderr, "!!! Bad Write\n");
#endif
	  m68k_set_reg(M68K_REG_D0, 0);	// fail
	}
      } else {
	printf("!!! Not init or out of bounds\n");
#ifdef DEBUG_LOG_IO
	fprintf(stderr, "!!! Not init or out of bounds\n");
#endif
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
	printf("\x1B[2J"); fflush(stdout);
      } else {
	// Move X, Y
	row= d1 & 0xff;
	col= (d1 >> 8) & 0xff;
	 printf("\x1B[%d;%dH", row, col); fflush(stdout);
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
  default:
    fprintf(stderr,
"WARN: Unexpected IRQ %d; Autovectoring, but machine will probably lock up!\n",
					irq);
    return M68K_INT_ACK_AUTOVECTOR;
  }
}
