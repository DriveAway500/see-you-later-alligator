.RECIPEPREFIX = >

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
PKGS    := $(shell pkg-config --cflags raylib vterm 2>/dev/null)
LIBS    := $(shell pkg-config --libs raylib vterm 2>/dev/null)

ifeq ($(LIBS),)
LIBS = -lraylib -lvterm
endif

silly-term: src/main.c src/load_conf.c src/load_conf.h
> $(CC) $(CFLAGS) $(PKGS) src/main.c src/load_conf.c -o $@ $(LIBS) -lutil -lm

install-conf:
> mkdir -p $${XDG_CONFIG_HOME:-$$HOME/.config}/silly-term
> cp -n sillyconf.conf $${XDG_CONFIG_HOME:-$$HOME/.config}/silly-term/

clean:
> rm -f silly-term

.PHONY: install-conf clean