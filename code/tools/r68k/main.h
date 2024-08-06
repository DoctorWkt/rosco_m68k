// main.c
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
