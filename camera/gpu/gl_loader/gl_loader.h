/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_GPU_GL_LOADER_GL_LOADER_H_
#define CAMERA_GPU_GL_LOADER_GL_LOADER_H_

#include <dlfcn.h>
#include <cstdlib>
#include <string>

#include "cros-camera/common.h"

#define EXPAND(x) x
#define CONCATENATE(x, y) x##y
#define FOR_EACH_ARG_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, \
                       _14, _15, _16, _17, _18, _19, _20, _21, _22, N, ...)    \
  N
#define FOR_EACH_NARG_(...) EXPAND(FOR_EACH_ARG_N(__VA_ARGS__))
#define FOR_EACH_RSEQ_N()                                                     \
  22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, \
      1, 0
#define FOR_EACH_NARG(...) FOR_EACH_NARG_(__VA_ARGS__, FOR_EACH_RSEQ_N())

#define FOR_EACH_PAIR_1(type) type
#define FOR_EACH_PAIR_2(type, arg) type arg
#define FOR_EACH_PAIR_4(type, arg, ...) type arg, FOR_EACH_PAIR_2(__VA_ARGS__)
#define FOR_EACH_PAIR_6(type, arg, ...) type arg, FOR_EACH_PAIR_4(__VA_ARGS__)
#define FOR_EACH_PAIR_8(type, arg, ...) type arg, FOR_EACH_PAIR_6(__VA_ARGS__)
#define FOR_EACH_PAIR_10(type, arg, ...) type arg, FOR_EACH_PAIR_8(__VA_ARGS__)
#define FOR_EACH_PAIR_12(type, arg, ...) type arg, FOR_EACH_PAIR_10(__VA_ARGS__)
#define FOR_EACH_PAIR_14(type, arg, ...) type arg, FOR_EACH_PAIR_12(__VA_ARGS__)
#define FOR_EACH_PAIR_16(type, arg, ...) type arg, FOR_EACH_PAIR_14(__VA_ARGS__)
#define FOR_EACH_PAIR_18(type, arg, ...) type arg, FOR_EACH_PAIR_16(__VA_ARGS__)
#define FOR_EACH_PAIR_20(type, arg, ...) type arg, FOR_EACH_PAIR_18(__VA_ARGS__)
#define FOR_EACH_PAIR_22(type, arg, ...) type arg, FOR_EACH_PAIR_20(__VA_ARGS__)
#define FOR_EACH_PAIR_(N, ...) \
  EXPAND(CONCATENATE(FOR_EACH_PAIR_, N)(__VA_ARGS__))
#define FOR_EACH_PAIR(...) \
  FOR_EACH_PAIR_(FOR_EACH_NARG(__VA_ARGS__), __VA_ARGS__)

#define FOR_EACH_ARG_1(type)
#define FOR_EACH_ARG_2(type, arg) arg
#define FOR_EACH_ARG_4(type, arg, ...) arg, FOR_EACH_ARG_2(__VA_ARGS__)
#define FOR_EACH_ARG_6(type, arg, ...) arg, FOR_EACH_ARG_4(__VA_ARGS__)
#define FOR_EACH_ARG_8(type, arg, ...) arg, FOR_EACH_ARG_6(__VA_ARGS__)
#define FOR_EACH_ARG_10(type, arg, ...) arg, FOR_EACH_ARG_8(__VA_ARGS__)
#define FOR_EACH_ARG_12(type, arg, ...) arg, FOR_EACH_ARG_10(__VA_ARGS__)
#define FOR_EACH_ARG_14(type, arg, ...) arg, FOR_EACH_ARG_12(__VA_ARGS__)
#define FOR_EACH_ARG_16(type, arg, ...) arg, FOR_EACH_ARG_14(__VA_ARGS__)
#define FOR_EACH_ARG_18(type, arg, ...) arg, FOR_EACH_ARG_16(__VA_ARGS__)
#define FOR_EACH_ARG_20(type, arg, ...) arg, FOR_EACH_ARG_18(__VA_ARGS__)
#define FOR_EACH_ARG_22(type, arg, ...) arg, FOR_EACH_ARG_20(__VA_ARGS__)
#define FOR_EACH_ARG_(N, ...) EXPAND(CONCATENATE(FOR_EACH_ARG_, N)(__VA_ARGS__))
#define FOR_EACH_ARG(...) FOR_EACH_ARG_(FOR_EACH_NARG(__VA_ARGS__), __VA_ARGS__)

#define DECLARE_FUNCTION(ret_ty, fct, ...)                          \
  __attribute__((__visibility__("default"))) extern "C" ret_ty fct( \
      FOR_EACH_PAIR(__VA_ARGS__)) {                                 \
    return ((ret_ty(*)(FOR_EACH_PAIR(__VA_ARGS__)))_##fct)(         \
        FOR_EACH_ARG(__VA_ARGS__));                                 \
  }
#define DECLARE_HANDLE(fct) void* _##fct = nullptr;
#define LOAD_SYMBOL(lib, fct) _##fct = lib->LoadSymbol(#fct);

class GlLibraryWrapper {
 public:
  explicit GlLibraryWrapper(std::string library_path) {
    lib_handle_ = dlopen(library_path.c_str(), RTLD_LAZY);
    if (lib_handle_ == nullptr) {
      LOGF(ERROR) << "Failed to dlopen library: '" << library_path << "'";
      exit(-1);
    }
  }
  ~GlLibraryWrapper() { dlclose(lib_handle_); }
  void* LoadSymbol(std::string symbol) {
    void* sym = dlsym(lib_handle_, symbol.c_str());
    if (sym == nullptr) {
      LOGF(ERROR) << "Failed to dlsym symbol '" << symbol << "'";
      exit(-1);
    }
    return sym;
  }

 private:
  void* lib_handle_;
};

#endif  // CAMERA_GPU_GL_LOADER_GL_LOADER_H_
