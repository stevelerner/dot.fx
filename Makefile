CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Werror
CFLAGS  += -Icore
LDLIBS  += -lm

HARNESS   = harness/harness
MKTESTIMG = tools/mktestimg
F0RHOST   = tools/f0r_host
F0R_INCL  = -Ireference/frei0r/include

F0R_CORE  = core/crt.c core/vhs.c
F0R_METAL = core/metal_fx.m
F0R_LIBS_EXTRA = -framework Metal -framework Foundation
F0R_SRCS  = frei0r-adapter/retrofx.c
F0R_DEPS  = $(F0R_SRCS) $(F0R_CORE) core/crt.h core/vhs.h core/fx_hash.h core/fx_math.h $(F0R_METAL) core/metal_fx.h

F0R_BUILD = build
F0R_LIBS  = $(F0R_BUILD)/libretrofx_vid_scanlines.dylib \
            $(F0R_BUILD)/libretrofx_vid_shadowmask.dylib \
            $(F0R_BUILD)/libretrofx_vid_chromablood.dylib \
            $(F0R_BUILD)/libretrofx_vid_lumar.dylib \
            $(F0R_BUILD)/libretrofx_vid_rainbow.dylib \
            $(F0R_BUILD)/libretrofx_vid_barrel.dylib \
            $(F0R_BUILD)/libretrofx_vid_bloom.dylib \
            $(F0R_BUILD)/libretrofx_vid_vignette.dylib \
            $(F0R_BUILD)/libretrofx_vid_overscan.dylib \
            $(F0R_BUILD)/libretrofx_vid_tapewow.dylib \
            $(F0R_BUILD)/libretrofx_vid_glitch.dylib \
            $(F0R_BUILD)/libretrofx_dotgate.dylib \
            $(F0R_BUILD)/libretrofx_dotportal.dylib \
            $(F0R_BUILD)/libretrofx_dot_spacengrave.dylib

all: $(HARNESS)

frei0r: $(F0R_LIBS) $(F0RHOST)

$(F0R_BUILD)/libretrofx_vid_scanlines.dylib: $(F0R_DEPS)
	mkdir -p $(F0R_BUILD)
	$(CC) $(CFLAGS) $(F0R_INCL) -DRETROFX_EFFECT=RETROFX_EFFECT_VID_SCANLINES -dynamiclib -o $@ $(F0R_SRCS) $(F0R_CORE) $(LDLIBS)

$(F0R_BUILD)/libretrofx_vid_shadowmask.dylib: $(F0R_DEPS)
	mkdir -p $(F0R_BUILD)
	$(CC) $(CFLAGS) $(F0R_INCL) -DRETROFX_EFFECT=RETROFX_EFFECT_VID_SHADOWMASK -dynamiclib -o $@ $(F0R_SRCS) $(F0R_CORE) $(LDLIBS)

$(F0R_BUILD)/libretrofx_vid_chromablood.dylib: $(F0R_DEPS)
	mkdir -p $(F0R_BUILD)
	$(CC) $(CFLAGS) $(F0R_INCL) -DRETROFX_EFFECT=RETROFX_EFFECT_VID_CHROMABLOOD -dynamiclib -o $@ $(F0R_SRCS) $(F0R_CORE) $(LDLIBS)

$(F0R_BUILD)/libretrofx_vid_lumar.dylib: $(F0R_DEPS)
	mkdir -p $(F0R_BUILD)
	$(CC) $(CFLAGS) $(F0R_INCL) -DRETROFX_EFFECT=RETROFX_EFFECT_VID_LUMAR -dynamiclib -o $@ $(F0R_SRCS) $(F0R_CORE) $(LDLIBS)

$(F0R_BUILD)/libretrofx_vid_rainbow.dylib: $(F0R_DEPS)
	mkdir -p $(F0R_BUILD)
	$(CC) $(CFLAGS) $(F0R_INCL) -DRETROFX_EFFECT=RETROFX_EFFECT_VID_RAINBOW -dynamiclib -o $@ $(F0R_SRCS) $(F0R_CORE) $(LDLIBS)

$(F0R_BUILD)/libretrofx_vid_barrel.dylib: $(F0R_DEPS)
	mkdir -p $(F0R_BUILD)
	$(CC) $(CFLAGS) $(F0R_INCL) -DRETROFX_EFFECT=RETROFX_EFFECT_VID_BARREL -dynamiclib -o $@ $(F0R_SRCS) $(F0R_CORE) $(LDLIBS)

$(F0R_BUILD)/libretrofx_vid_bloom.dylib: $(F0R_DEPS)
	mkdir -p $(F0R_BUILD)
	$(CC) $(CFLAGS) $(F0R_INCL) -DRETROFX_EFFECT=RETROFX_EFFECT_VID_BLOOM -dynamiclib -o $@ $(F0R_SRCS) $(F0R_CORE) $(LDLIBS)

$(F0R_BUILD)/libretrofx_vid_vignette.dylib: $(F0R_DEPS)
	mkdir -p $(F0R_BUILD)
	$(CC) $(CFLAGS) $(F0R_INCL) -DRETROFX_EFFECT=RETROFX_EFFECT_VID_VIGNETTE -dynamiclib -o $@ $(F0R_SRCS) $(F0R_CORE) $(LDLIBS)

$(F0R_BUILD)/libretrofx_vid_overscan.dylib: $(F0R_DEPS)
	mkdir -p $(F0R_BUILD)
	$(CC) $(CFLAGS) $(F0R_INCL) -DRETROFX_EFFECT=RETROFX_EFFECT_VID_OVERSCAN -dynamiclib -o $@ $(F0R_SRCS) $(F0R_CORE) $(LDLIBS)

$(F0R_BUILD)/libretrofx_vid_tapewow.dylib: $(F0R_DEPS)
	mkdir -p $(F0R_BUILD)
	$(CC) $(CFLAGS) $(F0R_INCL) -DRETROFX_EFFECT=RETROFX_EFFECT_VID_TAPEWOW -dynamiclib -o $@ $(F0R_SRCS) $(F0R_CORE) $(LDLIBS)

$(F0R_BUILD)/libretrofx_vid_glitch.dylib: $(F0R_DEPS) core/glitch.c core/glitch.h core/dot.c core/dot.h
	mkdir -p $(F0R_BUILD)
	$(CC) $(CFLAGS) $(F0R_INCL) -DRETROFX_EFFECT=RETROFX_EFFECT_VID_GLITCH -dynamiclib -o $@ $(F0R_SRCS) $(F0R_CORE) core/glitch.c core/dot.c $(F0R_METAL) $(LDLIBS) $(F0R_LIBS_EXTRA)

	$(F0R_BUILD)/libretrofx_dotgate.dylib: $(F0R_DEPS) core/dot.c core/dot.h
	mkdir -p $(F0R_BUILD)
	$(CC) $(CFLAGS) $(F0R_INCL) -DRETROFX_EFFECT=RETROFX_EFFECT_DOTGATE -dynamiclib -o $@ $(F0R_SRCS) $(F0R_CORE) core/dot.c $(F0R_METAL) $(LDLIBS) $(F0R_LIBS_EXTRA)

$(F0R_BUILD)/libretrofx_dotportal.dylib: $(F0R_DEPS) core/dot.c core/dot.h
	mkdir -p $(F0R_BUILD)
	$(CC) $(CFLAGS) $(F0R_INCL) -DRETROFX_EFFECT=RETROFX_EFFECT_DOTPORTAL -dynamiclib -o $@ $(F0R_SRCS) $(F0R_CORE) core/dot.c $(F0R_METAL) $(LDLIBS) $(F0R_LIBS_EXTRA)

$(F0R_BUILD)/libretrofx_dot_spacengrave.dylib: $(F0R_DEPS) core/dot.c core/dot.h
	mkdir -p $(F0R_BUILD)
	$(CC) $(CFLAGS) $(F0R_INCL) -DRETROFX_EFFECT=RETROFX_EFFECT_DOT_SPACENGRAVE -dynamiclib -o $@ $(F0R_SRCS) $(F0R_CORE) core/dot.c $(F0R_METAL) $(LDLIBS) $(F0R_LIBS_EXTRA)

$(F0RHOST): tools/f0r_host.c core/fx_hash.h
	$(CC) $(CFLAGS) $(F0R_INCL) -o $@ tools/f0r_host.c

$(MKTESTIMG): tools/mktestimg.c
	$(CC) $(CFLAGS) -o $@ $<

testimgs/bars.ppm: $(MKTESTIMG)
	mkdir -p testimgs
	./$(MKTESTIMG) testimgs

$(HARNESS): harness/main.c core/crt.c core/vhs.c core/glitch.c core/dot.c core/crt.h core/vhs.h core/glitch.h core/dot.h core/fx_hash.h
	$(CC) $(CFLAGS) -o $@ harness/main.c core/crt.c core/vhs.c core/glitch.c core/dot.c $(LDLIBS)

.PHONY: all testimgs clean frei0r
testimgs: testimgs/bars.ppm

clean:
	rm -f $(HARNESS) $(MKTESTIMG) $(F0RHOST)
	rm -rf $(F0R_BUILD)
