TAG := $(shell git describe --always --dirty || echo UNKNOWN)

default: libcrucible.so
%.so: Makefile

CRUCIBLE_OBJS = \
	chatter.o \
	cleanup.o \
	crc64.o \
	error.o \
	extentwalker.o \
	fd.o \
	fs.o \
	ntoa.o \
	path.o \
	process.o \
	string.o \
	task.o \
	time.o \
	uuid.o \
	.version.o \

libcrucible.so: $(CRUCIBLE_OBJS) -luuid

include ../makeflags
include ../Defines.mk

configure.h: configure.h.in
	$(TEMPLATE_COMPILER)

.depends/%.dep: %.cc configure.h Makefile
	@mkdir -p .depends
	$(CXX) $(CXXFLAGS) -M -MF $@ -MT $(<:.cc=.o) $<

depends.mk: $(CRUCIBLE_OBJS:%.o=.depends/%.dep)
	cat $^ > $@.new
	mv -f $@.new $@

.version.cc: configure.h Makefile ../makeflags *.cc ../include/crucible/*.h
	echo "namespace crucible { const char *VERSION = \"$(TAG)\"; }" > $@.new
	mv -f $@.new $@

include depends.mk

%.o: %.cc ../makeflags
	$(CXX) $(CXXFLAGS) -fPIC -o $@ -c $<

%.so:
	$(CXX) $(LDFLAGS) -fPIC -shared -Wl,-soname,$@ -o $@ $^
