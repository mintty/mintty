name := mintty

rev := $(shell svn info 2>/dev/null | grep Revision | sed "s/Revision: //" || echo 0)
ifneq ($(rev),)
defines := -DREVISION=$(rev) -DBRANCH=$(shell basename `pwd`)
endif

version := $(shell echo $(shell echo VERSION | cpp -P $(defines) --include appinfo.h))

exe := $(name).exe
dir := $(name)-$(version)

stuff := docs/readme.html scripts/create_shortcut.js

srcs := $(wildcard Makefile *.c *.h *.rc *.mft icon/*.ico icon/*.png)
srcs += $(wildcard COPYING LICENSE* INSTALL)
srcs += docs/mintty.1 $(stuff)

c_srcs := $(wildcard *.c)
rc_srcs := $(wildcard *.rc)
objs := $(c_srcs:.c=.o) $(rc_srcs:.rc=.o)
deps := $(objs:.o=.d)

cpp_opts = -MMD -MP $(defines)

cc_opts =  \
  $(cpp_opts) -DNDEBUG -include std.h \
  -std=gnu99 -Wall -Wextra -Werror \
  -march=i586 -mtune=pentium-m -fomit-frame-pointer -Os
ld_opts := -s
libs := -mwindows -lcomctl32 -limm32 -lwinspool -lole32 -luuid

cc := gcc
rc_cpp := $(cc) -E -xc-header -DRC_INVOKED $(cpp_opts)
rc := windres --preprocessor '$(rc_cpp)'

$(exe): $(objs)
	$(cc) -o $@ $^ $(ld_opts) $(libs)
	du -b $@


bin = $(dir)-cygwin17.zip
src = $(dir)-src.tar.bz2
doc = $(dir).pdf

all: bin src doc

bin: $(bin)
src: $(src)
doc: $(docs)

$(bin): $(exe) $(stuff)
	rm -f $@
	zip -9 -j $@ $^
	du -b $@

$(src): $(srcs)
	rm -rf $(dir)
	mkdir $(dir)
	cp -ax --parents $^ $(dir)
	rm -f $@
	tar cjf $@ $(dir)
	rm -rf $(dir)

$(doc): docs/$(name).1.pdf
	cp $< $@

%.o %.d: %.c
	$(cc) $< -c $(cc_opts)

%.o %.d: %.rc
	$(rc) $< $(<:.rc=.o)

%.1.pdf: %.1
	groff -t -man -Tps $< | ps2pdf - $@

clean:
	rm -f *.d *.o *.exe *.zip *.bz2 *.stackdump *.pdf docs/*.pdf

.PHONY: all src bin src doc clean

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),doc)
include $(deps)
endif
endif
