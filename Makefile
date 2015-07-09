# Interesting make targets:
# - exe: Just the executable. This is the default.
# - tar: Source tarball.
# - zip: Zip for standalone release.
# - pkg: Cygwin package.
# - pdf: PDF version of the manual page.
# - clean: Delete generated files.

# Variables intended for setting on the make command line.
# - RELEASE: release number for packaging
# - TARGET: target triple for cross compiling

exe:
	cd src; $(MAKE) exe

zip:
	cd src; $(MAKE) zip

pdf:
	cd src; $(MAKE) pdf

clean:
	cd src; $(MAKE) clean

NAME := mintty
version := \
  $(shell echo $(shell echo VERSION | cpp -P $(CPPFLAGS) --include src/appinfo.h))
name_ver := $(NAME)-$(version)

DIST := release
TARUSER := --owner=root --group=root --owner=mintty --group=cygwin

arch_files := Makefile COPYING LICENSE* INSTALL VERSION
arch_files += src/Makefile src/*.c src/*.h src/*.rc src/*.mft
arch_files += cygwin/*.cygport cygwin/README* cygwin/setup.hint cygwin/hi*.png
arch_files += docs/*.1 docs/*.html icon/*
arch_files += wiki/*
#arch_files += scripts/*

generated := docs/$(NAME).1.html

docs/$(NAME).1.html: docs/$(NAME).1
	cd src; $(MAKE) html
	cp docs/$(NAME).1.html mintty.github.io/

src := $(DIST)/$(name_ver)-src.tar.bz2
tar: $(generated) $(src)
$(src): $(arch_files)
	mkdir -p $(DIST)
	rm -rf $(name_ver)
	mkdir $(name_ver)
	#cp -ax --parents $^ $(name_ver)
	cp -dl --parents $^ $(name_ver)
	rm -f $@
	tar cjf $@ --exclude="*~" $(TARUSER) $(name_ver)
	rm -rf $(name_ver)

REL := 0
arch := $(shell uname -m)

cygport := $(name_ver)-$(REL).cygport
pkg: $(DIST) tar check binpkg srcpkg
$(DIST):
	mkdir $(DIST)

check:
	cd src; $(MAKE) check

binpkg:
	cp cygwin/mintty.cygport $(DIST)/$(cygport)
	cd $(DIST); cygport $(cygport) prep
	cd $(DIST); cygport $(cygport) compile install
	#cd $(DIST); cygport $(cygport) package
	cd $(DIST)/$(name_ver)-$(REL).$(arch)/inst; tar cJf ../$(name_ver)-$(REL).tar.xz $(TARUSER) *

srcpkg: $(DIST)/$(name_ver)-$(REL)-src.tar.xz

$(DIST)/$(name_ver)-$(REL)-src.tar.xz: $(DIST)/$(name_ver)-src.tar.bz2
	cd $(DIST); tar cJf $(name_ver)-$(REL)-src.tar.xz $(TARUSER) $(name_ver)-src.tar.bz2 $(name_ver)-$(REL).cygport

