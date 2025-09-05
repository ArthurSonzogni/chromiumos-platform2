// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/libmon.h"

#include <blazesym.h>
#include <cxxabi.h>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include <filesystem>
#include <string>
#include <vector>

namespace libmon {

static struct blaze_symbolizer* symb;

static volatile bool __mon_target_terminated;
static volatile bool __mon_should_stop;

static void mon_sig_handler(int sig) {
  switch (sig) {
    case SIGINT:
    case SIGTERM:
      __mon_should_stop = true;
      break;
    case SIGCHLD:
      __mon_target_terminated = true;
      break;
    default:
      break;
  }
}

bool should_stop(void) {
  return __mon_should_stop;
}

bool target_terminated(void) {
  return __mon_target_terminated;
}

int setup_sig_handlers(void) {
  struct sigaction sa;

  sa.sa_handler = mon_sig_handler;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    return -EINVAL;
  }

  sa.sa_handler = mon_sig_handler;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGTERM, &sa, NULL) == -1) {
    return -EINVAL;
  }

  sa.sa_handler = mon_sig_handler;
  sa.sa_flags = SA_NOCLDSTOP;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    return -EINVAL;
  }

  return 0;
}

int prepare_target(pid_t& pid, const char* cmd, std::vector<char*>& args) {
  pid_t npid = -1;

  if (pid == -1 && args.empty()) {
    return 0;
  }

  if (pid != -1) {
    kill(pid, SIGSTOP);
    return 0;
  }

  if (cmd == NULL) {
    return -EINVAL;
  }

  npid = fork();
  if (npid == -1) {
    return -EINVAL;
  }

  if (npid == 0) {
    char** argv = new char*[args.size()];
    if (!argv) {
      return -EINVAL;
    }

    for (int i = 0; i < args.size(); i++) {
      argv[i] = args[i];
    }

    /*
     * STOP the process so that we can load the monitor, attach it and setup
     * all the probes.
     */
    raise(SIGTSTP);
    execvp(cmd, argv);
    exit(0);
  }

  pid = npid;
  return 0;
}

int follow_target(pid_t pid) {
  if (pid == -1) {
    return 0;
  }
  /* Setup is done now we can resume target process */
  return kill(pid, SIGCONT);
}

static void show_frame(uintptr_t ip,
                       uintptr_t addr,
                       uint64_t offt,
                       const char* fn,
                       const blaze_symbolize_code_info* ci) {
  printf("    %016lx: %s @ 0x%lx+0x%lx", ip, fn, addr, offt);
  if (ci && ci->file) {
    printf(" %s\n", ci->file);
  }
  printf("\n");
}

static void show_stack_trace(pid_t pid, uintptr_t* ents, uint32_t num_ents) {
  const struct blaze_symbolize_inlined_fn* inlined;
  const struct blaze_syms* res;
  const struct blaze_sym* sym;

  if (pid) {
    struct blaze_symbolize_src_process src = {
        .type_size = sizeof(src),
        .pid = static_cast<uint32_t>(pid),
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

  if (!res) {
    return;
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
  blaze_syms_free(res);
}

void show_ustack(pid_t pid, uintptr_t* ents, uint32_t num_ents) {
  if (pid < 0) {
    return;
  }
  if (!num_ents) {
    return;
  }
  show_stack_trace(pid, ents, num_ents);
}

void show_kstack(uintptr_t* ents, uint32_t num_ents) {
  if (!num_ents) {
    return;
  }
  show_stack_trace(0, ents, num_ents);
}

static void decode_stack_trace(pid_t pid,
                               uintptr_t* ents,
                               uint32_t num_ents,
                               std::vector<std::string>& trace) {
  const struct blaze_symbolize_inlined_fn* inlined;
  const struct blaze_syms* res;
  const struct blaze_sym* sym;

  if (pid) {
    struct blaze_symbolize_src_process src = {
        .type_size = sizeof(src),
        .pid = static_cast<uint32_t>(pid),
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

  if (!res) {
    return;
  }

  for (size_t i = 0; i < num_ents; i++) {
    std::string frame;

    if (!res || res->cnt <= i || !res->syms[i].name) {
      frame = "unknown";
      trace.push_back(frame);
      continue;
    }

    sym = &res->syms[i];
    frame = sym->name;
    trace.push_back(frame);

    for (size_t j = 0; j < sym->inlined_cnt; j++) {
      inlined = &sym->inlined[j];
      frame = inlined->name;
      trace.push_back(frame);
    }
  }
  blaze_syms_free(res);
}

void decode_ustack(pid_t pid,
                   uintptr_t* ents,
                   uint32_t num_ents,
                   std::vector<std::string>& trace) {
  if (pid < 0) {
    return;
  }
  if (!num_ents) {
    return;
  }
  decode_stack_trace(pid, ents, num_ents, trace);
}

void decode_kstack(uintptr_t* ents,
                   uint32_t num_ents,
                   std::vector<std::string>& trace) {
  if (!num_ents) {
    return;
  }
  decode_stack_trace(0, ents, num_ents, trace);
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

static int lookup_map_files(pid_t pid, const char* name, std::string& path) {
  std::filesystem::path dir = "/proc/" + std::to_string(pid) + "/map_files";
  std::string lib_name = name;

  if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
    return -ENOENT;
  }

  for (auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
    if (!entry.is_symlink()) {
      continue;
    }

    std::error_code ec;
    std::filesystem::path link = std::filesystem::read_symlink(entry, ec);
    if (ec) {
      continue;
    }

    std::string ent = link.filename();
    if (ent.find(lib_name) != std::string::npos) {
      path = link.string();
      return 0;
    }
  }
  return -ENOENT;
}

int lookup_lib(pid_t pid, const char* name, std::string& path) {
  std::vector<std::filesystem::path> search = {"/lib64", "/usr/lib64"};
  std::string lib_name = name;

  if (lookup_map_files(pid, name, path) == 0) {
    return 0;
  }

  for (const auto& dir : search) {
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
      continue;
    }

    for (auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
      if (!entry.is_regular_file()) {
        continue;
      }

      std::string ent = entry.path().filename();
      /* Either full match or substring match, e.g. libc.so and libc.so.6 */
      if (ent == lib_name || ent.find(lib_name) != std::string::npos) {
        path = entry.path().string();
        return 0;
      }
    }
  }
  return -ENOENT;
}

static std::string cxx_demangle(const char* sym) {
  int status;
  char* r;

  r = abi::__cxa_demangle(sym, 0, 0, &status);
  if (r) {
    return std::string(r);
  }
  return sym;
}

int lookup_cxx_sym(const std::string& so, const char* fn, std::string& cxx) {
  Elf_Scn* scn = nullptr;
  GElf_Shdr shdr;
  Elf* e;
  size_t shstrndx;
  int fd, ret;

  if (elf_version(EV_CURRENT) == EV_NONE) {
    return -EINVAL;
  }

  fd = open(so.c_str(), O_RDONLY, 0);
  if (fd < 0) {
    return -ENOENT;
  }

  e = elf_begin(fd, ELF_C_READ, nullptr);
  if (!e) {
    ret = -EINVAL;
    goto out;
  }

  if (elf_kind(e) != ELF_K_ELF) {
    ret = -EINVAL;
    goto out;
  }

  if (elf_getshdrstrndx(e, &shstrndx) != 0) {
    ret = -EINVAL;
    goto out;
  }

  while ((scn = elf_nextscn(e, scn)) != nullptr) {
    gelf_getshdr(scn, &shdr);
    if (shdr.sh_type != SHT_SYMTAB && shdr.sh_type != SHT_DYNSYM) {
      continue;
    }

    Elf_Data* data = elf_getdata(scn, nullptr);
    size_t symbol_count = shdr.sh_size / shdr.sh_entsize;
    for (size_t i = 0; i < symbol_count; ++i) {
      const char* name;
      GElf_Sym sym;

      gelf_getsym(data, i, &sym);

      if (GELF_ST_TYPE(sym.st_info) != STT_FUNC) {
        continue;
      }

      if (sym.st_value == 0 || sym.st_shndx == SHN_UNDEF) {
        continue;
      }

      name = elf_strptr(e, shdr.sh_link, sym.st_name);
      if (!name || *name == 0x00) {
        continue;
      }

      std::string d_name = cxx_demangle(name);
      if (d_name.find(fn) != std::string::npos) {
        cxx = name;
        break;
      }
    }
  }

  ret = 0;

out:
  if (e) {
    elf_end(e);
  }
  if (fd > 0) {
    close(fd);
  }
  return ret;
}

}  // namespace libmon
