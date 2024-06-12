// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BPF_SNOOPS_INCLUDE_LIBSNOOP_H_
#define BPF_SNOOPS_INCLUDE_LIBSNOOP_H_

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include <errno.h>
#include <stdio.h>
#include <string>

namespace libsnoop {

#define ATTR_UNUSED __attribute__((unused))

#define LIBSNOOP_ATTACH_UPROBE(s, pid, obj, prog, opts)                   \
  do {                                                                    \
    printf("Attaching uprobe: " #prog "\n");                              \
    if (s->links.prog) {                                                  \
      fprintf(stderr, "Already attached: " #prog "\n");                   \
      return -EINVAL;                                                     \
    }                                                                     \
    s->links.prog = bpf_program__attach_uprobe_opts(s->progs.prog, (pid), \
                                                    (obj), 0, (opts));    \
    if (!s->links.prog) {                                                 \
      perror("Failed to attach: " #prog);                                 \
      return -EINVAL;                                                     \
    }                                                                     \
  } while (false)

#define LIBSNOOP_ATTACH_KPROBE(s, prog, sym, opts)                     \
  do {                                                                 \
    printf("Attaching kprobe: " #prog "\n");                           \
    if (s->links.prog) {                                               \
      fprintf(stderr, "Already attached: " #prog "\n");                \
      return -EINVAL;                                                  \
    }                                                                  \
    s->links.prog =                                                    \
        bpf_program__attach_kprobe_opts(s->progs.prog, (sym), (opts)); \
    if (!s->links.prog) {                                              \
      perror("Failed to attach: " #prog);                              \
      return -EINVAL;                                                  \
    }                                                                  \
  } while (false)

int init_stack_decoder(void);
void release_stack_decoder(void);

void decode_ustack(__u32 pid, __u64* ents, __s32 num_ents);
void decode_kstack(__u64* ents, __s32 num_ents);

int lookup_lib(const char* name, std::string& path);

}  // namespace libsnoop

#endif  // BPF_SNOOPS_INCLUDE_LIBSNOOP_H_
