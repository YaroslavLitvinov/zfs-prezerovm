PROJ_ROOT = ../..
include $(PROJ_ROOT)/Makefile.env

CFLAGS +=-DNATIVE_BUILD
CFLAGS +=-I./include -I$(PROJ_ROOT)/lib/libavl/include -I$(PROJ_ROOT)/lib/libsolcompat/include

all: libuutil.a

libuutil.a: $(OBJECTS)
	$(AR) rcs $@ $^

clean:
	@$(CLEAN)