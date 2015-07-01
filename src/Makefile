#############################################################################
# Interesting make targets:
# - exe: Just the executable. This is the default.
# - zip: Zip for standalone release.
# - clean: Delete generated files.

#############################################################################
# Variables intended for setting on the make command line.
# - RELEASE: release number for packaging
# - TARGET: target triple for cross compiling
#	values: i686-pc-cygwin, x86_64-pc-cygwin, i686-pc-msys
# - DEBUG: define to enable debug build
# - DMALLOC: define to enable the dmalloc heap debugging library
#
# The values of DEBUG and DMALLOC variables do not matter, it's just about
# whether they're defined, so e.g. 'make DEBUG=1' will trigger a debug build.

#############################################################################
NAME := mintty

.PHONY: exe src pkg zip pdf clean

ifdef TARGET
  CC := $(TARGET)-gcc
  RC := $(TARGET)-windres
else
  CC := gcc
  RC := windres
  TARGET := $(shell $(CC) -dumpmachine)
endif

ifeq ($(TARGET), i686-pc-cygwin)
  platform := cygwin32
  cygport_opts := --32
  zip_files := ../docs/readme.html ../scripts/create_shortcut.js
else ifeq ($(TARGET), x86_64-pc-cygwin)
  platform := cygwin64
  cygport_opts := --64
  zip_files := ../docs/readme.html ../scripts/create_shortcut.js
else ifeq ($(TARGET), i686-pc-msys)
  platform := msys
  zip_files := ../docs/readme-msys.html
else
  $(error Target '$(TARGET)' not supported)
endif

CPPFLAGS := -DTARGET=$(TARGET)

ifndef RELEASE
  svn_rev := $(shell svn info 2>/dev/null | grep ^Revision: | sed 's/Revision: //')
  ifneq ($(svn_rev),)
    CPPFLAGS += -DSVN_DIR=$(shell basename "`svn info | grep ^URL:`") \
                -DSVN_REV=$(svn_rev)
  endif
endif

version := \
  $(shell echo $(shell echo VERSION | cpp -P $(CPPFLAGS) --include appinfo.h))
name_ver := $(NAME)-$(version)

#############################################################################
# compilation parameters

c_srcs := $(wildcard *.c)
rc_srcs := $(wildcard *.rc)
objs := $(c_srcs:.c=.o) $(rc_srcs:.rc=.o)

CFLAGS := -std=gnu99 -include std.h -Wall -Wextra -Wundef -Werror

ifeq ($(shell VER=`$(CC) -dumpversion`; expr $${VER%.*} '>=' 4.5), 1)
  CFLAGS += -mtune=atom
endif

LDFLAGS := -L$(shell $(CC) -print-file-name=w32api) -static-libgcc
LDLIBS := -mwindows -lcomctl32 -limm32 -lwinspool -lole32 -luuid

ifdef DEBUG
  CFLAGS += -g
else
  CPPFLAGS += -DNDEBUG
  CFLAGS += -fomit-frame-pointer -O2
  LDFLAGS += -s
endif

ifdef DMALLOC
  CPPFLAGS += -DDMALLOC
  LDLIBS += -ldmallocth
endif

#############################################################################
# build

DEPOPT=-MMD -MP
#DEPOPT=

%.o: %.c
	$(CC) -c $(DEPOPT) $(CPPFLAGS) $(CFLAGS) $<

%.o: %.rc
	$(RC) --preprocessor '$(CC) -E -xc -DRC_INVOKED $(DEPOPT) $(CPPFLAGS)' $< $*.o

exe := $(NAME).exe
exe: $(exe)
$(exe): $(objs)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
	-du -b $@

-include $(wildcard *.d)

#############################################################################
# release targets

DIST := ../release

ifndef RELEASE
RELEASE=0
endif

ifdef RELEASE
pkg := $(name_ver)-$(RELEASE)
cygport := ../cygwin/mintty.cygport
pkg: $(pkg)
$(pkg): $(cygport) $(src)
	cp $(cygport) $(pkg).cygport
	cygport $(cygport_opts) $(pkg).cygport almostall
endif

zip := $(DIST)/$(name_ver)-$(platform).zip
zip: $(zip)
$(zip): $(exe) $(zip_files)
	mkdir -p $(DIST)
	zip -9 -j $@ $^
	-du -b $@

pdf := $(DIST)/$(name_ver).pdf
pdf: $(pdf)
$(pdf): ../docs/$(NAME).1
	groff -t -man -Tps $< | ps2pdf - $@

html := ../docs/$(NAME).1.html
html: $(html)
$(html): ../docs/$(NAME).1
#	manserver ... $< > $@
	groff -t -man -Thtml $< > $@

clean:
	rm -rf *.d *.o $(NAME)*

stripsupp=sed -e "/supportedOS/ s,>.*,>," -e t -e d
defsupp=/usr/lib/default-manifest.o
check:
	if egrep -a -e "^(printf|#define debug)" *.[hc]; then false; fi
	$(stripsupp) res.mft > .osmin
	cat "$(defsupp)" /dev/null | strings | $(stripsupp) > .osdef
	if [ -f "$(defsupp)" ]; then diff .osdef .osmin; else true; fi
	rm -f .osdef .osmin

#############################################################################
# development and debug targets

tags:	*.c *.h
	ctags -w *.c *.h

getappinfo=$(shell echo "$(1)" ; echo "$(1)" | cpp -P $(CPPFLAGS) --include appinfo.h)

ver:
	echo "$(call getappinfo,VERSION)"
	echo "$(call getappinfo,POINT_VERSION)"
	echo "$(call getappinfo,COMMA_VERSION)"
	echo "$(call getappinfo,DECIMAL_VERSION)"
	echo version $(version)

#############################################################################
# end
