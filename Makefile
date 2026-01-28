# Makefile for SyncTime - Amiga NTP Clock Synchronizer
# Usage: make / make clean
# Override: make PREFIX=/opt/amiga

PREFIX ?= /opt/amiga
CC      = $(PREFIX)/bin/m68k-amigaos-gcc

VERSION := $(shell cat version.txt | tr -d '\n')
HASH    := $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")
STAMP   := $(shell date '+%Y-%m-%d %H:%M')

CFLAGS  ?= -O2 -Wall -Wno-pointer-sign
CFLAGS  += '-DVERSION_STRING="$(VERSION)"' \
           '-DCOMMIT_HASH="$(HASH)"' \
           '-DBUILD_DATE="$(STAMP)"'
LDFLAGS  = -noixemul
INCLUDES = -Iinclude
LIBS     = -lamiga

SRCDIR = src
SRCS   = $(SRCDIR)/main.c \
         $(SRCDIR)/config.c \
         $(SRCDIR)/network.c \
         $(SRCDIR)/sntp.c \
         $(SRCDIR)/clock.c \
         $(SRCDIR)/window.c

OBJS = $(SRCS:.c=.o)
OUT  = SyncTime

.PHONY: all clean

all: $(OUT)

$(OUT): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c include/synctime.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -f $(OBJS) $(OUT)
