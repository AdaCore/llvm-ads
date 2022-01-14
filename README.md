# llvm-ads: turn LLVM bitcode into Ada specs

## Building

```
git clone git.adacore.com:llvm-project
cd llvm-project/llvm/tools/
git fetch origin release/13.0.0
git checkout origin/release/13.0.0
git clone git@github.com:AdaCore/llvm-ads.git llvm-ads
cd ..
mkdir obj
cd obj
cmake ..
make -j32 llvm-ads
```

## Using

./bin/llvm-ads /usr/local/cuda/nvvm/libdevice/libdevice.10.bc /tmp/libdevice.ads
