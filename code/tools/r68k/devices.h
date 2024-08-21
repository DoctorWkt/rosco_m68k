// devices.c
void init_term(void);
void reset_term(void);
int check_char(void);
char read_char(void);

unsigned int io_read_byte(unsigned int address);
unsigned int io_read_word(unsigned int address);
unsigned int io_read_long(unsigned int address);
void io_write_byte(unsigned int address, unsigned int value);
void io_write_word(unsigned int address, unsigned int value);
void io_write_long(unsigned int address, unsigned int value);

int illegal_instruction_handler(int opcode);
int interrupt_ack_handler(unsigned int irq);

#define DUART_IRQ       4
#define DUART_VEC       0x45

#define CH375_IRQ	3
#define CH375_VEC       0x1B
