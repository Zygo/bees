default: libcrucible.so

OBJS = \
	crc64.o \
	chatter.o \
	error.o \
	extentwalker.o \
	fd.o \
	fs.o \
	ntoa.o \
	path.o \
	process.o \
	string.o \
	time.o \
	uuid.o \
	.version.o \

include ../makeflags

depends.mk: *.c *.cc
	for x in *.c; do $(CC) $(CFLAGS) -M "$$x"; done > depends.mk.new
	for x in *.cc; do $(CXX) $(CXXFLAGS) -M "$$x"; done >> depends.mk.new
	mv -fv depends.mk.new depends.mk

.version.cc: Makefile ../makeflags *.c *.cc ../include/crucible/*.h
	echo "namespace crucible { const char *VERSION = \"$(shell git describe --always --dirty || echo UNKNOWN)\"; }" > .version.new.cc
	mv -f .version.new.cc .version.cc

-include depends.mk

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

%.o: %.cc ../include/crucible/%.h
	$(CXX) $(CXXFLAGS) -o $@ -c $<

libcrucible.so: $(OBJS) Makefile
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) -shared -luuid
