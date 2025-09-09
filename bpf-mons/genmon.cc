// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

// This include must be after stdint.h
#include "include/genmon.h"
#include "include/libmon.h"
#include "mons/bpf_skeletons/skeleton_genmon.bpf.h"

namespace {

static struct option long_options[] = {{"pid", required_argument, 0, 'p'},
                                       {"exec", required_argument, 0, 'e'},
                                       {"syms", required_argument, 0, 's'},
                                       {0, 0, 0, 0}};

struct task {
  std::string comm;
  int32_t pid;
  int32_t tgid;
};

struct gen_event_call {
  uint64_t num_calls;
};

struct gen_event {
  uint64_t num_calls;

  std::unordered_map<uint64_t, struct gen_event_call*> callers;

  uintptr_t ents[GENMON_MAX_KSTACK_ENTS];
  uint16_t num_ents;
};

static std::unordered_map<uint64_t, struct gen_event*> gen_events;
static std::unordered_map<uint64_t, struct task*> tasks;

static int attach_probes(struct genmon_bpf* mon,
                         std::vector<std::string>& syms) {
  switch (syms.size()) {
    case 5:
      LIBMON_ATTACH_KPROBE(mon, syms[4].c_str(), call_genmon_event4);
      [[fallthrough]];
    case 4:
      LIBMON_ATTACH_KPROBE(mon, syms[3].c_str(), call_genmon_event3);
      [[fallthrough]];
    case 3:
      LIBMON_ATTACH_KPROBE(mon, syms[2].c_str(), call_genmon_event2);
      [[fallthrough]];
    case 2:
      LIBMON_ATTACH_KPROBE(mon, syms[1].c_str(), call_genmon_event1);
      [[fallthrough]];
    case 1:
      LIBMON_ATTACH_KPROBE(mon, syms[0].c_str(), call_genmon_event0);
      break;
  }

  return 0;
}

static void lost_event(void* ctx, int cpu, unsigned long long lost_cnt) {
  fprintf(stderr, "Lost %llu events on CPU %d\n", lost_cnt, cpu);
}

static uint64_t generate_ctxid(struct genmon_event* event) {
  uint64_t id;

  id = (uint64_t)event->tgid << 32;
  id |= event->pid;
  return id;
}

static struct gen_event* init_gen_event(struct genmon_event* event) {
  struct gen_event* ev;

  ev = new struct gen_event;
  OOM_KILL(ev);

  ev->num_calls = 0;

  ev->num_ents = event->num_kstack_ents;
  memcpy(ev->ents, event->kstack_ents,
         sizeof(event->kstack_ents[0]) * event->num_kstack_ents);

  return ev;
}

static struct gen_event* lookup_gen_event(struct genmon_event* event) {
  struct gen_event* ev;
  uint64_t seed;

  seed = event->kstack_ents[1];
  seed ^= event->kstack_ents[event->num_kstack_ents - 1] + 0x9e3779b9 +
          (seed << 6) + (seed >> 2);

  if (gen_events.find(seed) == gen_events.end()) {
    ev = init_gen_event(event);
    gen_events[seed] = ev;
    return ev;
  }

  ev = gen_events[seed];
  if (ev->num_ents != event->num_kstack_ents ||
      memcmp(ev->ents, event->kstack_ents,
             sizeof(event->kstack_ents[0]) * event->num_kstack_ents)) {
    // Hash collision
    ev = init_gen_event(event);
    gen_events[seed] = ev;
    return ev;
  }

  return ev;
}

static void genmon_event_task(struct genmon_event* event, uint64_t id) {
  if (tasks.find(id) == tasks.end()) {
    struct task* t = new struct task;
    OOM_KILL(t);

    t->comm = reinterpret_cast<char*>(event->comm);
    t->pid = event->pid;
    t->tgid = event->tgid;
    tasks[id] = t;
  }
}

static struct gen_event_call* lookup_caller(struct gen_event* ev, uint64_t id) {
  if (ev->callers.find(id) == ev->callers.end()) {
    struct gen_event_call* call = new struct gen_event_call;
    OOM_KILL(call);

    call->num_calls = 0;
    ev->callers[id] = call;
  }

  return ev->callers[id];
}

static void genmon_event(void* ctx, int cpu, void* data, unsigned int data_sz) {
  struct genmon_event* event = (struct genmon_event*)data;
  struct gen_event_call* call;
  struct gen_event* ev;
  uint64_t id = generate_ctxid(event);

  genmon_event_task(event, id);
  ev = lookup_gen_event(event);
  ev->num_calls++;

  call = lookup_caller(ev, id);
  call->num_calls++;
}

static void show_genmon_records(void) {
  std::vector<uint64_t> evs;

  if (gen_events.empty()) {
    return;
  }

  for (auto& s : gen_events) {
    evs.push_back(s.first);
  }

  std::sort(evs.begin(), evs.end(), [](uint64_t a, uint64_t b) {
    return gen_events[a]->num_calls > gen_events[b]->num_calls;
  });

  fprintf(stdout, "\nnum_events=%zu\n\n", evs.size());

  for (auto& s : evs) {
    auto ev = gen_events[s];
    fprintf(stdout, "genevent num_calls=%lu\n", ev->num_calls);

    fprintf(stdout, "    event:\n");
    libmon::show_kstack(ev->ents, ev->num_ents);

    for (auto& t : ev->callers) {
      auto task = tasks[t.first];
      auto& call = t.second;

      fprintf(stdout, "\tTask %s pid=%d tgid=%d num_calls=%lu\n",
              task->comm.c_str(), task->pid, task->tgid, call->num_calls);
    }

    printf("\n");
  }
}

static int genmon(pid_t pid,
                  const char* cmd,
                  std::vector<char*>& args,
                  std::vector<std::string>& syms) {
  struct perf_buffer* pb = NULL;
  struct genmon_bpf* mon;
  int err;

  mon = genmon_bpf__open();
  if (!mon) {
    fprintf(stderr, "Failed to open BPF mon\n");
    return -EINVAL;
  }

  err = libmon::prepare_target(pid, cmd, args);
  if (err) {
    fprintf(stderr, "Failed to prepare target\n");
    goto cleanup;
  }

  mon->rodata->kprobe_mon_pid = pid;
  err = genmon_bpf__load(mon);
  if (err) {
    fprintf(stderr, "Failed to load BPF mon\n");
    goto cleanup;
  }

  err = attach_probes(mon, syms);
  if (err) {
    goto cleanup;
  }

  pb = perf_buffer__new(bpf_map__fd(mon->maps.pb), 128, genmon_event,
                        lost_event, NULL, NULL);
  if (!pb) {
    fprintf(stderr, "Failed to open ring buffer\n");
    err = -EINVAL;
    goto cleanup;
  }

  err = libmon::setup_sig_handlers();
  if (err) {
    fprintf(stderr, "Failed to setup signal handlers\n");
    goto cleanup;
  }

  err = libmon::follow_target(pid);
  if (err) {
    fprintf(stderr, "Failed to follow target\n");
    goto cleanup;
  }

  do {
    err = perf_buffer__poll(pb, LIBMON_RB_POLL_TIMEOUT / 8);
    /* We should stop, no matter how many events are left in the pb */
    if (libmon::should_stop()) {
      err = 0;
      break;
    }
    if (err == -EINTR) {
      continue;
    }
    if (err < 0) {
      printf("pb polling error: %d\n", err);
      break;
    }
    /* Even if the target has terminated we still need to handle all events */
    if (err > 0) {
      continue;
    }
    if (libmon::target_terminated()) {
      break;
    }
  } while (1);

  show_genmon_records();

cleanup:
  printf("genmon status: %d\n", err);
  perf_buffer__free(pb);
  genmon_bpf__destroy(mon);
  return err;
}

static void split_syms(const char* symstr, std::vector<std::string>& syms) {
  std::stringstream ss(symstr);
  std::string sym;

  while (std::getline(ss, sym, ',')) {
    syms.push_back(sym);
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> syms;
  std::vector<char*> exec_args;
  char* exec_cmd = NULL;
  pid_t pid = -1;
  int c, ret;

  while (1) {
    int option_index = 0;

    c = getopt_long(argc, argv, "p:e:s:", long_options, &option_index);

    /* Detect the end of the options. */
    if (c == -1) {
      break;
    }

    switch (c) {
      case 'p':
        pid = std::stol(optarg);
        break;
      case 'e':
        exec_cmd = optarg;
        break;
      case 's':
        split_syms(optarg, syms);
        break;
      default:
        abort();
    }
  }

  if (pid != -1 && exec_cmd != NULL) {
    printf("Options -p and -e are mutually exclusive\n");
    return -EINVAL;
  }

  if (syms.empty()) {
    printf("Symbol (-s) must be specified\n");
    return -EINVAL;
  }

  if (syms.size() > 5) {
    printf("Maximum 5 symbols are allowed\n");
    return -EINVAL;
  }

  ret = libmon::init_stack_decoder();
  if (ret) {
    return ret;
  }

  if (exec_cmd) {
    exec_args.push_back(basename(exec_cmd));
    while (optind < argc) {
      exec_args.push_back(argv[optind]);
      optind++;
    }
    exec_args.push_back(NULL);
  }

  ret = genmon(pid, exec_cmd, exec_args, syms);

  libmon::release_stack_decoder();
  return ret;
}
