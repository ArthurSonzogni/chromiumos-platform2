// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <format>
#include <iostream>
#include <list>
#include <stack>
#include <string>
#include <unordered_map>

#include "include/libmon.h"
// This include must be after stdint.h
#include "include/lockmon.h"
#include "mons/bpf_skeletons/skeleton_lockmon.bpf.h"

namespace {

static struct option long_options[] = {{"pid", required_argument, 0, 'p'},
                                       {"exec", required_argument, 0, 'e'},
                                       {0, 0, 0, 0}};
/*
 * Once we detect the locking was compromised we don't want to report any
 * further problems.
 */
static bool tainted = false;

/* lock dependency is unidirectional, from s to t */
struct dep {
  struct lockmon_event s;
  struct lockmon_event t;
};

struct lock {
  std::unordered_map<uintptr_t, struct dep*> deps;
};

/* All known locks and their dependency chains */
static std::unordered_map<uintptr_t, struct lock*> locks;
/*
 * Currently held locks (a list, because unlock() can come in any order).
 * Per execution context
 */
static std::unordered_map<uint64_t, std::list<struct lockmon_event*>> ctx;

static uint64_t generate_ctxid(struct lockmon_event* event) {
  uint64_t id;

  id = (uint64_t)event->pid << 32;
  id |= event->tid;
  return id;
}

/* Possible that we may never see init() for a lock (e.g. attached too late) */
static struct lock* lookup_lock(struct lockmon_event* event) {
  if (locks.find(event->lock) == locks.end()) {
    struct lock* l = new struct lock;
    locks[event->lock] = l;
  }

  return locks[event->lock];
}

static void register_lock(struct lockmon_event* event) {
  lookup_lock(event);
}

/* Checks currently held locks against the newly acquired one */
static void recursive_locking(uint64_t id, struct lockmon_event* event) {
  for (auto& st : ctx[id]) {
    if (st->lock != event->lock)
      continue;

    printf("comm: %s pid: %d attempts to acquire lock %p:\n", event->comm,
           event->pid, reinterpret_cast<void*>(event->lock));
    libmon::show_ustack(event->pid, event->ustack_ents, event->num_ustack_ents);

    printf("which it already holds:\n");
    libmon::show_ustack(st->pid, st->ustack_ents, st->num_ustack_ents);

    tainted = true;
    return;
  }
}

static bool is_reachable(uintptr_t from, struct lockmon_event* event) {
  std::stack<uintptr_t> stack;

  stack.push(from);

  while (!stack.empty()) {
    uintptr_t cur = stack.top();

    stack.pop();

    struct lock* l = locks[cur];
    /* what, what, what do you mean? */
    if (!l)
      continue;

    for (auto& dep : l->deps) {
      struct dep* d = dep.second;

      if (d->t.lock == event->lock) {
        printf("comm: %s pid %d violates existing dependency chain\n",
               event->comm, event->pid);

        printf("#0 lock %p acquired at:\n", reinterpret_cast<void*>(from));
        libmon::show_ustack(d->s.pid, d->s.ustack_ents, d->s.num_ustack_ents);

        printf("#1 lock %p acquired at:\n",
               reinterpret_cast<void*>(event->lock));
        libmon::show_ustack(d->t.pid, d->t.ustack_ents, d->t.num_ustack_ents);

        tainted = true;
        return true;
      }
      stack.push(d->t.lock);
    }
  }
  return false;
}

/* Check for violations of known locking ordering */
static void locking_chains(uint64_t id, struct lockmon_event* event) {
  if (tainted)
    return;

  if (ctx[id].empty())
    return;

  for (auto& cur : ctx[id]) {
    if (!is_reachable(event->lock, cur))
      continue;

    printf("reverse dependency chain\n");

    printf("#0 lock %p acquired at:\n", reinterpret_cast<void*>(cur->lock));
    libmon::show_ustack(cur->pid, cur->ustack_ents, cur->num_ustack_ents);

    printf("#1 lock %p acquired at:\n", reinterpret_cast<void*>(event->lock));
    libmon::show_ustack(event->pid, event->ustack_ents, event->num_ustack_ents);
    break;
  }
}

/* Establishes dependencies between locks */
static void lock_dependency(struct lockmon_event* event) {
  uint64_t id = generate_ctxid(event);

  if (tainted)
    return;

  if (ctx[id].empty())
    return;

  /* 'top' lock is the most recently acquired one */
  struct lockmon_event* top = ctx[id].back();
  struct lock* tl = lookup_lock(top);

  /* Do we already know that new lock depends on the top one */
  if (tl->deps.find(event->lock) != tl->deps.end())
    return;

  struct dep* d = new struct dep;
  memcpy(&d->s, top, sizeof(*top));
  memcpy(&d->t, event, sizeof(*event));
  tl->deps[event->lock] = d;
}

static void __lock(struct lockmon_event* event) {
  uint64_t id = generate_ctxid(event);

  /* keep track of all locks we attempt to lock */
  register_lock(event);

  if (ctx.find(id) == ctx.end())
    return;

  recursive_locking(id, event);
  locking_chains(id, event);
}

static void ctx_add_top_lock(struct lockmon_event* event) {
  uint64_t id = generate_ctxid(event);

  /* new "most recent" lock */
  struct lockmon_event* cur = new struct lockmon_event;
  memcpy(cur, event, sizeof(*cur));
  ctx[id].push_back(cur);
}

static void lock(struct lockmon_event* event) {
  __lock(event);
  lock_dependency(event);

  if (tainted)
    return;

  ctx_add_top_lock(event);
}

/*
 * An attempt to try_lock(), we don't modify ctx stack, but trylock is
 * enough to run dependency checks.
 */
static void trylock_call(struct lockmon_event* event) {
  __lock(event);
}

/* A successful try_lock(), need to add lock to ctx stack */
static void trylock_ret(struct lockmon_event* event) {
  lock_dependency(event);
  ctx_add_top_lock(event);
}

static void unlock(struct lockmon_event* event) {
  uint64_t id = generate_ctxid(event);

  /* Somehow unlock() is the first event we see for this ctx */
  if (ctx.find(id) == ctx.end())
    return;

  for (auto it = ctx[id].begin(); it != ctx[id].end(); it++) {
    struct lockmon_event* cur = *it;

    if (cur->lock == event->lock) {
      ctx[id].erase(it);
      delete cur;
      break;
    }
  }
}

static void init(struct lockmon_event* event) {
  register_lock(event);
}

static void destroy(struct lockmon_event* event) {
  struct lock* l;

  if (locks.find(event->lock) == locks.end())
    return;

  l = lookup_lock(event);
  for (auto& it : l->deps) {
    delete it.second;
  }

  l->deps.clear();
}

static int attach_probes(struct lockmon_bpf* mon, pid_t pid) {
  std::string libc;

  if (libmon::lookup_lib(pid, "libc.so", libc))
    return -ENOENT;

  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), "pthread_mutex_init",
                       call_mutex_init);
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), "pthread_mutex_lock",
                       call_mutex_lock);
  /*
   * WORKAROUND b/356967465
   *
   * BPF does not handle dyn symbols properly, use a "special" name.
   *
   * See for details:
   * lore.kernel.org/bpf/20230904022444.1695820-2-hengqi.chen@gmail.com/T/
   *
   * Need to investigate uprev. For the time being - do a barrel roll.
   */
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), "__pthread_mutex_trylock",
                       call_mutex_trylock);
  LIBMON_ATTACH_URETPROBE(mon, pid, libc.c_str(), "__pthread_mutex_trylock",
                          ret_mutex_trylock);
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), "pthread_mutex_unlock",
                       call_mutex_unlock);
  LIBMON_ATTACH_UPROBE(mon, pid, libc.c_str(), "pthread_mutex_destroy",
                       call_mutex_destroy);
  return 0;
}

static int lockmon_event(void* ctx, void* data, size_t data_sz) {
  struct lockmon_event* event = (struct lockmon_event*)data;

  if (tainted)
    return 0;

  switch (event->type) {
    case LOCKMON_EVENT_MUTEX_INIT:
      init(event);
      break;
    case LOCKMON_EVENT_MUTEX_LOCK:
      lock(event);
      break;
    case LOCKMON_EVENT_MUTEX_UNLOCK:
      unlock(event);
      break;
    case LOCKMON_EVENT_MUTEX_TRYLOCK_CALL:
      trylock_call(event);
      break;
    case LOCKMON_EVENT_MUTEX_TRYLOCK_RET:
      trylock_ret(event);
      break;
    case LOCKMON_EVENT_MUTEX_DESTROY:
      destroy(event);
      break;
    case LOCKMON_EVENT_INVALID:
      printf("INVALID\n");
      return -EINVAL;
  }

  return 0;
}

static int lockmon(pid_t pid, const char* cmd, std::vector<char*>& cmd_args) {
  struct ring_buffer* rb = NULL;
  struct lockmon_bpf* mon;
  int err;

  mon = lockmon_bpf__open();
  if (!mon) {
    fprintf(stderr, "Failed to open BPF mon\n");
    return -EINVAL;
  }

  err = libmon::prepare_target(pid, cmd, cmd_args);
  if (err)
    goto cleanup;

  err = lockmon_bpf__load(mon);
  if (err) {
    fprintf(stderr, "Failed to load BPF mon\n");
    goto cleanup;
  }

  err = attach_probes(mon, pid);
  if (err)
    goto cleanup;

  rb = ring_buffer__new(bpf_map__fd(mon->maps.rb), lockmon_event, NULL, NULL);
  if (!rb) {
    fprintf(stderr, "Failed to open ring buffer\n");
    err = -EINVAL;
    goto cleanup;
  }

  err = libmon::setup_sig_handlers();
  if (err)
    goto cleanup;

  err = libmon::follow_target(pid);
  if (err)
    goto cleanup;

  do {
    err = ring_buffer__poll(rb, LIBMON_RB_POLL_TIMEOUT);
    /* We should stop, no matter how many events are left in the rb */
    if (libmon::should_stop()) {
      err = 0;
      break;
    }
    if (err == -EINTR)
      continue;
    if (err < 0) {
      printf("rb polling error: %d\n", err);
      break;
    }
    /* Even if the target has terminated we still need to handle all events */
    if (err > 0)
      continue;
    if (libmon::target_terminated())
      break;
  } while (1);

cleanup:
  printf("lockmon status: %d\n", err);
  ring_buffer__free(rb);
  lockmon_bpf__destroy(mon);
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
    if (c == -1)
      break;

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

  if (pid == -1 && exec_cmd == NULL) {
    printf("Must specify either -p or -e\n");
    return -EINVAL;
  }

  if (exec_cmd) {
    exec_args.push_back(basename(exec_cmd));
    while (optind < argc) {
      exec_args.push_back(argv[optind]);
      optind++;
    }
    exec_args.push_back(NULL);
  }

  ret = libmon::init_stack_decoder();
  if (ret)
    return ret;

  ret = lockmon(pid, exec_cmd, exec_args);

  libmon::release_stack_decoder();
  return ret;
}
