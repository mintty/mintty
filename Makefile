# Interesting make targets:
# - exe: Just the executable. This is the default.
# - tar: Source tarball.
# - release: New release with checks, lang files, git tag.
# - pkg: Cygwin package.
# - upload: Upload cygwin packages for publishing.
# - ann: Create cygwin announcement mail.
# Auxiliary:
# - html: HTML version of the manual page.
# - pdf: PDF version of the manual page.
# - clean: Delete generated files.
# - _: Create language translation template, update translation files.
# - install: Install locally into $(DESTDIR)/
# Obsolete:
# - zip: Zip for standalone release.

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

#############################################################################

DIST := release
# --mtime would stamp all files the same
#TARTIME := --mtime="$(shell date +%Y-%m-%d) 00:00:00"
TARUSER := --owner=root --group=root --owner=mintty --group=cygwin
REL := 1
arch := $(shell uname -m)

#############################################################################
# release

version := \
  $(shell echo $(shell echo VERSION | cpp -P $(CPPFLAGS) --include src/appinfo.h))
name_ver := $(NAME)-$(version)

changelogversion := $(shell sed -e '1 s,^\#* *\([0-9.]*\).*,\1,' -e t -e d wiki/Changelog.md)
# must not set gitversion via := (would be version before tagging)
gitversion=$(shell src/mkvertag)

ver:
	echo $(version) > VERSION
	cd src; $(MAKE) releasever

checkver:
	echo "checking same version in changelog, source (appinfo.h), and git"
	test "$(version)" = "$(changelogversion)"
	test "$(version)" = "$(gitversion)"

tag:
	git tag -f $(version)
	#git push --tags

committed:
	if git status -suno | sed -e "s,^..,," | grep .; then false; fi

hint=\e[42;30m
norm=\e[m

release: check-x11 cop check _ ver
	mkdir -p $(DIST)
	echo -e "$(hint)Now: git commit -a; make tag;$(norm)"
	echo -e "$(hint)Then: make pkg and git push; git push --tags;$(norm)"
	echo -e "$(hint)Last: make upload; make ann; mail cygwin-announce$(norm)"
	echo -e "$(hint)Also: github release$(norm)"
checkrelease: committed tag checkver tar

pkg: release checkrelease cygpkg binver

binver:
	$(DIST)/$(name_ver)-$(REL).$(arch)/inst/usr/bin/mintty -V | grep "mintty $(version) "

check:
	cd src; $(MAKE) check

cop:
	grep YEAR.*`date +%Y` src/appinfo.h

_:
	cd src; $(MAKE) _

check-x11:	src/rgb.t src/composed.t

src/rgb.t:	/usr/share/X11/rgb.txt # X11 color names, from package 'rgb'
	rm -f src/rgb.t
	cd src; $(MAKE) rgb.t

compose_list=/usr/share/X11/locale/en_US.UTF-8/Compose
keysymdefs=/usr/include/X11/keysymdef.h
src/composed.t:	$(compose_list) $(keysymdefs)
	rm -f src/composed.t
	cd src; $(MAKE) composed.t

#############################################################################
# build

arch_files := Makefile LICENSE* INSTALL VERSION config.template
arch_files += src/Makefile src/*.c src/*.h src/*.rc src/*.mft
arch_files += src/[!_]*.t src/mk*
arch_files += tools/mintheme tools/getemojis tools/getflags
arch_files += lang/*.pot lang/*.po
arch_files += themes/*[!~] sounds/*.wav sounds/*.WAV sounds/*.md
arch_files += cygwin/*.cygport
arch_files += docs/*.1 docs/*.html icon/*
arch_files += wiki/*
#arch_files += scripts/*

generated := docs/$(NAME).1.html

docs/$(NAME).1.html: docs/$(NAME).1 src/htmlroff.sed src/htmlhtml.sed
	cd src; $(MAKE) html
	cp docs/$(NAME).1.html mintty.github.io/

src := $(DIST)/$(name_ver).tar.gz
tar: $(generated) $(src)
$(src): $(arch_files)
	mkdir -p $(DIST)
	rm -rf $(name_ver)
	mkdir -p $(name_ver)
	#cp -ax --parents $^ $(name_ver)
	# cp -l fail with mysterious "cannot create hard link..."
	#cp -dl --parents $^ $(name_ver)
	cp -d --parents $^ $(name_ver)
	rm -f $@
	tar czf $@ --exclude="*~" $(TARUSER) $(name_ver)
	rm -rf $(name_ver)

cygport := $(name_ver)-$(REL).cygport
cygpkg:
	cp cygwin/mintty.cygport $(DIST)/$(cygport)
	cd $(DIST); cygport $(cygport) prep
	cd $(DIST); cygport $(cygport) compile install package

#############################################################################
# cygwin

upload:
	cd $(DIST); SSH_KEY=$${ID_RSA-$$HOME/.ssh/id_rsa} cygport $(cygport) upload

announcement=cygwin/announcement.$(version)

ann:	announcement
announcement:
	echo To: cygwin-announce@cygwin.com > $(announcement)
	echo Subject: Updated: mintty $(version) >> $(announcement)
	echo >> $(announcement)
	echo I have uploaded mintty $(version) with the following changes: >> $(announcement)
	sed -n -e 1d -e "/^#/ q" -e p wiki/Changelog.md >> $(announcement)
	echo It is suggested to also install package emojis-noto >> $(announcement)
	echo to enable graphic emojis display in default configuration. >> $(announcement)
	echo  >> $(announcement)
	echo The homepage is at http://mintty.github.io/ >> $(announcement)
	echo It also links to the issue tracker. >> $(announcement)
	echo  >> $(announcement)
	echo ------ >> $(announcement)
	echo Thomas >> $(announcement)

#############################################################################
# local installation

.PHONY: install # prevent file INSTALL to be taken as target install
install:
	mkdir -p $(DESTDIR)/
	echo Installing into $(DESTDIR)/
	# binaries
	mkdir -p $(DESTDIR)/usr/bin
	cp bin/mintty tools/mintheme $(DESTDIR)/usr/bin/
	# manual
	mkdir -p $(DESTDIR)/usr/share/man
	gzip -c docs/mintty.1 > $(DESTDIR)/usr/share/man/mintty.1.gz
	# resources
	mkdir -p $(DESTDIR)/usr/share/mintty/{lang,themes,sounds,icon,emojis}
	cp lang/*.pot lang/*.po $(DESTDIR)/usr/share/mintty/lang/
	cp themes/* $(DESTDIR)/usr/share/mintty/themes/
	cp sounds/*.wav sounds/*.WAV $(DESTDIR)/usr/share/mintty/sounds/
	cp icon/wsl.ico $(DESTDIR)/usr/share/mintty/icon/
	cp tools/getemojis tools/getflags $(DESTDIR)/usr/share/mintty/emojis/
	# icons
	for i in 16 24 32 48 64 256; do mkdir -p $(DESTDIR)/usr/share/icons/hicolor/$${i}x$${i}/apps; cp icon/hi$${i}-apps-mintty.png $(DESTDIR)/usr/share/icons/hicolor/$${i}x$${i}/apps/mintty.png; done
	# enable new icon files
	rm -f $(DESTDIR)/usr/share/icons/hicolor/icon-theme.cache
	# make X11 desktop entry
	# template: /usr/share/cygport/lib/src_install.cygpart
	mkdir -p $(DESTDIR)/usr/share/applications
	echo "[Desktop Entry]" > $(DESTDIR)/usr/share/applications/mintty.desktop
	echo "Version=1.0" >> $(DESTDIR)/usr/share/applications/mintty.desktop
	echo "Name=Mintty" >> $(DESTDIR)/usr/share/applications/mintty.desktop
	echo "Exec=mintty" >> $(DESTDIR)/usr/share/applications/mintty.desktop
	echo "TryExec=mintty" >> $(DESTDIR)/usr/share/applications/mintty.desktop
	echo "Type=Application" >> $(DESTDIR)/usr/share/applications/mintty.desktop
	echo "Icon=mintty" >> $(DESTDIR)/usr/share/applications/mintty.desktop
	echo "Categories=System;TerminalEmulator;" >> $(DESTDIR)/usr/share/applications/mintty.desktop
	echo "OnlyShowIn=X-Cygwin;" >> $(DESTDIR)/usr/share/applications/mintty.desktop

#############################################################################
# end
