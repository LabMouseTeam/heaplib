#
#
#

OBJDIR=`pwd`/obj
PWD=`pwd`
CFLAGS=-Iheap/include -Iplatform/linux/include -g -ggdb -static -O3
FILES=\
	heap/src/alloc.o\
	heap/src/region.o\
	platform/linux/src/printf.o

PWD=$(subst $(TOPLEVEL),,$(shell pwd))

all: $(AFILES) $(FILES)
	$(CC) -o obj/test test/test.c obj/*.o -lpthread $(CFLAGS)
	$(CC) -o obj/fuzz1 test/fuzz1.c obj/*.o -lpthread $(CFLAGS)
	$(CC) -o obj/thread1 test/thread1.c obj/*.o -lpthread $(CFLAGS)

$(AFILES):
	$(CC) -c -o $(OBJDIR)/$(subst /,+,$(PWD))+$(subst /,+,$@) $(@:%.o=%.s) $(CFLAGS)

$(FILES):
	$(CC) -c -o $(OBJDIR)/$(subst /,+,$(PWD))+$(subst /,+,$@) $(@:%.o=%.c) $(CFLAGS)

clean:

install: 

