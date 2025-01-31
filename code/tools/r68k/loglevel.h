// Available log levels
#ifndef LOGLEVEL_H
#define LOGLEVEL_H

#define LOG_INSTDECODE	0x01	// Instruction disassembly
#define LOG_REGDUMP	0x02	// Register dump
#define LOG_IOACCESS	0x04	// Hardware I/O operations
#define LOG_SDCARD	0x08	// SD Card operations
#define LOG_SD_DATA	0x10	// SD Card data movements
#define LOG_CH375	0x20	// CH375 operations
#define LOG_CH375_DATA	0x40	// CH375 data movements
#define LOG_MEMACCESS	0x80	// Memory accesses
#define LOG_BUSERROR	0x100	// Invalid memory accesses
#define LOG_ILLINST	0x200	// Illegal instruction handler
#define LOG_INTACK	0x400	// Interrupt acknowledge handler

#endif
