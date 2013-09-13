# Interesting make targets:
# - exe: Just the executable. This is the default.
# - src: Source tarball.
# - pkg: Cygwin package.
# - zip: Zip for standalone release.
# - pdf: PDF version of the manual page.
# - clean: Delete generated files.
#
# Variables intended for setting on the make command line.
# - TARGET: target triple for cross compiling
# - RELEASE: release number for packaging
# - DEBUG: define to enable debug build
# - DMALLOC: define to enable the dmalloc heap debugging library
#
# The values of DEBUG and DMALLOC variables do not matter, it's just about
# whether they're defined, so e.g. 'make DEBUG=1' will trigger a debug build.

NAME := mintty

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
  zip_files := docs/readme.html scripts/create_shortcut.js
else ifeq ($(TARGET), x86_64-pc-cygwin)
  platform := cygwin64
  cygport_opts := --64
  zip_files := docs/readme.html scripts/create_shortcut.js
else ifeq ($(TARGET), i686-pc-msys)
  platform := msys
  zip_files := docs/readme-msys.html
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

src_files := $(wildcard Makefile *.c *.h *.rc *.mft COPYING LICENSE* INSTALL)
src_files += $(wildcard docs/$(NAME).1 docs/readme*.html scripts/* icon/*)

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

.PHONY: exe src pkg zip pdf clean

exe := $(NAME).exe
exe: $(exe)
$(exe): $(objs)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
	-du -b $@

src := $(name_ver)-src.tar.bz2
src: $(src)
$(src): $(src_files)
	rm -rf $(name_ver)
	mkdir $(name_ver)
	cp -ax --parents $^ $(name_ver)
	rm -f $@
	tar cjf $@ $(name_ver)
	rm -rf $(name_ver)

ifdef RELEASE
pkg := $(name_ver)-$(RELEASE)
pkg: $(pkg)
$(pkg): pkg.cygport $(src)
	cp pkg.cygport $(pkg).cygport
	cygport $(cygport_opts) $(pkg).cygport almostall
endif

zip := $(name_ver)-$(platform).zip
zip: $(zip)
$(zip): $(exe) $(zip_files)
	zip -9 -j $@ $^
	-du -b $@

pdf := $(name_ver).pdf
pdf: $(pdf)
$(pdf): docs/$(NAME).1
	groff -t -man -Tps $< | ps2pdf - $@

clean:
	rm -rf *.d *.o $(NAME)*

%.o: %.c
	$(CC) -c -MMD -MP $(CPPFLAGS) $(CFLAGS) $<

%.o: %.rc
	$(RC) --preprocessor '$(CC) -E -xc -DRC_INVOKED -MMD -MP $(CPPFLAGS)' $< $*.o

-include $(wildcard *.d)
