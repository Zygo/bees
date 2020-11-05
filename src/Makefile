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

bees-usage.c: bees-usage.txt Makefile
	(echo 'const char *BEES_USAGE = '; sed -r 's/^(.*)$$/"\1\\n"/' < bees-usage.txt; echo ';') > bees-usage.new.c
	mv -f bees-usage.new.c bees-usage.c

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

$(PROGRAMS): ../lib/libcrucible.a

$(BEES): $(BEES_OBJS) bees-version.o bees-usage.o
	$(CXX) $(BEES_CXXFLAGS) $(BEES_LDFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -fv *.o bees-version.c
