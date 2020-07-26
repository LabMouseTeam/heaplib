#
#
#

ifndef PLATFORM
	PLATFORM=linux
endif

CDIRS=
ifeq ($(PLATFORM), linux)
	TESTS=thread1
	TESTS+=natural
	CDIRS=clean_obj
endif

ifndef OBJDIR
	OBJDIR=`pwd`/obj
endif

ifndef PWD
	PWD=$(subst $(TOPLEVEL),,$(shell pwd))
endif

ifndef CFLAGS
	CFLAGS=-g -ggdb -static -O3 -fPIC -W -Wall
endif
CFLAGS+=-Iheap/include -Iplatform/$(PLATFORM)/include

FILES=\
	heap/src/alloc.o\
	heap/src/region.o\
	platform/$(PLATFORM)/src/printf.o

all: $(AFILES) $(FILES) $(TESTS)

thread1:
	$(CC) -o obj/$@ test/$@.c obj/*.o -lpthread $(CFLAGS) -DDEBUG
natural:
	$(CC) -o obj/$@ test/$@.c obj/*.o -lpthread $(CFLAGS) -DDEBUG

$(AFILES):
	$(CC) -c -o $(OBJDIR)/$(subst /,+,$(PWD))+$(subst /,+,$@) $(@:%.o=%.s) $(CFLAGS)

$(FILES):
	$(CC) -c -o $(OBJDIR)/$(subst /,+,$(PWD))+$(subst /,+,$@) $(@:%.o=%.c) $(CFLAGS)

clean: $(CDIRS)

clean_obj:
	rm -f $(PWD)/obj/*.o
	rm -f $(PWD)/obj/thread1
	rm -f $(PWD)/obj/natural

install: 

