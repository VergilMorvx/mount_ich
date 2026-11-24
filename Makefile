# Makefile — mount_ich
#
#   make          device build (arm64, -Os)
#   make check    host syntax check
#   make debug    device build with symbols
#   make clean
#
# Copyright (c) 2026 vergilmorvx. All rights reserved.
# Source-available restricted license — see LICENSE. NOT open source.

CC      ?= clang
CFLAGS  ?= -std=c99 -Wall -Wextra -Isrc
LDFLAGS ?=

TARGET  := build/mount_ich
OUT     := $(TARGET)
SRCS    := src/mount_ich.c src/main.c
HDRS    := src/mount_ich.h

.PHONY: all check debug clean

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	@mkdir -p build
	$(CC) -arch arm64 -Os $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

check:
	$(CC) $(CFLAGS) -Wpedantic -fsyntax-only $(SRCS)

debug: $(SRCS) $(HDRS)
	@mkdir -p build
	$(CC) -arch arm64 -O0 -g $(CFLAGS) -o $(OUT) $(SRCS)

clean:
	rm -rf build