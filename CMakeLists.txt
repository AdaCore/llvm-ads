set(LLVM_LINK_COMPONENTS
  BitReader
  Core
  Support
  )

add_llvm_tool(llvm-ads
  llvm-ads.cpp

  DEPENDS
  intrinsics_gen
  )
