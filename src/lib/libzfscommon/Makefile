PROJ_ROOT = ../..
include $(PROJ_ROOT)/Makefile.env

CFLAGS += -I. -I./include -I$(PROJ_ROOT)/lib/libumem/include -I$(PROJ_ROOT)/lib/libavl/include
CFLAGS += -I$(PROJ_ROOT)/lib/libzpool/include -I$(PROJ_ROOT)/lib/libsolcompat/include
CFLAGS += -I$(PROJ_ROOT)/lib/libnvpair/include

all: libzfscommon.a

libzfscommon.a: $(OBJECTS)
	$(AR) rcs $@ $^

clean:
	@$(CLEAN)