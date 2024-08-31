// main.c

// Size of ROM/RAM and their locations
// in the MC68010 address space. Also
// the base of the I/O area.
#define RAM_SIZE        1024 * 1024
#define RAM_BASE        0x00000000
#define ROM_SIZE        1024 * 1024
#define ROM_BASE        0x00e00000

#define IO_BASE         0x00f00000

extern uint8_t *g_ram;		// RAM memory

unsigned int cpu_read_byte(unsigned int address);
unsigned int cpu_read_word(unsigned int address);
unsigned int cpu_read_long(unsigned int address);
unsigned int cpu_read_word_dasm(unsigned int address);
unsigned int cpu_read_long_dasm(unsigned int address);
void cpu_write_byte(unsigned int address, unsigned int value);
void cpu_write_word(unsigned int address, unsigned int value);
void cpu_write_long(unsigned int address, unsigned int value);
void cpu_pulse_reset(void);
void make_hex(char *buff, unsigned int pc, unsigned int length);
void print_regs(FILE *fh);
void attach_sigalrm(void);
void detach_sigalrm(void);
void set_timer(void);

