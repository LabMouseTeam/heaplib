#
#
#

ifndef PLATFORM
	PLATFORM=harvest
	CC=riscv32-unknown-elf-gcc
	CDIRS=clean_obj
endif

CDIRS=
ifeq ($(PLATFORM), linux)
	TESTS=thread1
	TESTS+=natural
	CDIRS=clean_obj
endif

ifndef PWD
	PWD=$(subst $(TOPLEVEL),,$(shell pwd))
endif

ifndef OBJDIR
	OBJDIR=$(PWD)/obj/.
endif

ifndef CFLAGS
	CFLAGS=-g -ggdb -O3 -fPIC -W -Wall
endif
CFLAGS+=-Iheap/include -Iplatform/$(PLATFORM)/include -DINTERNAL

FILES=\
	heap/src/alloc.o\
	heap/src/region.o\
	platform/$(PLATFORM)/src/printf.o

all: $(AFILES) $(FILES) $(TESTS)

thread1:
	$(CC) -o obj/$@ test/$@.c obj/*.o -lpthread $(CFLAGS) -DDEBUG -Iplatform/$(PLATFORM)/include 
natural:
	$(CC) -o obj/$@ test/$@.c obj/*.o -lpthread $(CFLAGS) -DDEBUG -Iplatform/$(PLATFORM)/include 


$(AFILES):
	$(CC) -c -o $(OBJDIR)/$(subst /,+,$(PWD))+$(subst /,+,$@) $(@:%.o=%.s) $(CFLAGS) -Iplatform/$(PLATFORM)/include 

$(FILES):
	$(CC) -c -o $(OBJDIR)/$(subst /,+,$(PWD))+$(subst /,+,$@) $(@:%.o=%.c) $(CFLAGS) -Iplatform/$(PLATFORM)/include

clean: $(CDIRS)

clean_obj:
	rm -f $(PWD)/obj/*.o
	rm -f $(PWD)/obj/thread1
	rm -f $(PWD)/obj/natural

install: 

