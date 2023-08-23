OUTDIR=.

cxxflags := $(shell llvm-config --cxxflags)

llvm_ldflags := $(shell llvm-config --ldflags --libs BitReader Core Support --system-libs)
ldflags := $(llvm_ldflags) -static-libgcc -static-libstdc++

$(OUTDIR)/bin/llvm-ads: llvm-ads.cpp
	mkdir -p $(OUTDIR)/bin
	g++ $(cxxflags) $< $(ldflags) -o $@
