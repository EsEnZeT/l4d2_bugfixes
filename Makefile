# Makefile
HX_SOURCEMOD = ../sourcemod
HX_SDKL4D2 = ../hl2sdk
HX_METAMOD = ../mmsource
#
# l4d2_bugfixes.ext.so
#
HX_INCLUDE = -I$(HX_SDKL4D2)/public/game/server -I$(HX_SDKL4D2)/common -I$(HX_SDKL4D2)/game/shared -I. -I$(HX_SDKL4D2)/public -I$(HX_SDKL4D2)/public/engine -I$(HX_SDKL4D2)/public/mathlib -I$(HX_SDKL4D2)/public/tier0 -I$(HX_SDKL4D2)/public/tier1 -I$(HX_METAMOD)/core -I$(HX_METAMOD)/core/sourcehook -I$(HX_SOURCEMOD)/public -I$(HX_SOURCEMOD)/public/extensions -I$(HX_SOURCEMOD)/sourcepawn/include
#
HX_QWERTY = -D_LINUX \
	-Dstricmp=strcasecmp \
	-D_stricmp=strcasecmp \
	-D_strnicmp=strncasecmp \
	-Dstrnicmp=strncasecmp \
	-D_snprintf=snprintf \
	-D_vsnprintf=vsnprintf \
	-D_alloca=alloca \
	-Dstrcmpi=strcasecmp \
	-Wall \
	-Werror \
	-Wno-switch \
	-Wno-unused \
	-msse \
	-DSOURCEMOD_BUILD \
	-DHAVE_STDINT_H \
	-m32 \
	-DNDEBUG \
	-O2 \
	-funroll-loops \
	-pipe \
	-fno-strict-aliasing \
	-fvisibility=hidden \
	-DCOMPILER_GCC \
	-mfpmath=sse

CPP_FLAGS = -Wno-non-virtual-dtor \
	-fvisibility-inlines-hidden \
	-fno-exceptions \
	-fno-rtti \
	-std=c++11
#
HX_SO = l4d2_release/smsdk_ext.o \
	l4d2_release/detours.o \
	l4d2_release/extension.o \
	l4d2_release/asm.o
#
all:
	mkdir -p l4d2_release
	ln -sf $(HX_SOURCEMOD)/public/smsdk_ext.cpp
	ln -sf $(HX_SDKL4D2)/lib/linux/libvstdlib_srv.so libvstdlib_srv.so;
	ln -sf $(HX_SDKL4D2)/lib/linux/libtier0_srv.so libtier0_srv.so;
#
	gcc $(HX_INCLUDE) $(HX_QWERTY) $(CPP_FLAGS) -o l4d2_release/smsdk_ext.o -c smsdk_ext.cpp
	gcc $(HX_INCLUDE) $(HX_QWERTY) $(CPP_FLAGS) -o l4d2_release/detours.o -c CDetour/detours.cpp
	gcc $(HX_INCLUDE) $(HX_QWERTY) $(CPP_FLAGS) -o l4d2_release/extension.o -c extension.cpp
	gcc $(HX_INCLUDE) $(HX_QWERTY) -o l4d2_release/asm.o -c asm/asm.c
#
	gcc $(HX_SO) $(HX_SDKL4D2)/lib/linux/tier1_i486.a $(HX_SDKL4D2)/lib/linux/mathlib_i486.a libvstdlib_srv.so libtier0_srv.so -static-libgcc -shared -m32 -lm -ldl -o l4d2_release/l4d2_bugfixes.ext.so
#
	rm -rf l4d2_release/*.o
