// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BPF_MONS_INCLUDE_LIBMON_H_
#define BPF_MONS_INCLUDE_LIBMON_H_

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <string>
#include <vector>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

namespace libmon {

#define LIBMON_RB_POLL_TIMEOUT 888

#define LIBMON_ATTACH_UPROBE(s, pid, obj, prog, opts)                     \
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

#define LIBMON_ATTACH_KPROBE(s, prog, sym, opts)                       \
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

void decode_ustack(pid_t pid, uintptr_t* ents, uint32_t num_ents);
void decode_kstack(uintptr_t* ents, uint32_t num_ents);

int lookup_lib(pid_t pid, const char* name, std::string& path);

int setup_sig_handlers(void);
bool should_stop(void);

int prepare_target(pid_t& pid, const char* cmd, std::vector<char*>& args);
int follow_target(pid_t pid);
bool target_terminated(void);

}  // namespace libmon

#endif  // BPF_MONS_INCLUDE_LIBMON_H_
