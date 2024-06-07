# UnwindCppExceptions

Custom C++ exceptions using unwind.

## Context

**Disclaimer:** This discussion is very implementation-specific.

In normal C++ code, "throw" dynamically allocates memory.  
When the exception's reference count reaches 0, it is freed.

This is fine for most users. However, in some embedded systems,  
it's important to track all dynamic memory allocations.

There is no API to control allocations/deallocations from throw/catch, so  
it's difficult to track these allocations.

You can use the linker to override global allocation functions,  
but this makes it hard to customize allocations for exceptions.

Linker overrides for `__cxa_allocate_exception` and `__cxa_free_exception`  
also do not work, because libcxxabi calls `__cxa_free_exception` internally,  
and those internal calls are inlined, which means you can't override them.

There's also `setjmp` and `longjmp`, but those don't work with RAII.

## Solution

As a workaround, this code calls runtime APIs directly to throw and catch.  
This makes it possible to customize the allocation and deallocation.

This solution was designed by reverse-engineering libcxxabi source code.  
It throws a "foreign exception", normally for exceptions from other languages.  
If you're curious, look for `cxa_exceptions.h` and `cxa_exceptions.cpp` in LLVM.

## Issues

* You still need to enable exceptions, so there is binary size overhead.  
* You need to hardcode the size of `__cxa_exception`. This depends on the ABI.

## Demo

I developed this code on Ubuntu using Windows Subsystem for Linux.  
I cross-compiled to aarch64 using clang, and tested using qemu.

```sh
$ clang++ --target=aarch64-linux-gnu -stdlib=libc++ main.cpp -o main
$ qemu-aarch64 ./main
Throwing 0x55000132c0
Success: "You caught me!"
Deleting 0x55000132c0
```
