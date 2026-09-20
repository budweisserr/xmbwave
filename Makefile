# xmbwave - PS3 XMB wave live wallpaper
#
#   make         release, -O3 -march=native (targets the build machine)
#   make debug   -O0 -g3, all warnings, ASan+UBSan, in build/debug

CXX      ?= g++
BUILD    ?= build
PREFIX   ?= /usr/local
TARGET   := xmbwave

SPDLOG := thirdparty/spdlog

WARNINGS := -Wall -Wextra -Wshadow -Wpedantic

ifdef DEBUG
BUILD    := build/debug
# -O0 but keep the ISA the release build uses, so the sanitizers actually cover
# the AVX2 kernel instead of compiling it out.
OPT      := -O0 -g3 -mavx2 -mfma -fno-omit-frame-pointer -fno-common
SAN      := -fsanitize=address,undefined -fno-sanitize-recover=all
CXXFLAGS := -std=c++17 $(OPT) $(WARNINGS) $(SAN) -pthread -MMD -MP
else
OPT      := -O3 -march=native -pipe -ffast-math -fno-math-errno
SAN      :=
CXXFLAGS ?= -std=c++17 $(OPT) $(WARNINGS) -Wno-psabi -pthread -MMD -MP
endif

CPPFLAGS += -Iinclude -isystem $(SPDLOG)/include -DSPDLOG_COMPILED_LIB
LDLIBS   += -lX11 -lXext -lXrandr -lGL -lm -pthread $(SAN)

SRCS        := $(wildcard src/*.cpp)
SPDLOG_SRCS := $(wildcard $(SPDLOG)/src/*.cpp)
OBJS        := $(SRCS:src/%.cpp=$(BUILD)/%.o) $(SPDLOG_SRCS:$(SPDLOG)/src/%.cpp=$(BUILD)/spdlog/%.o)
DEPS        := $(OBJS:.o=.d)

.PHONY: all clean install uninstall check run debug debug-check

all: $(BUILD)/$(TARGET)

# spdlog is a submodule; fetch it on a fresh checkout.
$(SPDLOG)/include/spdlog/spdlog.h:
	git submodule update --init --recursive $(SPDLOG)

$(OBJS): $(SPDLOG)/include/spdlog/spdlog.h

$(BUILD)/$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

# Vendored code: built with the same instrumentation, without our warnings.
$(BUILD)/spdlog/%.o: $(SPDLOG)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -w -c -o $@ $<

-include $(DEPS)

check: $(BUILD)/$(TARGET)
	$(BUILD)/$(TARGET) --selftest

run: $(BUILD)/$(TARGET)
	$(BUILD)/$(TARGET)

# Debug build in its own tree so the two never share objects.
debug:
	$(MAKE) DEBUG=1 all
	@echo "debug build: build/debug/$(TARGET)"

debug-check:
	$(MAKE) DEBUG=1 check

install: $(BUILD)/$(TARGET)
	install -D -m 755 $(BUILD)/$(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	install -D -m 644 config/xmbwave.conf $(DESTDIR)$(PREFIX)/share/xmbwave/xmbwave.conf

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	rm -f $(DESTDIR)$(PREFIX)/share/xmbwave/xmbwave.conf

clean:
	rm -rf $(BUILD)
