.RECIPEPREFIX = >

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
PKGS    := $(shell pkg-config --cflags raylib vterm 2>/dev/null)
LIBS    := $(shell pkg-config --libs raylib vterm 2>/dev/null)

ifeq ($(LIBS),)
LIBS = -lraylib -lvterm
endif

SRC := $(wildcard src/*.c)
HDR := $(wildcard src/headers/*.h)
OBJ := $(SRC:src/%.c=build/%.o)

silly-term: $(OBJ)
> $(CC) $(OBJ) -o $@ $(LIBS) -lutil -lm

build/%.o: src/%.c $(HDR)
> @mkdir -p build
> $(CC) $(CFLAGS) $(PKGS) -c $< -o $@

install-conf:
> mkdir -p $${XDG_CONFIG_HOME:-$$HOME/.config}/silly-term
> cp -n sillyconf.conf $${XDG_CONFIG_HOME:-$$HOME/.config}/silly-term/

clean:
> rm -rf build silly-term

.PHONY: install-conf clean