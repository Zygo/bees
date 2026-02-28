PREFIX ?= /usr
ETC_PREFIX ?= /etc
LIBDIR ?= lib
BINDIR ?= sbin

LIB_PREFIX ?= $(PREFIX)/$(LIBDIR)
LIBEXEC_PREFIX ?= $(LIB_PREFIX)/bees

SYSTEMD_SYSTEM_UNIT_DIR ?= $(shell pkg-config systemd --variable=systemdsystemunitdir)
OPENRC_INITD_DIR ?=

BEES_VERSION ?= $(shell git describe --always --dirty || echo UNKNOWN)

# allow local configuration to override above variables
-include localconf

DEFAULT_MAKE_TARGET ?= reallyall

ifeq ($(DEFAULT_MAKE_TARGET),reallyall)
	RUN_INSTALL_TESTS = test
endif

include Defines.mk

default: $(DEFAULT_MAKE_TARGET)

all: lib src scripts
reallyall: all doc test

clean: ## Cleanup
	git clean -dfx -e localconf

.PHONY: lib src test doc

lib: ## Build libs
	+$(MAKE) TAG="$(BEES_VERSION)" -C lib

src: ## Build bins
src: lib
	+$(MAKE) BEES_VERSION="$(BEES_VERSION)" -C src

test: ## Run tests
test: lib src
	+$(MAKE) -C test

doc: ## Build docs
	+$(MAKE) -C docs

scripts/%: scripts/%.in
	$(TEMPLATE_COMPILER)

scripts: scripts/beesd scripts/beesd@.service scripts/bees.initd scripts/bees.confd

install_bees: ## Install bees + libs
install_bees: src $(RUN_INSTALL_TESTS)
	install -Dm755 bin/bees	$(DESTDIR)$(LIBEXEC_PREFIX)/bees

install_scripts: ## Install scripts
install_scripts: scripts
	@if [ -n "$(SYSTEMD_SYSTEM_UNIT_DIR)" ] && [ -n "$(OPENRC_INITD_DIR)" ]; then \
		echo "ERROR: Both SYSTEMD_SYSTEM_UNIT_DIR and OPENRC_INITD_DIR are set." >&2; \
		echo "  Installing both init systems on the same host would cause bees" >&2; \
		echo "  to start twice on the same filesystem.  Set only one." >&2; \
		echo "  SYSTEMD_SYSTEM_UNIT_DIR=$(SYSTEMD_SYSTEM_UNIT_DIR)" >&2; \
		echo "  OPENRC_INITD_DIR=$(OPENRC_INITD_DIR)" >&2; \
		exit 1; \
	fi
	install -Dm755 scripts/beesd $(DESTDIR)$(PREFIX)/$(BINDIR)/beesd
	install -Dm644 scripts/beesd.conf.sample $(DESTDIR)$(ETC_PREFIX)/bees/beesd.conf.sample
ifneq ($(SYSTEMD_SYSTEM_UNIT_DIR),)
	install -Dm644 scripts/beesd@.service $(DESTDIR)$(SYSTEMD_SYSTEM_UNIT_DIR)/beesd@.service
endif
ifneq ($(OPENRC_INITD_DIR),)
	install -Dm755 scripts/bees.initd $(DESTDIR)$(OPENRC_INITD_DIR)/bees
	install -Dm644 scripts/bees.confd $(DESTDIR)$(ETC_PREFIX)/conf.d/bees
endif

install: ## Install distribution
install: install_bees install_scripts

help: ## Show help
	@fgrep -h "##" $(MAKEFILE_LIST) | fgrep -v fgrep | sed -e 's/\\$$//' | sed -e 's/##/\t/'

bees: reallyall
fly: install
