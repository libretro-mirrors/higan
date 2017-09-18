ifneq ($(platform),windows)
  flags += -fPIC
endif

ifneq ($(target),macosx)
  link += -shared
else
  link += -dynamiclib
endif

ifeq ($(core),sfc)
  corename := sfc
  include sfc/GNUmakefile
else ifeq ($(core),)
  corename := sfc
  include sfc/GNUmakefile
endif

ifeq ($(platform),windows)
  name := higan_$(corename)_libretro.dll
else ifeq ($(platform),macosx)
  name := higan_$(corename)_libretro.dylib
else
  name := higan_$(corename)_libretro.so
endif
#flags += -DSFC_SUPERGAMEBOY

#include fc/GNUmakefile
#include ms/GNUmakefile
#include md/GNUmakefile
#include pce/GNUmakefile
#include gb/GNUmakefile
#include gba/GNUmakefile
#include ws/GNUmakefile
include processor/GNUmakefile

# rules
objects := libretro_$(corename) $(objects)
objects := $(patsubst %,obj/%.o,$(objects))

obj/libretro_$(corename).o: target-libretro/libretro.cpp
	$(compiler) $(cppflags) $(flags) -c $< -o $@ -Dlibretro_use_$(corename)

# targets
build: $(objects)
	$(strip $(compiler) -o out/$(name) $(objects) $(link) -Wl,--version-script=target-libretro/link.T -Wl,--no-undefined)

