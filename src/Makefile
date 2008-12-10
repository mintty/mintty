exe = mintty.exe

c_srcs := $(wildcard *.c)
rc_srcs := $(wildcard *.rc)
objs := $(c_srcs:.c=.o) $(rc_srcs:.rc=.o)
deps := $(objs:.o=.d)

c_opts := -include std.h -std=gnu99 -Wall -Wextra -Werror
code_opts := -march=i586 -mtune=pentium-m -fomit-frame-pointer -Os
ld_opts := -mwindows -lcomctl32 -lcomdlg32 -limm32 -lwinspool --gc-sections -s

rev := $(shell printf "r%03u" `svn info | grep Revision | sed "s/Revision: //"`)

rc_cpp := gcc -E -MMD -xc-header -DRC_INVOKED -DREVISION=$(rev)

$(exe): $(objs)
	gcc -o $@ $(objs) $(ld_opts)
	du -b $@

%.o %.d: %.c
	gcc $< -c -MMD -MP $(c_opts) $(code_opts) -DREVISION=$(rev)

%.o %.d: %.rc
	windres --preprocessor "$(rc_cpp)" $< $(<:.rc=.o)

%.zip: %.exe
	zip -9 $@ $<
	du -b $@

zip: $(exe:.exe=.zip)

clean:
	rm -f *.d *.o *.exe *.zip *.stackdump

include $(deps)
