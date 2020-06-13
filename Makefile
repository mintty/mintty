# Interesting make targets:
# - exe: Just the executable. This is the default.
# - tar: Source tarball.
# - zip: Zip for standalone release.
# - pkg: Cygwin package.
# - html: HTML version of the manual page.
# - pdf: PDF version of the manual page.
# - clean: Delete generated files.
# - upload: Upload cygwin packages for publishing.
# - ann: Create cygwin announcement mail.
# - _: Create language translation template, update translation files.

# Variables intended for setting on the make command line.
# - RELEASE: release number for packaging
# - TARGET: target triple for cross compiling

# To add a file to the distribution:
# src: add to make variable below: arch_files += ...
# bin: add to cygwin/mintty.cygport

NAME := mintty

exe:
	#cd src; $(MAKE) exe
	#cd src; $(MAKE) bin
	cd src; $(MAKE)

zip:
	cd src; $(MAKE) zip

html: docs/$(NAME).1.html

pdf:
	cd src; $(MAKE) pdf

clean:
	cd src; $(MAKE) clean

version := \
  $(shell echo $(shell echo VERSION | cpp -P $(CPPFLAGS) --include src/appinfo.h))
name_ver := $(NAME)-$(version)

changelogversion := $(shell sed -e '1 s,^\#* *\([0-9.]*\).*,\1,' -e t -e d wiki/Changelog.md)

ver:
	echo checking same version in changelog and source
	test "$(version)" = "$(changelogversion)"
	echo $(version) > VERSION

DIST := release
TARUSER := --owner=root --group=root --owner=mintty --group=cygwin

arch_files := Makefile LICENSE* INSTALL VERSION
arch_files += src/Makefile src/*.c src/*.h src/*.rc src/*.mft
arch_files += src/[!_]*.t src/mk*
arch_files += tools/mintheme tools/getemojis
arch_files += lang/*.pot lang/*.po
arch_files += themes/*[!~] sounds/*.wav sounds/*.WAV sounds/*.md
arch_files += cygwin/*.cygport cygwin/README cygwin/setup.hint cygwin/mintty-debuginfo.hint
arch_files += docs/*.1 docs/*.html icon/*
arch_files += wiki/*
#arch_files += scripts/*

generated := docs/$(NAME).1.html

docs/$(NAME).1.html: docs/$(NAME).1
	cd src; $(MAKE) html
	cp docs/$(NAME).1.html mintty.github.io/

src := $(DIST)/$(name_ver).tar.gz
tar: $(generated) $(src)
$(src): $(arch_files)
	mkdir -p $(DIST)
	rm -rf $(name_ver)
	mkdir $(name_ver)
	#cp -ax --parents $^ $(name_ver)
	cp -dl --parents $^ $(name_ver)
	rm -f $@
	tar czf $@ --exclude="*~" $(TARUSER) $(name_ver)
	rm -rf $(name_ver)

REL := 1
arch := $(shell uname -m)

cygport := $(name_ver)-$(REL).cygport
pkg: $(DIST) ver cop tar check _ srcpkg binpkg
$(DIST):
	mkdir $(DIST)

check:
	cd src; $(MAKE) check

cop:
	grep YEAR.*`date +%Y` src/appinfo.h

_:
	cd src; $(MAKE) _

binpkg:
	cp cygwin/mintty.cygport $(DIST)/$(cygport)
	cd $(DIST); cygport $(cygport) prep
	cd $(DIST); cygport $(cygport) compile install
	# cygport packages would reveal user information;
	# with $$(TARUSER) in $${TAR_OPTIONS}, they are reduced to numeric, 
	# but strangely still the local ids:
	#cd $(DIST); TAR_OPTIONS="$(TARUSER)" cygport $(cygport) package
	# this succeeds to record owner/group "0/0" in the archives:
	#cd $(DIST); TAR_OPTIONS="--owner=0 --group=0" cygport $(cygport) package
	# but we've already established the explicit archive build,
	# which records owner/group "mintty/cygwin" in the archives:
	# binary package:
	cd $(DIST)/$(name_ver)-$(REL).$(arch)/inst; tar cJf ../$(name_ver)-$(REL).tar.xz $(TARUSER) usr/bin etc usr/share
	# debug package:
	cd $(DIST)/$(name_ver)-$(REL).$(arch)/inst; tar cJf ../$(NAME)-debuginfo-$(version)-$(REL).tar.xz $(TARUSER) usr/lib/debug usr/src/debug

srcpkg: $(DIST)/$(name_ver)-$(REL)-src.tar.xz

# deprecated:
#$(DIST)/$(name_ver)-$(REL)-src.tar.xz: $(DIST)/$(name_ver)-src.tar.bz2
#	cp cygwin/mintty.cygport $(DIST)/$(cygport)
#	cd $(DIST); tar cJf $(name_ver)-$(REL)-src.tar.xz $(TARUSER) $(name_ver)-src.tar.bz2 $(name_ver)-$(REL).cygport

# let's make the source package a .tar.gz (not -src.tar.bz2) to be 
# consistent with github and support cygport magic package name handling:
$(DIST)/$(name_ver)-$(REL)-src.tar.xz: $(DIST)/$(name_ver).tar.gz
	cp cygwin/mintty.cygport $(DIST)/$(cygport)
	cd $(DIST); tar cJf $(name_ver)-$(REL)-src.tar.xz $(TARUSER) $(name_ver).tar.gz $(name_ver)-$(REL).cygport

upload:
	REL=$(REL) cygwin/upload.sftp

announcement=cygwin/announcement.$(version)

ann:	announcement
announcement:
	echo To: cygwin-announce@cygwin.com > $(announcement)
	echo Subject: Updated: mintty $(version) >> $(announcement)
	echo >> $(announcement)
	echo I have uploaded mintty $(version) with the following changes: >> $(announcement)
	sed -n -e 1d -e "/^#/ q" -e p wiki/Changelog.md >> $(announcement)
	echo The homepage is at http://mintty.github.io/ >> $(announcement)
	echo It also links to the issue tracker. >> $(announcement)
	echo  >> $(announcement)
	echo ------ >> $(announcement)
	echo Thomas >> $(announcement)

