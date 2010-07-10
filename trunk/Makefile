name := mintty

rev := $(shell svn info 2>/dev/null | grep Revision | sed "s/Revision: //" || echo 0)
ifneq ($(rev),)
defines := -DREVISION=$(rev) -DBRANCH=$(shell basename `pwd`)
endif

version := $(shell echo $(shell echo VERSION | cpp -P $(defines) --include appinfo.h))

exe := $(name).exe
dir := $(name)-$(version)

srcs := $(wildcard Makefile *.c *.h *.rc *.mft icon/*.ico icon/*.png)
srcs += $(wildcard COPYING LICENSE* INSTALL docs/* scripts/*)

c_srcs := $(wildcard *.c)
rc_srcs := $(wildcard *.rc)
objs := $(c_srcs:.c=.o) $(rc_srcs:.rc=.o)
deps := $(objs:.o=.d)

cpp_opts = -MMD -MP $(defines)

cc_opts =  \
  $(cpp_opts) -include std.h \
  -std=gnu99 -Wall -Wextra -Werror -Wundef \
  -march=i586 -mtune=pentium-m

ld_opts := -mwindows -lcomctl32 -limm32 -lwinspool -lole32 -luuid 

ifdef debug
cc_opts += -DDMALLOC -g
ld_opts += -ldmallocth
else
cc_opts += -DNDEBUG -fomit-frame-pointer -Os
ld_opts += -s
endif

cc := gcc
rc_cpp := $(cc) -E -xc-header -DRC_INVOKED $(cpp_opts)
rc := windres --preprocessor '$(rc_cpp)'

$(exe): $(objs)
	$(cc) -o $@ $^ $(ld_opts)
	-du -b $@

src = $(dir)-src.tar.bz2
doc = $(dir).pdf
bz2 = $(dir).exe.bz2

cygwin17 = $(dir)-cygwin17.zip
cygwin15 = $(dir)-cygwin15.zip
msys = $(dir)-msys.zip

all: bin src doc

cygwin17: $(cygwin17)
cygwin15: $(cygwin15)
msys: $(msys)
src: $(src)
doc: $(doc)
bz2: $(bz2)

$(bz2): $(exe)
	bzip2 -k $<
	mv $<.bz2 $@

$(cygwin17): $(exe) docs/readme.html scripts/create_shortcut-cygwin17.js
	cp scripts/create_shortcut-cygwin17.js scripts/create_shortcut.js
	rm -f $@
	zip -9 -j $@ $< docs/readme.html scripts/create_shortcut.js
	rm scripts/create_shortcut.js
	-du -b $@

$(cygwin15): $(exe) docs/readme.html scripts/create_shortcut-cygwin15.js
	cp scripts/create_shortcut-cygwin15.js scripts/create_shortcut.js
	rm -f $@
	zip -9 -j $@ $< docs/readme.html scripts/create_shortcut.js
	rm scripts/create_shortcut.js
	-du -b $@

$(msys): $(exe) docs/readme-msys.html
	rm -f $@
	zip -9 -j $@ $< docs/readme-msys.html
	-du -b $@

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
ifneq ($(MAKECMDGOALS),src)
include $(deps)
endif
endif
endif
