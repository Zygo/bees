MARKDOWN := $(firstword $(shell command -v cmark-gfm redcarpet markdown2 markdown markdown_py 2>/dev/null || echo markdown))

# If you have cmark-gfm, you get Github-style tables; otherwise, you don't.
ifeq ($(notdir $(MARKDOWN)),cmark-gfm)
MARKDOWN += -e table
endif

.PHONY: docs

docs: $(subst .md,.html,$(wildcard *.md)) index.html ../README.html

%.html: %.md Makefile
	$(MARKDOWN) $< | sed -e 's/\.md/\.html/g' > $@.new
	mv -f $@.new $@

index.md: ../README.md
	sed -e 's:docs/::g' < ../README.md > index.md.new
	mv -f index.md.new index.md
