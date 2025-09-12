BEES = ../bin/bees

all: $(BEES)

include ../makeflags
-include ../localconf

LIBS = -lcrucible -lpthread -latomic
BEES_LDFLAGS = -L../lib $(LDFLAGS)

BEES_OBJS = \
	bees.o \
	bees-context.o \
	bees-hash.o \
	bees-resolve.o \
	bees-roots.o \
	bees-thread.o \
	bees-trace.o \
	bees-types.o \

ALL_OBJS = $(BEES_OBJS) $(PROGRAM_OBJS)

bees-version.c: bees.h $(BEES_OBJS:.o=.cc) Makefile ../lib/libcrucible.a
	echo "const char *BEES_VERSION = \"$(BEES_VERSION)\";" > bees-version.c.new
	if ! [ -e "$@" ] || ! cmp -s "$@.new" "$@"; then mv -fv $@.new $@; fi

bees-usage.c: bees-usage.txt Makefile
	(echo 'const char *BEES_USAGE = '; sed -r 's/^(.*)$$/"\1\\n"/' < bees-usage.txt; echo ';') > bees-usage.new.c
	mv -f bees-usage.new.c bees-usage.c

%.dep: %.cc Makefile
	$(CXX) $(BEES_CXXFLAGS) -M -MF $@ -MT $(<:.cc=.o) $<

include $(ALL_OBJS:%.o=%.dep)

%.o: %.c ../makeflags
	$(CC) $(BEES_CFLAGS) -o $@ -c $<

%.o: %.cc ../makeflags
	$(CXX) $(BEES_CXXFLAGS) -o $@ -c $<

$(BEES): $(BEES_OBJS) bees-version.o bees-usage.o ../lib/libcrucible.a
	$(CXX) $(BEES_CXXFLAGS) $(BEES_LDFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -fv *.o bees-version.c
