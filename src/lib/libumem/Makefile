PROJ_ROOT = ../..
include $(PROJ_ROOT)/Makefile.env

CFLAGS +=-D_KERNEL
CFLAGS += -I${ZVM_PREFIX_ABSPATH}/include  #zlib include
CFLAGS += -I. -I./include -I./include/$(ARCH) -I$(PROJ_ROOT)/lib/libumem/include -I$(PROJ_ROOT)/lib/libavl/include 

all: libumem.a

libumem.a: $(OBJECTS)
	$(AR) rcs $@ $^

clean:
	@$(CLEAN)