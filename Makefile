PREFIX ?= /
LIBDIR ?= lib
USR_PREFIX ?= $(PREFIX)/usr
USRLIB_PREFIX ?= $(USR_PREFIX)/$(LIBDIR)
LIBEXEC_PREFIX ?= $(USRLIB_PREFIX)/bees
SYSTEMD_SYSTEM_UNIT_DIR ?= $(shell pkg-config systemd --variable=systemdsystemunitdir)

MARKDOWN := $(firstword $(shell which markdown markdown2 markdown_py 2>/dev/null || echo markdown))

# allow local configuration to override above variables
-include localconf

DEFAULT_MAKE_TARGET ?= reallyall

ifeq ($(DEFAULT_MAKE_TARGET),reallyall)
	RUN_INSTALL_TESTS = test
endif

default: $(DEFAULT_MAKE_TARGET)

all: lib src scripts README.html
reallyall: all test

clean: ## Cleanup
	git clean -dfx -e localconf

.PHONY: lib src test

lib: ## Build libs
	$(MAKE) -C lib

src: ## Build bins
src: lib
	$(MAKE) -C src

test: ## Run tests
test: lib src
	$(MAKE) -C test

scripts/%: scripts/%.in
	sed -e's#@LIBEXEC_PREFIX@#$(LIBEXEC_PREFIX)#' -e's#@PREFIX@#$(PREFIX)#' $< >$@

scripts: scripts/beesd scripts/beesd@.service

README.html: README.md
	$(MARKDOWN) README.md > README.html.new
	mv -f README.html.new README.html

install_libs: lib
	install -Dm644 lib/libcrucible.so $(DESTDIR)$(USRLIB_PREFIX)/libcrucible.so

install_tools: ## Install support tools + libs
install_tools: install_libs src
	install -Dm755 bin/fiemap $(DESTDIR)$(USR_PREFIX)/bin/fiemap
	install -Dm755 bin/fiewalk $(DESTDIR)$(USR_PREFIX)/sbin/fiewalk

install_bees: ## Install bees + libs
install_bees: install_libs src $(RUN_INSTALL_TESTS)
	install -Dm755 bin/bees	$(DESTDIR)$(LIBEXEC_PREFIX)/bees

install_scripts: ## Install scipts
install_scripts: scripts
	install -Dm755 scripts/beesd $(DESTDIR)$(USR_PREFIX)/sbin/beesd
	install -Dm644 scripts/beesd.conf.sample $(DESTDIR)$(PREFIX)/etc/bees/beesd.conf.sample
ifneq (SYSTEMD_SYSTEM_UNIT_DIR,)
	install -Dm644 scripts/beesd@.service $(DESTDIR)$(SYSTEMD_SYSTEM_UNIT_DIR)/beesd@.service
endif

install: ## Install distribution
install: install_bees install_scripts $(OPTIONAL_INSTALL_TARGETS)

help: ## Show help
	@fgrep -h "##" $(MAKEFILE_LIST) | fgrep -v fgrep | sed -e 's/\\$$//' | sed -e 's/##/\t/'

bees: all
fly: install
