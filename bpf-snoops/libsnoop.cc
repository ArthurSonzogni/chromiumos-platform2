// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/libsnoop.h"

#include <blazesym.h>
#include <unistd.h>

#include <filesystem>
#include <string>

namespace libsnoop {

static struct blaze_symbolizer* symb;

static void show_frame(uintptr_t ip,
                       uintptr_t addr,
                       uint64_t offt,
                       const char* fn,
                       const blaze_symbolize_code_info* ci) {
  printf("    %016lx: %s @ 0x%lx+0x%lx", ip, fn, addr, offt);
  if (ci && ci->file)
    printf(" %s\n", ci->file);
  printf("\n");
}

static void show_stack_trace(uint32_t pid, uintptr_t* ents, uint32_t num_ents) {
  const struct blaze_symbolize_inlined_fn* inlined;
  const struct blaze_result* res;
  const struct blaze_sym* sym;

  if (pid) {
    struct blaze_symbolize_src_process src = {
        .type_size = sizeof(src),
        .pid = pid,
        .map_files = true,
    };

    res = blaze_symbolize_process_abs_addrs(symb, &src, (const uintptr_t*)ents,
                                            num_ents);
  } else {
    struct blaze_symbolize_src_kernel src = {
        .type_size = sizeof(src),
    };

    res = blaze_symbolize_kernel_abs_addrs(symb, &src, (const uintptr_t*)ents,
                                           num_ents);
  }

  for (size_t i = 0; i < num_ents; i++) {
    if (!res || res->cnt <= i || !res->syms[i].name) {
      printf("    %016lx: <no-symbol>\n", ents[i]);
      continue;
    }

    sym = &res->syms[i];
    show_frame(ents[i], sym->addr, sym->offset, sym->name, &sym->code_info);

    for (size_t j = 0; j < sym->inlined_cnt; j++) {
      inlined = &sym->inlined[j];
      show_frame(0, 0, 0, sym->name, &inlined->code_info);
    }
  }
  printf("\n");
  blaze_result_free(res);
}

void decode_ustack(uint32_t pid, uintptr_t* ents, uint32_t num_ents) {
  if (!pid)
    return;
  if (!num_ents)
    return;
  show_stack_trace(pid, ents, num_ents);
}

void decode_kstack(uintptr_t* ents, uint32_t num_ents) {
  if (!num_ents)
    return;
  show_stack_trace(0, ents, num_ents);
}

int init_stack_decoder(void) {
  symb = blaze_symbolizer_new();
  if (!symb) {
    fprintf(stderr, "Unable to init stack decoder\n");
    return -EINVAL;
  }
  return 0;
}

void release_stack_decoder(void) {
  blaze_symbolizer_free(symb);
}

int lookup_lib(const char* name, std::string& path) {
  char p[4096];

  snprintf(p, sizeof(p), "/lib64/%s", name);
  if (std::filesystem::exists(p))
    goto ok;

  snprintf(p, sizeof(p), "/lib/%s", name);
  if (std::filesystem::exists(p))
    goto ok;

  return -ENOENT;

ok:
  path = p;
  return 0;
}

}  // namespace libsnoop
