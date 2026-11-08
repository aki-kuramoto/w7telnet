# Makefile for w7telnet-listen (the w7telnet telnet listener).
#
# Native builds (macOS / Linux / other POSIX):
#   make            # same as "make posix": builds ./w7telnet-listen for this host
#   make posix      # -> ./w7telnet-listen
#
# Cross-compile the Windows binaries from a POSIX host (needs mingw-w64):
#   make win-x64    # -> ./w7telnet-listen-x64.exe
#   make win-x86    # -> ./w7telnet-listen-x86.exe
#   make win        # both of the above
#
#   make all        # posix + (best effort) the Windows binaries
#   make clean
#
# On Windows itself, without make, use one of:
#   src\windows\build.bat       (MSYS2 / mingw-w64 gcc, CRT-free)
#   src\windows\build-msvc.bat  (MSVC, static CRT /MT)

CORE      := src/core/telnet.c
POSIX_SRC := src/posix/telnet-listen.c $(CORE)
WIN_SRC   := src/windows/telnet-listen.c $(CORE)

# --- POSIX (native) -------------------------------------------------------
CC        ?= cc
CFLAGS    := -O2 -Wall -Wextra -Isrc
POSIX_LIBS := -lpthread

# forkpty() is in -lutil on Linux; on macOS it is part of libSystem.
UNAME := $(shell uname -s)
ifeq ($(UNAME),Linux)
  POSIX_LIBS += -lutil
endif

# --- Windows (mingw-w64 cross-compile, CRT stripped) ----------------------
MINGW64   := x86_64-w64-mingw32-gcc
MINGW32   := i686-w64-mingw32-gcc
WIN_FLAGS := -O2 -s -ffreestanding -fno-builtin -fno-stack-protector \
             -fno-tree-loop-distribute-patterns -nostdlib -e mainCRTStartup -Isrc
WIN_LIBS  := -lws2_32 -lkernel32 -lgcc

.PHONY: all posix win win-x64 win-x86 clean

posix: w7telnet-listen

w7telnet-listen: $(POSIX_SRC)
	$(CC) $(CFLAGS) -o $@ $(POSIX_SRC) $(POSIX_LIBS)

win: win-x64 win-x86
win-x64: w7telnet-listen-x64.exe
win-x86: w7telnet-listen-x86.exe

w7telnet-listen-x64.exe: $(WIN_SRC)
	$(MINGW64) $(WIN_FLAGS) -o $@ $(WIN_SRC) $(WIN_LIBS)

w7telnet-listen-x86.exe: $(WIN_SRC)
	$(MINGW32) $(WIN_FLAGS) -o $@ $(WIN_SRC) $(WIN_LIBS)

all: posix
	-$(MAKE) win-x64
	-$(MAKE) win-x86

clean:
	rm -f w7telnet-listen w7telnet-listen-x64.exe w7telnet-listen-x86.exe
