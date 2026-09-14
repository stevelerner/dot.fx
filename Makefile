CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Werror
CFLAGS  += -Icore
LDLIBS  += -lm

HARNESS   = harness/harness
DOTPIPE   = dotpipe/dotpipe
MKTESTIMG = tools/mktestimg

all: $(HARNESS) $(DOTPIPE)

$(MKTESTIMG): tools/mktestimg.c
	$(CC) $(CFLAGS) -o $@ $<

testimgs/bars.ppm: $(MKTESTIMG)
	mkdir -p testimgs
	./$(MKTESTIMG) testimgs

$(HARNESS): harness/main.c core/crt.c core/vhs.c core/glitch.c core/dot.c core/crt.h core/vhs.h core/glitch.h core/dot.h core/fx_hash.h
	$(CC) $(CFLAGS) -o $@ harness/main.c core/crt.c core/vhs.c core/glitch.c core/dot.c $(LDLIBS)

$(DOTPIPE): dotpipe/dotpipe.c core/crt.c core/vhs.c core/glitch.c core/dot.c core/metal_fx.m core/crt.h core/vhs.h core/glitch.h core/dot.h core/fx_hash.h core/metal_fx.h
	$(CC) $(CFLAGS) -o $@ dotpipe/dotpipe.c core/crt.c core/vhs.c core/glitch.c core/dot.c core/metal_fx.m $(LDLIBS) -framework Metal -framework Foundation

# The archived frei0r plugin method builds separately:
#   make -f archive/frei0r/Makefile

.PHONY: all testimgs clean
testimgs: testimgs/bars.ppm

clean:
	rm -f $(HARNESS) $(DOTPIPE) $(MKTESTIMG)
