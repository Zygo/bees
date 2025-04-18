PREFIX ?= /usr
ETC_PREFIX ?= /etc
LIBDIR ?= lib
BINDIR ?= sbin

LIB_PREFIX ?= $(PREFIX)/$(LIBDIR)
LIBEXEC_PREFIX ?= $(LIB_PREFIX)/bees

SYSTEMD_SYSTEM_UNIT_DIR ?= $(shell pkg-config systemd --variable=systemdsystemunitdir)

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

scripts: scripts/beesd scripts/beesd@.service

install_bees: ## Install bees + libs
install_bees: src $(RUN_INSTALL_TESTS)
	install -Dm755 bin/bees	$(DESTDIR)$(LIBEXEC_PREFIX)/bees

install_scripts: ## Install scipts
install_scripts: scripts
	install -Dm755 scripts/beesd $(DESTDIR)$(PREFIX)/$(BINDIR)/beesd
	install -Dm644 scripts/beesd.conf.sample $(DESTDIR)$(ETC_PREFIX)/bees/beesd.conf.sample
ifneq ($(SYSTEMD_SYSTEM_UNIT_DIR),)
	install -Dm644 scripts/beesd@.service $(DESTDIR)$(SYSTEMD_SYSTEM_UNIT_DIR)/beesd@.service
endif

install: ## Install distribution
install: install_bees install_scripts

help: ## Show help
	@fgrep -h "##" $(MAKEFILE_LIST) | fgrep -v fgrep | sed -e 's/\\$$//' | sed -e 's/##/\t/'

bees: reallyall
fly: install
