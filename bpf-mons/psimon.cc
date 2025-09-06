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

#include <iostream>
#include <string>
#include <unordered_map>

// This include must be after stdint.h
#include "include/libmon.h"
#include "include/psimon.h"
#include "mons/bpf_skeletons/skeleton_psimon.bpf.h"

namespace {

static struct option long_options[] = {{"pid", required_argument, 0, 'p'},
                                       {"exec", required_argument, 0, 'e'},
                                       {0, 0, 0, 0}};

struct task {
  std::string comm;
  int32_t pid;
  int32_t tgid;

  std::vector<struct psimon_event> enter_events;
};

struct psi_scope_call {
  uint64_t max_duration;
  uint64_t total_duration;
  uint64_t num_calls;
};

struct psi_scope {
  uint64_t total_duration;
  uint64_t num_calls;

  std::unordered_map<uint64_t, struct psi_scope_call*> callers;

  uintptr_t enter_ents[PSIMON_MAX_KSTACK_ENTS];
  uint16_t num_enter_ents;

  uintptr_t leave_ents[PSIMON_MAX_KSTACK_ENTS];
  uint16_t num_leave_ents;
};

static std::unordered_map<uint64_t, struct psi_scope*> mem_stall_scopes;
static std::unordered_map<uint64_t, struct task*> tasks;

static int attach_probes(struct psimon_bpf* mon, pid_t pid) {
  std::string libc;

  LIBMON_ATTACH_KPROBE(mon, "psi_memstall_enter", call_psi_memstall_enter);
  LIBMON_ATTACH_KPROBE(mon, "psi_memstall_leave", call_psi_memstall_leave);
  return 0;
}

static void lost_event(void* ctx, int cpu, unsigned long long lost_cnt) {
  fprintf(stderr, "Lost %llu events on CPU %d\n", lost_cnt, cpu);
}

static uint64_t generate_ctxid(struct psimon_event* event) {
  uint64_t id;

  id = (uint64_t)event->tgid << 32;
  id |= event->pid;
  return id;
}

static struct psi_scope* init_psi_scope(struct psimon_event* enter,
                                        struct psimon_event* leave) {
  struct psi_scope* scope;

  scope = new struct psi_scope;
  OOM_KILL(scope);

  scope->total_duration = 0;
  scope->num_calls = 0;

  scope->num_enter_ents = enter->num_kstack_ents;
  for (int i = 0; i < enter->num_kstack_ents; i++) {
    scope->enter_ents[i] = enter->kstack_ents[i];
  }

  scope->num_leave_ents = leave->num_kstack_ents;
  for (int i = 0; i < leave->num_kstack_ents; i++) {
    scope->leave_ents[i] = leave->kstack_ents[i];
  }

  return scope;
}

static struct psi_scope* lookup_mem_stall_scope(struct psimon_event* enter,
                                                struct psimon_event* leave) {
  struct psi_scope* scope;
  uint64_t seed;

  seed = enter->kstack_ents[1];
  seed ^= leave->kstack_ents[1] + 0x9e3779b9 + (seed << 6) + (seed >> 2);

  if (mem_stall_scopes.find(seed) == mem_stall_scopes.end()) {
    scope = init_psi_scope(enter, leave);
    mem_stall_scopes[seed] = scope;
    return scope;
  }

  scope = mem_stall_scopes[seed];
  if (scope->enter_ents[1] != enter->kstack_ents[1] ||
      scope->leave_ents[1] != leave->kstack_ents[1]) {
    // Hash collision
    scope = init_psi_scope(enter, leave);
    mem_stall_scopes[seed] = scope;
  }

  return scope;
}

static struct task* lookup_task(struct psimon_event* event, uint64_t id) {
  if (tasks.find(id) == tasks.end()) {
    struct task* t = new struct task;
    OOM_KILL(t);

    t->comm = reinterpret_cast<char*>(event->comm);
    t->pid = event->pid;
    t->tgid = event->tgid;
    tasks[id] = t;
  }

  return tasks[id];
}

static struct psi_scope_call* lookup_caller(struct psi_scope* scope,
                                            uint64_t id) {
  if (scope->callers.find(id) == scope->callers.end()) {
    struct psi_scope_call* call = new struct psi_scope_call;
    OOM_KILL(call);

    call->max_duration = 0;
    call->total_duration = 0;
    call->num_calls = 0;
    scope->callers[id] = call;
  }

  return scope->callers[id];
}

static void psimon_event(void* ctx, int cpu, void* data, unsigned int data_sz) {
  struct psimon_event* event = (struct psimon_event*)data;
  struct psimon_event* last_enter;
  struct psi_scope_call* call;
  struct psi_scope* scope;
  struct task* task;
  uint64_t id = generate_ctxid(event);

  task = lookup_task(event, id);
  if (event->type == PSIMON_EVENT_MEMSTALL_ENTER) {
    task->enter_events.push_back(*event);
    return;
  }

  if (task->enter_events.empty()) {
    // Missed enter event
    return;
  }

  last_enter = &task->enter_events.back();
  task->enter_events.pop_back();

  scope = lookup_mem_stall_scope(last_enter, event);
  scope->num_calls++;
  scope->total_duration += event->ts - last_enter->ts;

  call = lookup_caller(scope, id);
  call->num_calls++;
  call->total_duration += event->ts - last_enter->ts;
  call->max_duration = std::max(call->max_duration, event->ts - last_enter->ts);
}

static void show_psimon_records(void) {
  std::vector<uint64_t> scopes;

  for (auto& s : mem_stall_scopes) {
    scopes.push_back(s.first);
  }

  std::sort(scopes.begin(), scopes.end(), [](uint64_t a, uint64_t b) {
    return mem_stall_scopes[a]->total_duration >
           mem_stall_scopes[b]->total_duration;
  });

  for (auto& s : scopes) {
    auto scope = mem_stall_scopes[s];
    fprintf(stdout, "PSI memstall scope: total_duration=%lu num_calls=%lu\n",
            scope->total_duration, scope->num_calls);

    fprintf(stdout, "    enter:\n");
    libmon::show_kstack(scope->enter_ents, scope->num_enter_ents);
    fprintf(stdout, "    leave:\n");
    libmon::show_kstack(scope->leave_ents, scope->num_leave_ents);

    for (auto& t : scope->callers) {
      auto task = tasks[t.first];
      auto& call = t.second;

      fprintf(stdout, "\tTask %s pid=%d tgid=%d\n", task->comm.c_str(),
              task->pid, task->tgid);

      fprintf(stdout, "\t  PSI memstall: max=%lu avg=%lu samples=%lu\n",
              call->max_duration, call->total_duration / call->num_calls,
              call->num_calls);
    }

    printf("\n");
  }
}

static int psimon(pid_t pid, const char* cmd, std::vector<char*>& args) {
  struct perf_buffer* pb = NULL;
  struct psimon_bpf* mon;
  int err;

  mon = psimon_bpf__open();
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
  err = psimon_bpf__load(mon);
  if (err) {
    fprintf(stderr, "Failed to load BPF mon\n");
    goto cleanup;
  }

  err = attach_probes(mon, pid);
  if (err) {
    goto cleanup;
  }

  pb = perf_buffer__new(bpf_map__fd(mon->maps.pb), 32, psimon_event, lost_event,
                        NULL, NULL);
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
    err = perf_buffer__poll(pb, LIBMON_RB_POLL_TIMEOUT);
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

  show_psimon_records();

cleanup:
  printf("psimon status: %d\n", err);
  perf_buffer__free(pb);
  psimon_bpf__destroy(mon);
  return err;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<char*> exec_args;
  char* exec_cmd = NULL;
  pid_t pid = -1;
  int c, ret;

  while (1) {
    int option_index = 0;

    c = getopt_long(argc, argv, "p:e:", long_options, &option_index);

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
      default:
        abort();
    }
  }

  if (pid != -1 && exec_cmd != NULL) {
    printf("Options -p and -e are mutually exclusive\n");
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

  ret = psimon(pid, exec_cmd, exec_args);

  libmon::release_stack_decoder();
  return ret;
}
