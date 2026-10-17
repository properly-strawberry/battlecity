# Headless FCEUX 2.6.6 core, built as a test oracle only (GPL; not part of the
# reconstruction). Source tree is read-only: objects go to ./obj.
# SPDX-License-Identifier: GPL-2.0-or-later (see COPYING)
FCEUX_SRC ?= /d/Development/reverse/Software/fceux-2.6.6
F := $(FCEUX_SRC)/src
CORE := asm cart cheat conddebug config debug debugsymboltable drawing fceu fds file emufile \
        filter ines input ld65dbg movie netplay nsf oldmovie palette profiler ppu sound state \
        unif video vsuni wave x6502
INPUT := arkanoid bworld cursor fkb fns ftrainer hypershot mahjong mouse oekakids pec586kb \
         powerpad quiz shadow snesmouse suborkb toprider virtualboy lcdcompzapper zapper
UTILS := xstring crc32 endian general guid md5 memory mutex timeStamp ioapi unzip
ZLIB := adler32 compress crc32 deflate gzio infblock infcodes inffast inflate inftrees infutil trees uncompr zutil
SRCS := $(CORE:%=$(F)/%.cpp) $(INPUT:%=$(F)/input/%.cpp) $(UTILS:%=$(F)/utils/%.cpp) \
        $(wildcard $(F)/boards/*.cpp)
CSRCS := $(F)/boards/emu2413.c $(ZLIB:%=$(F)/drivers/win/zlib/%.c) $(F)/utils/ConvertUTF.c
OBJS := $(patsubst $(F)/%.cpp,obj/%.o,$(SRCS)) $(patsubst $(F)/%.c,obj/%.o,$(CSRCS))
DEFS := -DHAVE_ASPRINTF -DPSS_STYLE=1 -DFCEUDEF_DEBUGGER
INC := -Ishim -I$(F)/drivers -I$(F) -I$(F)/drivers/win/zlib
CXXFLAGS := -O2 -w -fpermissive $(DEFS) $(INC)
CFLAGS := -O2 -w $(DEFS) $(INC)

fceux_headless: $(OBJS) obj/driver.o
	g++ -o $@ $^ -lpthread

obj/driver.o: driver.cpp
	@mkdir -p obj
	g++ $(CXXFLAGS) -c $< -o $@

obj/%.o: $(F)/%.cpp
	@mkdir -p $(dir $@)
	g++ $(CXXFLAGS) -c $< -o $@

obj/%.o: $(F)/%.c
	@mkdir -p $(dir $@)
	gcc $(CFLAGS) -c $< -o $@

clean:
	rm -rf obj fceux_headless
