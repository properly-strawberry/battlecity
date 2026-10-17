CFLAGS ?= -O2 -Wall -Wextra
all: nesrun libnes.so
nesrun: nesrun.c nes.c cpu6502.c nes.h cpu6502.h
	$(CC) $(CFLAGS) -o $@ nesrun.c nes.c cpu6502.c
libnes.so: nes.c cpu6502.c nes.h cpu6502.h
	$(CC) $(CFLAGS) -shared -fPIC -o $@ nes.c cpu6502.c
clean:
	rm -f nesrun libnes.so
