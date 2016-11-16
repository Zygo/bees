PROGRAMS = \
	../bin/bees \
	../bin/fiemap \
	../bin/fiewalk \

all: $(PROGRAMS) depends.mk

include ../makeflags

LIBS = -lcrucible -lpthread
LDFLAGS = -L../lib -Wl,-rpath=$(shell realpath ../lib)

depends.mk: Makefile *.cc
	for x in *.cc; do $(CXX) $(CXXFLAGS) -M "$$x"; done > depends.mk.new
	mv -fv depends.mk.new depends.mk

-include depends.mk

%.o: %.cc %.h
	$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

../bin/%: %.o
	@echo Implicit bin rule "$<" '->' "$@"
	$(CXX) $(CXXFLAGS) -o "$@" "$<" $(LDFLAGS) $(LIBS)

BEES_OBJS = \
	bees.o \
	bees-context.o \
	bees-hash.o \
	bees-resolve.o \
	bees-roots.o \
	bees-thread.o \
	bees-types.o \

../bin/bees: $(BEES_OBJS)
	$(CXX) $(CXXFLAGS) -o "$@" $(BEES_OBJS) $(LDFLAGS) $(LIBS)

clean:
	-rm -fv *.o
