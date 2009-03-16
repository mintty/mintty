name := mintty
version := 0.3.7

exe := $(name).exe
dir := $(name)-$(version)
srcs := $(wildcard Makefile *.c *.h *.rc *.mft icon/*.ico icon/*.png)
srcs += $(wildcard COPYING LICENSE* INSTALL)
srcs += docs/readme.html docs/mintty.1 scripts/create_shortcut.js
stuff := docs/readme.html docs/mintty.1.pdf scripts/create_shortcut.js

c_srcs := $(wildcard *.c)
rc_srcs := $(wildcard *.rc)
objs := $(c_srcs:.c=.o) $(rc_srcs:.rc=.o)
deps := $(objs:.o=.d)

c_opts := -include std.h -std=gnu99 -Wall -Wextra -Werror -DNDEBUG
code_opts := -march=i586 -mtune=pentium-m -fomit-frame-pointer -Os
ld_opts := --gc-sections -s
libs := -mwindows -lcomctl32 -lcomdlg32 -limm32 -lwinspool -lole32 -luuid

cc := gcc
rc_cpp := $(cc) -E -MMD -xc-header -DRC_INVOKED -DVERSION=$(version)
rc := windres --preprocessor "$(rc_cpp)"

$(exe): $(objs)
	$(cc) -o $@ $^ $(ld_opts) $(libs)
	du -b $@

bin: $(dir)-cygwin.zip
src: $(dir)-src.tgz

$(dir)-cygwin.zip: $(exe) $(stuff)
	rm -f $@
	zip -9 -j $@ $^
	du -b $@

$(dir)-src.tgz: $(srcs)
	rm -rf $(dir)
	mkdir $(dir)
	cp -ax --parents $^ $(dir)
	rm -f $@
	tar czf $@ $(dir)
	rm -rf $(dir)

%.o %.d: %.c Makefile
	$(cc) $< -c -MMD -MP $(c_opts) $(code_opts) -DVERSION=$(version)

%.o %.d: %.rc Makefile
	$(rc) $< $(<:.rc=.o)

%.1.pdf: %.1
	groff -t -man -Tps $< | ps2pdf - $@

clean:
	rm -f *.d *.o *.exe *.zip *.tgz *.stackdump

.PHONY: bin src clean

include $(deps)
