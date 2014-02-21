PROJ_ROOT = ../..
include $(PROJ_ROOT)/Makefile.env

CFLAGS += -I./include -I./include/$(ARCH)
OBJECTS += $(ARCH)/atomic.o

all: libsolcompat.a

libsolcompat.a: $(OBJECTS)
	$(AR) rcs $@ $^

$(ARCH)/atomic.o: $(ARCH)/atomic.S
	$(CC) -o $@ -c $^ $(CFLAGS)

clean:
	@$(CLEAN)