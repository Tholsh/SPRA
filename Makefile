# SPDX-License-Identifier: GPL-3.0-or-later
CXX = g++
CXXFLAGS = -std=c++20 -O2 -Wall -Wextra
PROGRAMS = build/reduced_codec build/relabel-words build/paint-waste build/spra-bpe \
           build/literal_check build/verify_sorted build/verify_standalone

.PHONY: all test recipes
all: $(PROGRAMS) recipes

recipes:
	$(MAKE) -C recipes

build:
	mkdir -p $@

build/reduced_codec: tools/reduced_codec.cpp tools/spra_word_state.h | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@
build/relabel-words: tools/relabel_words.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@
build/paint-waste: tools/paint_waste.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@
build/spra-bpe: tools/bpe_layer.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@
build/%: tools/verification/%.cpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

test: all
	./build/literal_check --self-test
	bash tools/tests/test_reduced_codec.sh
	bash tools/tests/test_reduced_alphabet.sh
	bash tools/tests/test_relabel_words.sh
	bash tools/tests/test_bpe_layer.sh
	$(MAKE) -C recipes test
