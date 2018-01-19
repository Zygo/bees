PROGRAMS = \
	chatter \
	crc64 \
	fd \
	limits \
	path \
	process \
	task \

all: test

test: $(PROGRAMS) Makefile

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

%.o: %.cc %.h ../makeflags Makefile
	@echo "Implicit rule %.o: %.cc"
	$(CXX) $(CXXFLAGS) -o $@ -c $<

$(PROGRAMS): %: %.o ../makeflags Makefile
	@echo "Implicit rule %: %.o"
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)
	./$@

clean:
	rm -fv $(PROGRAMS:%=%.o) $(PROGRAMS)
