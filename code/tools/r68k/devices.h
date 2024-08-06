// devices.c
void init_term(void);
void reset_term(void);
int check_char(void);
char read_char(void);
int illegal_instruction_handler(int opcode);
int interrupt_ack_handler(unsigned int irq);

#define DUART_IRQ       4
#define DUART_VEC       0x45
