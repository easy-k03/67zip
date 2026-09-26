# Portable build for the POSIX family:
#   glibc/musl Linux (including NixOS), Android, macOS, iOS,
#   FreeBSD/NetBSD/OpenBSD, DragonFly, GhostBSD, Solaris/illumos/OmniOS, Haiku.
# Windows is not built here yet; see src/port.hpp for the seam.
#
# NixOS has no /usr and no /usr/local. Headers and libraries live in
# /nix/store and are found through pkg-config, which nix-shell / nix develop
# put on PATH. Do not add -I/usr/include or -L/usr/lib.
#
#   nix-shell -p zlib xz pkg-config --run 'make && make install PREFIX=$out'
#   make CXX=clang++ ZLIB_CFLAGS=... LZMA_LIBS=-llzma

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?=
LDFLAGS  ?=
LDLIBS   ?=

# Default matches FHS. A Nix derivation must pass PREFIX=$out (or $prefix);
# the default is never consulted there because /usr/local does not exist.
PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin

# pkg-config is the source of truth on NixOS. It is optional elsewhere:
# Android NDK, Haiku and some cross compilers do not ship it.
PKG_CONFIG ?= pkg-config
ifneq ($(shell $(PKG_CONFIG) --exists zlib liblzma && echo yes),)
  CPPFLAGS += $(shell $(PKG_CONFIG) --cflags zlib liblzma)
  LDLIBS   += $(shell $(PKG_CONFIG) --libs zlib liblzma)
else
  CPPFLAGS += $(ZLIB_CFLAGS) $(LZMA_CFLAGS)
  LDLIBS   += $(ZLIB_LIBS) $(LZMA_LIBS)
  ifeq ($(origin ZLIB_LIBS), undefined)
    LDLIBS += -lz
  endif
  ifeq ($(origin LZMA_LIBS), undefined)
    LDLIBS += -llzma
  endif
endif

# libstdc++ / libc++ need pthread on glibc. musl, Android, BSD, macOS and
# Solaris pull it in differently; linking it is harmless where it exists and
# can be cleared with `make LDLIBS=` plus an explicit list.
PTHREAD_LIB ?= -pthread
CXXFLAGS += $(PTHREAD_LIB)
LDFLAGS  += $(PTHREAD_LIB)

TARGET   := 67zip
SRCS     := src/main.cpp src/port.cpp
OBJS     := $(SRCS:.cpp=.o)

.PHONY: all clean install uninstall test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

src/%.o: src/%.cpp src/67zip.hpp src/port.hpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

port_test: src/port_test.cpp src/port.cpp src/port.hpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ src/port_test.cpp src/port.cpp

test: port_test $(TARGET)
	./port_test

clean:
	rm -f $(OBJS) $(TARGET) port_test

install: $(TARGET)
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/$(TARGET)

uninstall:
	rm -f $(BINDIR)/$(TARGET)
