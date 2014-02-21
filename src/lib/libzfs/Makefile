PROJ_ROOT = ../..
include $(PROJ_ROOT)/Makefile.env

CFLAGS += -I${ZVM_PREFIX_ABSPATH}/include  #zlib include
CFLAGS += -I. -I./include -I$(PROJ_ROOT)/lib/libumem/include -I$(PROJ_ROOT)/lib/libavl/include
CFLAGS += -I$(PROJ_ROOT)/lib/libnvpair/include -I$(PROJ_ROOT)/lib/libzfscommon/include
CFLAGS += -I$(PROJ_ROOT)/lib/libzpool/include -I$(PROJ_ROOT)/lib/libuutil/include -I$(PROJ_ROOT)/lib/libsolcompat/include

all: libzfs.a

libzfs.a: $(OBJECTS)
	$(AR) rcs $@ $^

clean:
	@$(CLEAN)