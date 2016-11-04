#############################################################################
# Interesting make targets:
# - exe: Just the executable. This is the default.
# - zip: Zip for standalone release.
# - clean: Delete generated files.

# make parameter CCOPT, e.g. for make CCOPT=-Wno-unused

#############################################################################
# Variables intended for setting on the make command line.
# - RELEASE: release number for packaging
# - TARGET: target triple for cross compiling
#	values: i686-pc-cygwin, x86_64-pc-cygwin, i686-pc-msys, x86_64-pc-msys
# - DEBUG: define to enable debug build
# - DMALLOC: define to enable the dmalloc heap debugging library
#
# The values of DEBUG and DMALLOC variables do not matter, it's just about
# whether they're defined, so e.g. 'make DEBUG=1' will trigger a debug build.

#############################################################################
NAME := mintty

.PHONY: exe src pkg zip pdf clean

BINFOLDER = ../bin
#BINDIR = $(BINFOLDER)/$(TARGET)
BINDIR = $(BINFOLDER)/$(platform)

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
  MSYS=$(shell uname -r | sed -e 's,\..*,,')
  ifeq ("$(MSYS)", "1")
    platform := msys
  else
    platform := msys32
  endif
  zip_files := ../docs/readme-msys.html
else ifeq ($(TARGET), x86_64-pc-msys)
  platform := msys64
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
bins := $(patsubst %.o,$(BINDIR)/%.o,$(objs))

CFLAGS := -std=gnu99 -include std.h -Wall -Wextra -Wundef -Werror

ifeq ($(shell VER=`$(CC) -dumpversion`; expr $${VER%.*} '>=' 4.5), 1)
  CFLAGS += -mtune=atom
endif

LDFLAGS := -L$(shell $(CC) -print-file-name=w32api) -static-libgcc
LDLIBS := -mwindows -lcomctl32 -limm32 -lwinmm -lwinspool -lole32 -luuid
#override LDFLAGS += -L$(shell $(CC) -print-file-name=w32api) -static-libgcc
#override LDLIBS += -mwindows -lcomctl32 -limm32 -lwinmm -lwinspool -lole32 -luuid

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

all:	bin

DEPOPT=-MMD -MP
#DEPOPT=

CCOPT=
CCFLAGS:=$(CFLAGS) $(CCOPT)

$(BINDIR)/term%.o: term%.c
	$(CC) -c $(DEPOPT) $(CPPFLAGS) $(CCFLAGS) $< -o $(BINDIR)/term$*.o

$(BINDIR)/%.o: %.c
	$(CC) -c $(DEPOPT) $(CPPFLAGS) $(CCFLAGS) -fstack-check $< -o $(BINDIR)/$*.o

$(BINDIR)/%.o: %.rc %.h %.mft
	$(RC) -c 65001 --preprocessor '$(CC) -E -xc -DRC_INVOKED $(DEPOPT) $(CPPFLAGS)' $< $(BINDIR)/$*.o
	-mv $*.d $(BINDIR)/

exe := $(NAME).exe
exe: $(exe)
$(exe): $(objs)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
	#-du -b $@

bin := $(BINDIR)/$(NAME).exe
bin: $(BINDIR) $(bin)
$(BINDIR):
	mkdir -p $(BINDIR)
$(bin): $(bins)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
	cp -f $@ $(BINFOLDER)/		# for 'dobin' install in .cygport
	# support simpler debugging:
	-test -f `basename $@` && cp -fp $@ ./ || true
	# report size of exe:
	#-type du && du -b $@ || true

#-include $(wildcard *.d)
-include $(wildcard $(BINDIR)/*.d)

#############################################################################
# generate

#WGET=curl -R -O --connect-timeout 55
WGET=wget -N -t 1 --timeout=55

rgb:	# /usr/share/X11/rgb.txt # X11 color names
#	255 250 250		snow
#	->
#		{255, 250, 250, "snow"},
	sed -e 's,	, ,g' -e 's/ *\([0-9][0-9]*\) *\([0-9][0-9]*\) *\([0-9][0-9]*\) *\([^ ].*[^ ]\) */	{\1, \2, \3, "\4"},/' /usr/share/X11/rgb.txt > rgb.t

unidata:	combining.t ambiguous.t combdouble.t combined.t bidiclasses.t

combining.t:	# UnicodeData.txt Blocks.txt
	uniset +cat=Me +cat=Mn +cat=Cf -00AD +1160-11FF +200B +D7B0-D7C6 +D7CB-D7FB c > combining.t

ambiguous.t:	# UnicodeData.txt Blocks.txt WIDTH-A
	uniset +WIDTH-A -cat=Me -cat=Mn -cat=Cf c > ambiguous.t

combdouble=$(shell sed -e "s,^\([^;]*\);.*Mn;23[34];.*, +\1," -e t -e d UnicodeData.txt )

combdouble.t:	# UnicodeData.txt Blocks.txt
	uniset $(combdouble) c > combdouble.t

WIDTH-A:	# UnicodeData.txt Blocks.txt EastAsianWidth.txt
	sh ./mkwidthA

combined.t:	# UnicodeData.txt
	sh ./mkcombinedt > combined.t

bidiclasses.t:	# UnicodeData.txt
	sh ./mkbidiclasses > bidiclasses.t

unicode:	UnicodeData.txt Blocks.txt EastAsianWidth.txt

%.txt:
	$(WGET) http://unicode.org/Public/UNIDATA/$@

#compose_list=/usr/share/X11/locale/en_US.UTF-8/Compose
#keysymdefs=/usr/include/X11/keysymdef.h

composed.t:	# $(compose_list) $(keysymdefs)
	sh ./mkcomposedt > composed.t

XGETTEXT_OPTS=	# --no-location --no-wrap ?

_:	localization
localization:
	xgettext --from-code=UTF-8 -k_ -k_W -k__ -c_ $(XGETTEXT_OPTS) *.c

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
	#-du -b $@

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
	#rm -rf *.d *.o $(NAME)*
	rm -rf $(BINDIR)/*.d $(BINDIR)/*.o $(BINDIR)/$(NAME)*

check:	checksrc checkresource

# check whether supportedOS Id list is up-to-date
stripsupp=sed -e "/supportedOS/ s,>.*,>," -e t -e d
defsupp=/usr/lib/default-manifest.o
checkresource:
	$(stripsupp) res.mft > .osmin
	cat "$(defsupp)" /dev/null | strings | $(stripsupp) > .osdef
	if [ -f "$(defsupp)" ]; then diff .osdef .osmin; else true; fi
	rm -f .osdef .osmin

# check whether sources are free of remaining debugging code
checksrc:
	if egrep -a -e "^(f*printf|#define debug)" *.[hc]; then false; fi
	if egrep -e "^(if|do|for|while) *[({]" *.c; then false; fi
	if egrep -e "\((false|0) *&" *.c; then false; fi
	if egrep -e "& 0($|[^bx])" *.c; then false; fi

#############################################################################
# development and debug targets

tags:	*.c *.h
	ctags -w *.c *.h

_wm.t:	# Windows Message names, only for debugging
#	#define WM_NULL 0x0000
#	->
#		{0x0000, "WM_NULL"},
	sed -e 's/^#define \(WM_[^ ]*\) *\(0x[0-9A-Fa-f]*\)$$/  {\2, "\1"},/' -e t -e d /usr/include/w32api/winuser.h | tr 'abcdef' 'ABCDEF' > _wm.t
	echo '  {0x02E0, "WM_DPICHANGED"},' >> _wm.t
	# or, with include file copied from Windows SDK:
	#sed -e 's/^#define \(WM_[^ ]*\) *\(0x[0-9A-Fa-f]*\)$$/  {\2, "\1"},/' -e t -e d etc/WinUser.h | tr 'abcdef' 'ABCDEF' > _wm.t

_vk.t:	# Windows Virtual Key Code names, only for debugging
#	#define VK_TAB 0x09
#	->
#		{0x09, "VK_TAB"},
	sed -e 's/^#define \(VK_[^ ]*\) \(0x[0-9A-Fa-f]*\)$$/	{\2, "\1"},/' -e t -e d /usr/include/w32api/winuser.h > _vk.t

_winidm.t:	winids.h
	sed -e 's/#define *\(IDM_[^ ]*\).*/	{\1, "\1"},/' -e t -e d winids.h > _winidm.t

target:
	echo TARGET $(TARGET)
	echo platform $(platform)
	echo BINDIR $(BINDIR)
	echo $(bins)

getappinfo=$(shell echo "$(1)" ; echo "$(1)" | cpp -P $(CPPFLAGS) --include appinfo.h)

ver:
	echo "$(call getappinfo,VERSION)"
	echo "$(call getappinfo,POINT_VERSION)"
	echo "$(call getappinfo,COMMA_VERSION)"
	echo "$(call getappinfo,DECIMAL_VERSION)"
	echo version $(version)

#############################################################################
# end
