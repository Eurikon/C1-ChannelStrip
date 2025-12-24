# Standard Makefile for C1-ChannelStrip VCV Rack Plugin
# Cross-platform compatible (Windows/Linux/macOS)
# Works when cloned into Rack/plugins/C1-ChannelStrip/

# Use relative path to Rack source (standard VCV Rack pattern)
# Assumes plugin is in Rack/plugins/C1-ChannelStrip/
RACK_DIR ?= ../..

# Common include paths (apply to both C and C++ compilation)
FLAGS += -Idep/libebur128/ebur128 -Idep/libebur128/ebur128/queue

# C++ specific flags
CXXFLAGS += -std=c++17 -Ishared/include

# Source files
SOURCES += $(wildcard src/*.cpp)
SOURCES += shared/src/EqAnalysisEngine.cpp
SOURCES += shared/src/VCACompressor.cpp
SOURCES += shared/src/FETCompressor.cpp
SOURCES += shared/src/OpticalCompressor.cpp
SOURCES += shared/src/VariMuCompressor.cpp
SOURCES += dep/libebur128/ebur128/ebur128.c

# Distributables
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

# Restore git submodules (called by toolchain after cleandep)
dep:
	git submodule update --init --recursive

# Include VCV Rack plugin build system
include $(RACK_DIR)/plugin.mk
