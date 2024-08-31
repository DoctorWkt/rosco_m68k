# Make r68k 
# (c) 2023 Ross Bamford & Contribs

CLEAN_FILES=r68k *.o rosco_m68k_glue/*.o machine/*.o
R68K_OBJS=main.o devices.o mapfile.o monitor.o \
	 sdcard.o ch375.o xv6syscalls.o
MUSASHI_OBJS=musashi/m68kcpu.o musashi/m68kdasm.o musashi/m68kops.o \
	musashi/softfloat/softfloat.o
ROM_BINARY=firmware/rosco_m68k.rom
CFLAGS=-g -Wall -Wextra -Wpedantic -Iinclude #-DDEBUG_LOG_IO
LIBS= -lreadline

.PHONY: clean all

all: r68k $(ROM_BINARY)

clean:
	rm -rf $(CLEAN_FILES)
	$(MAKE) -C musashi clean
	$(MAKE) -C firmware clean

r68k: $(MUSASHI_OBJS) $(R68K_OBJS)
	$(CC) -o $@ $^ $(LIBS)

musashi/%.o:
	$(MAKE) -C musashi $(patsubst musashi/%,%,$@)

$(ROM_BINARY):
	$(MAKE) -C firmware rosco_m68k.rom

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<
