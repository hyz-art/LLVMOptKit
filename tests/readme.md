### copy-elimination

```
# CHECK-LABEL: test_redundant_copy:
# CHECK-NEXT: add a0, a0, a1
# CHECK-NEXT: ret

测原Pass
./build/bin/llc -march=riscv32 -O2 -riscv-enable-copy-propagation=true test/test-copy-fixed.mir -o -

自写Pass
 # 链接
LLVM_BUILD=/root/llvm-project/build
CLANG=$LLVM_BUILD/bin/clang++

$LLVM_BUILD/bin/clang++ -fPIC -shared \
  $($LLVM_BUILD/bin/llvm-config --cxxflags) \
  RISCVRedundantCopyElim.cpp \
  -o RISCVRedundantCopyElim.so \
  -L$LLVM_BUILD/lib \
  -Wl,-rpath=$LLVM_BUILD/lib \
  -Wl,--enable-new-dtags \
  $($LLVM_BUILD/bin/llvm-config --libs codegen)
  
$LLVM_BUILD/bin/clang++ -fPIC -shared   $($LLVM_BUILD/bin/llvm-config --cxxflags)   RISCVRedundantCopyElim.cpp   -o RISCVRedundantCopyElim.so   -L$LLVM_BUILD/lib   -Wl,-rpath=$LLVM_BUILD/lib   -Wl,--enable-new-dtags   $($LLVM_BUILD/bin/llvm-config --libs codegen)

 # 使用
  $($LLVM_BUILD/bin/llvm-config --cxxflags) \
  $($LLVM_BUILD/bin/llvm-config --cxxflags) \
/root/llvm-project/build/bin/llc -load ./RISCVRedundantCopyElim.so \
 -run-pass riscv-redundant-copy-elim -march=riscv32 -O0 tests/test-copy-fixed.mir -o -

```