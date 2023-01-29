default: libcrucible.a
%.a: Makefile

CRUCIBLE_OBJS = \
	bytevector.o \
	btrfs-tree.o \
	chatter.o \
	city.o \
	cleanup.o \
	crc64.o \
	error.o \
	extentwalker.o \
	fd.o \
	fs.o \
	multilock.o \
	ntoa.o \
	path.o \
	process.o \
	string.o \
	table.o \
	task.o \
	time.o \
	uname.o \

include ../makeflags
-include ../localconf
include ../Defines.mk

BEES_LDFLAGS = $(LDFLAGS)

configure.h: configure.h.in
	$(TEMPLATE_COMPILER)

%.dep: %.cc configure.h Makefile
	$(CXX) $(BEES_CXXFLAGS) -M -MF $@ -MT $(<:.cc=.o) $<

include $(CRUCIBLE_OBJS:%.o=%.dep)

%.o: %.cc ../makeflags
	$(CXX) $(BEES_CXXFLAGS) -o $@ -c $<

libcrucible.a: $(CRUCIBLE_OBJS)
	$(AR) rcs $@ $^
