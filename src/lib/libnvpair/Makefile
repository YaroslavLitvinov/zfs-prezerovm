PROJ_ROOT = ../..
include $(PROJ_ROOT)/Makefile.env

CFLAGS += -I./include -I$(PROJ_ROOT)/lib/libsolcompat/include

all: libnvpair.a

libnvpair.a: $(OBJECTS)
	$(AR) rcs $@ $^

clean:
	@$(CLEAN)