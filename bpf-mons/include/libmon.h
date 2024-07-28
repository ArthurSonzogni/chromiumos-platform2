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

#define LIBMON_ATTACH_UPROBE(mon, pid, obj, sym, prog)                        \
  do {                                                                        \
    LIBBPF_OPTS(bpf_uprobe_opts, uopts);                                      \
    printf("Attaching uprobe: " #prog "\n");                                  \
    uopts.func_name = sym;                                                    \
    uopts.retprobe = false;                                                   \
    if (mon->links.prog) {                                                    \
      fprintf(stderr, "Already attached: " #prog "\n");                       \
      return -EINVAL;                                                         \
    }                                                                         \
    mon->links.prog = bpf_program__attach_uprobe_opts(mon->progs.prog, (pid), \
                                                      (obj), 0, &uopts);      \
    if (!mon->links.prog) {                                                   \
      perror("Failed to attach: " #prog);                                     \
      return -EINVAL;                                                         \
    }                                                                         \
  } while (false)

#define LIBMON_ATTACH_URETPROBE(mon, pid, obj, sym, prog)                     \
  do {                                                                        \
    LIBBPF_OPTS(bpf_uprobe_opts, uopts);                                      \
    printf("Attaching uretprobe: " #prog "\n");                               \
    uopts.func_name = sym;                                                    \
    uopts.retprobe = true;                                                    \
    if (mon->links.prog) {                                                    \
      fprintf(stderr, "Already attached: " #prog "\n");                       \
      return -EINVAL;                                                         \
    }                                                                         \
    mon->links.prog = bpf_program__attach_uprobe_opts(mon->progs.prog, (pid), \
                                                      (obj), 0, &uopts);      \
    if (!mon->links.prog) {                                                   \
      perror("Failed to attach: " #prog);                                     \
      return -EINVAL;                                                         \
    }                                                                         \
  } while (false)

#define LIBMON_ATTACH_KPROBE(mon, sym, prog)                             \
  do {                                                                   \
    LIBBPF_OPTS(bpf_kprobe_opts, kopts);                                 \
    printf("Attaching kprobe: " #prog "\n");                             \
    kopts.retprobe = false;                                              \
    if (mon->links.prog) {                                               \
      fprintf(stderr, "Already attached: " #prog "\n");                  \
      return -EINVAL;                                                    \
    }                                                                    \
    mon->links.prog =                                                    \
        bpf_program__attach_kprobe_opts(mon->progs.prog, (sym), &kopts); \
    if (!mon->links.prog) {                                              \
      perror("Failed to attach: " #prog);                                \
      return -EINVAL;                                                    \
    }                                                                    \
  } while (false)

#define LIBMON_ATTACH_KRETPROBE(mon, sym, prog)                          \
  do {                                                                   \
    LIBBPF_OPTS(bpf_kprobe_opts, kopts);                                 \
    printf("Attaching kretprobe: " #prog "\n");                          \
    kopts.retprobe = true;                                               \
    if (mon->links.prog) {                                               \
      fprintf(stderr, "Already attached: " #prog "\n");                  \
      return -EINVAL;                                                    \
    }                                                                    \
    mon->links.prog =                                                    \
        bpf_program__attach_kprobe_opts(mon->progs.prog, (sym), &kopts); \
    if (!mon->links.prog) {                                              \
      perror("Failed to attach: " #prog);                                \
      return -EINVAL;                                                    \
    }                                                                    \
  } while (false)

int init_stack_decoder(void);
void release_stack_decoder(void);

void show_ustack(pid_t pid, uintptr_t* ents, uint32_t num_ents);
void show_kstack(uintptr_t* ents, uint32_t num_ents);

int lookup_lib(pid_t pid, const char* name, std::string& path);

int setup_sig_handlers(void);
bool should_stop(void);

int prepare_target(pid_t& pid, const char* cmd, std::vector<char*>& args);
int follow_target(pid_t pid);
bool target_terminated(void);

}  // namespace libmon

#endif  // BPF_MONS_INCLUDE_LIBMON_H_
