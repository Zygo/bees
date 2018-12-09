BEES = ../bin/bees
PROGRAMS = \
	../bin/fiemap \
	../bin/fiewalk \

all: $(BEES) $(PROGRAMS)

include ../makeflags

LIBS = -lcrucible -luuid -lpthread
BEES_LDFLAGS = -L../lib $(LDFLAGS)

BEES_OBJS = \
	bees.o \
	bees-context.o \
	bees-hash.o \
	bees-resolve.o \
	bees-roots.o \
	bees-thread.o \
	bees-types.o \

bees-version.c: bees.h $(BEES_OBJS:.o=.cc) Makefile
	echo "const char *BEES_VERSION = \"$(BEES_VERSION)\";" > bees-version.new.c
	mv -f bees-version.new.c bees-version.c

.depends:
	mkdir -p $@

.depends/%.dep: %.cc Makefile | .depends
	$(CXX) $(BEES_CXXFLAGS) -M -MF $@ -MT $(<:.cc=.o) $<

depends.mk: $(BEES_OBJS:%.o=.depends/%.dep)
	cat $^ > $@.new
	mv -f $@.new $@

include depends.mk

$(BEES_OBJS) fiemap.o fiewalk.o: %.o: %.cc
	$(CXX) $(BEES_CXXFLAGS) -o $@ -c $<

$(PROGRAMS): ../bin/%: %.o
	$(CXX) $(BEES_CXXFLAGS) $(BEES_LDFLAGS) -o $@ $< $(LIBS)

bees-version.o: %.o: %.c
	$(CC) $(BEES_CFLAGS) -o $@ -c $<

$(BEES): $(BEES_OBJS) bees-version.o
	$(CXX) $(BEES_CXXFLAGS) $(BEES_LDFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -fv *.o bees-version.c
