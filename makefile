.RECIPEPREFIX = >

CC       ?= cc
TARGET   := silly-term

# Base flags (the user can override with make CFLAGS=...)
CFLAGS   ?= -O2
WARN     := -Wall -Wextra -std=gnu11
INCLUDES := -Isrc -Isrc/headers
DEPFLAGS := -MMD -MP

PKG_CFLAGS := $(shell pkg-config --cflags raylib vterm 2>/dev/null)
PKG_LIBS   := $(shell pkg-config --libs raylib vterm 2>/dev/null)

ifeq ($(PKG_LIBS),)
PKG_LIBS = -lraylib -lvterm
endif

LDLIBS_EXTRA := -lutil -lm

# ---- build modes -----------------------------------------------------------
# MODE: release (default), debug, asan
MODE ?= release

ifeq ($(MODE),debug)
  CFLAGS  := -g -O0 -fno-omit-frame-pointer
else ifeq ($(MODE),asan)
  CFLAGS  := -g -O1 -fno-omit-frame-pointer -fsanitize=address
  LDFLAGS += -fsanitize=address
else ifneq ($(MODE),release)
  $(error Invalid MODE: $(MODE) (use release, debug or asan))
endif

BUILD := build/$(MODE)
BIN   := $(BUILD)/$(TARGET)

SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=$(BUILD)/%.o)
DEP := $(OBJ:.o=.d)

# ---- main targets ----------------------------------------------------------
all: $(TARGET)

# Link: final binary at ./silly-term (symlink to the current mode's build)
$(TARGET): $(BIN)
> @ln -sf $(BIN) $@

$(BIN): $(OBJ)
> $(CC) $(OBJ) -o $@ $(LDFLAGS) $(PKG_LIBS) $(LDLIBS_EXTRA)

$(BUILD)/%.o: src/%.c
> @mkdir -p $(BUILD)
> $(CC) $(CFLAGS) $(WARN) $(INCLUDES) $(PKG_CFLAGS) $(DEPFLAGS) -c $< -o $@

-include $(DEP)

# ---- debug / profiling -----------------------------------------------------
debug:
> $(MAKE) MODE=debug $(TARGET)

asan:
> $(MAKE) MODE=asan $(TARGET)

valgrind:
> $(MAKE) MODE=debug $(TARGET)
> valgrind --leak-check=full --show-leak-kinds=all \
>          --track-origins=yes --num-callers=30 \
>          ./$(TARGET) $(ARGS)

run: $(TARGET)
> ./$(TARGET) $(ARGS)

# ---- config / cleanup ------------------------------------------------------
install-conf:
> mkdir -p $${XDG_CONFIG_HOME:-$$HOME/.config}/silly-term
> cp -n sillyconf.conf $${XDG_CONFIG_HOME:-$$HOME/.config}/silly-term/

clean:
> rm -rf build $(TARGET)

.PHONY: all debug asan valgrind run install-conf clean