# SPDX-License-Identifier: GPL-3.0-or-later
CXX = g++
CXXFLAGS = -std=c++20 -O3 -Wall -Wextra -pedantic
BUILD = ../build/recipes

.PHONY: all test
all: $(BUILD)/kleene
$(BUILD)/kleene: kleene.cpp kleene_source.h ../tools/spra_word_state.h
	mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@
test: all
	bash tests/test_kl_source.sh
	bash tests/test_limits.sh
