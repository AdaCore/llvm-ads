# llvm-ads: turn LLVM bitcode into Ada specs

## Building

llvm-ads depends on LLVM. If you have LLVM installed, you can compile llvm-ads
by invoking make in llvm-ads' source directory. The resulting executable will be
located at `bin/llvm-ads`.

## Using

```
./bin/llvm-ads /usr/local/cuda/nvvm/libdevice/libdevice.10.bc /tmp/libdevice.ads
```
