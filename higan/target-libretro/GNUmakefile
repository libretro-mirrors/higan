name := libretro.so
#flags += -DSFC_SUPERGAMEBOY

#include fc/GNUmakefile
include sfc/GNUmakefile
#include ms/GNUmakefile
#include md/GNUmakefile
#include pce/GNUmakefile
#include gb/GNUmakefile
#include gba/GNUmakefile
#include ws/GNUmakefile
include processor/GNUmakefile

# rules
objects := libretro $(objects)
objects := $(patsubst %,obj/%.o,$(objects))

obj/libretro.o: target-libretro/libretro.cpp

# targets
build: $(objects)
	$(strip $(compiler) -o out/$(name) $(objects) $(link) -Wl,--version-script=target-libretro/link.T -Wl,--no-undefined)

