PREFIX ?= /
LIBDIR ?= lib
USR_PREFIX ?= $(PREFIX)/usr
USRLIB_PREFIX ?= $(USR_PREFIX)/$(LIBDIR)
SYSTEMD_LIB_PREFIX ?= $(PREFIX)/lib/systemd
LIBEXEC_PREFIX ?= $(USRLIB_PREFIX)/bees

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

install_bees: ## Install bees + libs
install_bees: lib src $(RUN_INSTALL_TESTS)
	install -Dm644 lib/libcrucible.so $(DESTDIR)$(USRLIB_PREFIX)/libcrucible.so
	install -Dm755 bin/bees	$(DESTDIR)$(LIBEXEC_PREFIX)/bees

install_scripts: ## Install scipts
install_scripts: scripts
	install -Dm755 scripts/beesd $(DESTDIR)$(USR_PREFIX)/sbin/beesd
	install -Dm644 scripts/beesd.conf.sample $(DESTDIR)$(PREFIX)/etc/bees/beesd.conf.sample
	install -Dm644 scripts/beesd@.service $(DESTDIR)$(SYSTEMD_LIB_PREFIX)/system/beesd@.service

install: ## Install distribution
install: install_bees install_scripts

help: ## Show help
	@fgrep -h "##" $(MAKEFILE_LIST) | fgrep -v fgrep | sed -e 's/\\$$//' | sed -e 's/##/\t/'

bees: all
fly: install
