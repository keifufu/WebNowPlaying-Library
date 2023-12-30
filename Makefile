# There is no linux32 build since who even uses 32bit on linux
CLEAN = rm -rf build
MKDIR = mkdir -p build/obj
CFLAGS = -Wall -Iinclude -Wno-unknown-pragmas -Wno-int-to-void-pointer-cast
CFLAGS += -O2
# CFLAGS += -g

ifeq ($(OS),Windows_NT)
	CLEAN = rmdir /s /q build
	MKDIR = mkdir build\obj
	CFLAGS = /Iinclude /Isrc/windows /std:c++20 /EHsc
	CFLAGS += /O2
#	CFLAGS += /Zi /DEBUG
#	LINK_FLAGS = /DEBUG
endif

all:
	@echo Available targets: linux64, win64, win32

OBJS_LINUX64 = build/obj/wnp_linux_amd64.o build/obj/dp_linux_linux_amd64.o build/obj/cws_linux_amd64.o
OBJS_LINUX64_NODP = build/obj/wnp_linux_amd64_nodp.o build/obj/cws_linux_amd64.o
linux64: | build
	@$(MAKE) --no-print-directory build/libwnp_linux_amd64.a
	@$(MAKE) --no-print-directory build/libwnp_linux_amd64_nodp.a
	@$(MAKE) --no-print-directory examples_linux64
build/obj/%_linux_amd64.o: src/%.c
	clang $(CFLAGS) -c $< -o $@
build/obj/wnp_linux_amd64_nodp.o: src/wnp.c
	clang $(CFLAGS) -DWNP_NODP -c $< -o $@
build/libwnp_linux_amd64.a: $(OBJS_LINUX64)
	ar rcs $@ $^
build/libwnp_linux_amd64_nodp.a: $(OBJS_LINUX64_NODP)
	ar rcs $@ $^
EXAMPLES_LINUX64 = build/all_players_linux_amd64 build/callbacks_linux_amd64 build/simple_linux_amd64
OBJS_EXAMPLES_LINUX64 = build/obj/all_players_linux_amd64.o build/obj/callbacks_linux_amd64.o build/obj/simple_linux_amd64.o
$(OBJS_EXAMPLES_LINUX64): build/obj/%_linux_amd64.o: examples/%.c
	clang $(CFLAGS) -c $< -o $@
$(EXAMPLES_LINUX64): build/%_linux_amd64: build/obj/%_linux_amd64.o build/libwnp_linux_amd64.a
	clang $(CFLAGS) $< -o $@ -Lbuild -lwnp_linux_amd64
examples_linux64: $(EXAMPLES_LINUX64)

OBJS_WIN64 = build/obj/wnp_win64.obj build/obj/dp_windows_win64.obj build/obj/cws_win64.obj
OBJS_WIN64_NODP = build/obj/wnp_win64_nodp.obj build/obj/cws_win64.obj
win64: | build
	@$(MAKE) --no-print-directory build/libwnp_win64.lib
	@$(MAKE) --no-print-directory build/libwnp_win64_nodp.lib
	@$(MAKE) --no-print-directory examples_win64
build/obj/%_win64.obj: src/%.c
	cl $(CFLAGS) /c $< /Fo$@
build/obj/%_win64.obj: src/%.cpp
	cl $(CFLAGS) /c $< /Fo$@
build/obj/wnp_win64_nodp.obj: src/wnp.c
	cl $(CFLAGS) /DWNP_NODP /c $< /Fo$@
build/libwnp_win64.lib: $(OBJS_WIN64)
	lib /OUT:$@ $^ ws2_32.lib gdiplus.lib ole32.lib oleaut32.lib Advapi32.lib
build/libwnp_win64_nodp.lib: $(OBJS_WIN64_NODP)
	lib /OUT:$@ $^ ws2_32.lib
EXAMPLES_WIN64 = build/all_players_win64.exe build/callbacks_win64.exe build/simple_win64.exe
OBJS_EXAMPLES_WIN64 = build/obj/all_players_win64.obj build/obj/callbacks_win64.obj build/obj/simple_win64.obj
$(OBJS_EXAMPLES_WIN64): build/obj/%_win64.obj: examples/%.c
	cl $(CFLAGS) /c $< /Fo$@
$(EXAMPLES_WIN64): build/%_win64.exe: build/obj/%_win64.obj build/libwnp_win64.lib
	link $(LINK_FLAGS) /MACHINE:x64 /LIBPATH:build libwnp_win64.lib $< /OUT:$@
examples_win64: $(EXAMPLES_WIN64)

OBJS_WIN32 = build/obj/wnp_win32.obj build/obj/dp_windows_win32.obj build/obj/cws_win32.obj
OBJS_WIN32_NODP = build/obj/wnp_win32_nodp.obj build/obj/cws_win32.obj
win32: | build
	@$(MAKE) --no-print-directory build/libwnp_win32.lib
	@$(MAKE) --no-print-directory build/libwnp_win32_nodp.lib
	@$(MAKE) --no-print-directory examples_win32
build/obj/%_win32.obj: src/%.c
	cl $(CFLAGS) /c $< /Fo$@
build/obj/%_win32.obj: src/%.cpp
	cl $(CFLAGS) /c $< /Fo$@
build/obj/wnp_win32_nodp.obj: src/wnp.c
	cl $(CFLAGS) /DWNP_NODP /c $< /Fo$@
build/libwnp_win32.lib: $(OBJS_WIN32)
	lib /OUT:$@ $^ ws2_32.lib gdiplus.lib ole32.lib oleaut32.lib Advapi32.lib
build/libwnp_win32_nodp.lib: $(OBJS_WIN32_NODP)
	lib /OUT:$@ $^ ws2_32.lib
EXAMPLES_WIN32 = build/all_players_win32.exe build/callbacks_win32.exe build/simple_win32.exe
OBJS_EXAMPLES_WIN32 = build/obj/all_players_win32.obj build/obj/callbacks_win32.obj build/obj/simple_win32.obj
$(OBJS_EXAMPLES_WIN32): build/obj/%_win32.obj: examples/%.c
	cl $(CFLAGS) /c $< /Fo$@
$(EXAMPLES_WIN32): build/%_win32.exe: build/obj/%_win32.obj build/libwnp_win32.lib
	link $(LINK_FLAGS) /MACHINE:x86 /LIBPATH:build libwnp_win32.lib $< /OUT:$@
examples_win32: $(EXAMPLES_WIN32)

build:
	$(MKDIR)
.PHONY: clean
clean:
	$(CLEAN)