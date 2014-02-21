PROJ_ROOT = ../..
include $(PROJ_ROOT)/Makefile.env

CFLAGS +=-D_KERNEL
CFLAGS += -I${ZVM_PREFIX_ABSPATH}/include  #zlib include
CFLAGS += -I./include -I./include/$(ARCH) -I$(PROJ_ROOT)/lib/libumem/include -I$(PROJ_ROOT)/lib/libavl/include 
OBJECTS += $(ARCH)/atomic.o

all: libsolkerncompat.a

libsolkerncompat.a: $(OBJECTS)
	$(AR) rcs $@ $^

$(ARCH)/atomic.o: $(ARCH)/atomic.S
	$(CC) -o $@ -c $^ $(CFLAGS)

clean:
	@$(CLEAN)