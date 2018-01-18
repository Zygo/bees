PROGRAMS = \
	chatter \
	crc64 \
	fd \
	limits \
	path \
	process \
	task \

all: test

test: $(PROGRAMS)
	set -x; for prog in $(PROGRAMS); do ./$$prog || exit 1; done

include ../makeflags

LIBS = -lcrucible -lpthread
LDFLAGS = -L../lib -Wl,-rpath=$(shell realpath ../lib)

.depends/%.dep: %.cc tests.h Makefile
	@mkdir -p .depends
	$(CXX) $(CXXFLAGS) -M -MF $@ -MT $(<:.cc=.o) $<

depends.mk: $(PROGRAMS:%=.depends/%.dep)
	cat $^ > $@.new
	mv -f $@.new $@

include depends.mk

%.o: %.cc %.h ../makeflags
	@echo "Implicit rule %.o: %.cc"
	$(CXX) $(CXXFLAGS) -o $@ -c $<

%: %.o ../makeflags
	@echo "Implicit rule %: %.o"
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -o $@ $<

clean:
	rm -fv *.o
