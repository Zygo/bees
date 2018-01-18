PROGRAMS = \
	../bin/bees \
	../bin/fiemap \
	../bin/fiewalk \

all: $(PROGRAMS) depends.mk

include ../makeflags

LIBS = -lcrucible -lpthread
LDFLAGS = -L../lib

BEES_OBJS = \
	bees.o \
	bees-context.o \
	bees-hash.o \
	bees-resolve.o \
	bees-roots.o \
	bees-thread.o \
	bees-types.o \

bees-version.c: bees.h $(BEES_OBJS:.o=.cc) Makefile
	echo "const char *BEES_VERSION = \"$(shell git describe --always --dirty || echo UNKNOWN)\";" > bees-version.new.c
	mv -f bees-version.new.c bees-version.c

.depends/%.dep: %.cc Makefile
	@mkdir -p .depends
	$(CXX) $(CXXFLAGS) -M -MF $@ -MT $(<:.cc=.o) $<

depends.mk: $(BEES_OBJS:%.o=.depends/%.dep)
	cat $^ > $@.new
	mv -f $@.new $@

include depends.mk

%.o: %.cc %.h
	$(CXX) $(CXXFLAGS) -o $@ -c $<

../bin/%: %.o
	@echo Implicit bin rule "$<" '->' "$@"
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

../bin/bees: $(BEES_OBJS) bees-version.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -fv *.o bees-version.c
