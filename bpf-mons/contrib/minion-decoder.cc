// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minion-decoder.h"

#include <cxxabi.h>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

/*
 * Keeping this code here just in case if blazesym-c doesn't work in
 * some cases (somehow).
 *
 * This is a very basic and trivial /proc/PID/maps based stack-decoder
 * with C++ name demangling.
 *
 * NOTE this is not a complete solution by any means, this is a
 * starting point. It decodes basic cases, but has a number of
 * limitations:
 *
 * 1) known to have some problems with libc symbols
 * 2) kernel decoding has never been tested
 * 3) Java/Android has never been tested
 *
 * Requirements:
 *
 * 1) in BUILD.gn
 * pkg_deps = [ "libelf" ]
 *
 * 2) in bpf-mons ebuild DEPEND/RDEPEND
 * dev-cpp/abseil-cpp
 * dev-libs/elfutils
 */

namespace minion_decoder {

struct addr {
  uintptr_t lo;
  uintptr_t hi;

  std::string sym;
  std::string mod;
};

/* For interval tree lookups */
struct addr_cmp {
  bool operator()(const struct addr* l, const struct addr* r) const {
    return l->hi < r->lo;
  }
};

typedef std::vector<struct addr*> addr_range_t;

static addr_range_t ksyms;
static addr_range_t maps;
static addr_range_t maps_syms;

static std::string cxx_demangle(std::string& sym) {
  int status;
  char* r;

  r = abi::__cxa_demangle(sym.c_str(), 0, 0, &status);
  if (r)
    return std::string(r);
  return sym;
}

static void input_split(std::string& input,
                        const char delim,
                        std::vector<std::string>& ret) {
  size_t s = 0, e;
  std::string tok;

  while ((e = input.find(delim, s)) != std::string::npos) {
    if (e - s < 1) {
      s++;
      continue;
    }

    tok = input.substr(s, e - s);
    s = e + 1;
    ret.push_back(tok);
  }

  ret.push_back(input.substr(s));
}

static struct addr* lookup_addr(addr_range_t& r, uintptr_t ptr) {
  /*
   * THIS IS A THROWAWAY CODE, FOR TESTING ONLY
   *
   * O(n) is okay here.  Had this not been a throwaway code
   * we'd use interval tree of some sort.
   */
  for (auto n : r)
    if (n->lo <= ptr && ptr < n->hi)
      return n;
  return NULL;
}

static __attribute__((unused)) struct addr* lookup_kern_addr(uintptr_t ptr) {
  return lookup_addr(ksyms, ptr);
}

static struct addr* lookup_maps_addr(uintptr_t ptr) {
  return lookup_addr(maps, ptr);
}

static struct addr* lookup_maps_syms_addr(uintptr_t ptr) {
  return lookup_addr(maps_syms, ptr);
}

static __attribute__((unused)) int populate_ksyms(void) {
  std::ifstream in("/proc/kallsyms");
  std::string input;

  if (!in.is_open())
    return -EINVAL;

  while (std::getline(in, input)) {
    if (input.empty())
      break;

    std::vector<std::string> split;

    input_split(input, ' ', split);
    struct addr* r;

    if (split.size() < 3)
      continue;

    r = new struct addr;
    if (!r)
      return -ENOMEM;

    r->lo = std::strtoull(split[0].c_str(), nullptr, 16);
    r->hi = 0;
    r->sym = split[2];
    if (split.size() >= 4)
      r->mod = split[3];

    ksyms.push_back(r);
  }

  return 0;
}

static int populate_maps_syms(std::string& fn, uintptr_t lo) {
  Elf_Scn* scn = nullptr;
  GElf_Shdr shdr;
  Elf* e;
  size_t shstrndx;
  int fd, ret;

  if (elf_version(EV_CURRENT) == EV_NONE)
    return -EINVAL;

  fd = open(fn.c_str(), O_RDONLY, 0);
  if (fd < 0)
    return -ENOENT;

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
    if (shdr.sh_type != SHT_SYMTAB && shdr.sh_type != SHT_DYNSYM)
      continue;

    Elf_Data* data = elf_getdata(scn, nullptr);
    size_t symbol_count = shdr.sh_size / shdr.sh_entsize;
    for (size_t i = 0; i < symbol_count; ++i) {
      const char* name;
      GElf_Sym sym;

      gelf_getsym(data, i, &sym);

      if (GELF_ST_TYPE(sym.st_info) != STT_FUNC)
        continue;

      if (sym.st_value == 0 || sym.st_shndx == SHN_UNDEF)
        continue;

      // maybe sub "shdr.sh_addr + shdr.sh_offset";
      uintptr_t rst_val = sym.st_value;
      struct addr* r = lookup_maps_syms_addr(lo + rst_val);
      if (r)
        continue;

      name = elf_strptr(e, shdr.sh_link, sym.st_name);
      if (!name || *name == 0x00)
        continue;

      r = new struct addr;
      if (!r) {
        ret = -ENOMEM;
        goto out;
      }

      r->lo = lo + rst_val;
      r->hi = r->lo + sym.st_size;
      r->sym = name;

      maps_syms.push_back(r);
    }
  }

  ret = 0;

out:
  if (e)
    elf_end(e);
  if (fd > 0)
    close(fd);
  return ret;
}

static int populate_maps(pid_t pid) {
  std::ifstream in("/proc/" + std::to_string(pid) + "/maps");
  std::string input;

  if (!in.is_open())
    return -EINVAL;

  while (std::getline(in, input)) {
    if (input.empty())
      break;

    std::vector<std::string> split;

    input_split(input, ' ', split);
    struct addr* r;

    if (split.size() < 5)
      continue;

    if (stol(split[4]) == 0)
      continue;

    if (split[1][2] != 'x')
      continue;

    std::vector<std::string> interval;
    input_split(split[0], '-', interval);
    if (interval.size() != 2)
      return -ENOMEM;

    uintptr_t lo = std::strtoul(interval[0].c_str(), nullptr, 16);
    uintptr_t hi = std::strtoul(interval[1].c_str(), nullptr, 16);

    if (lookup_maps_addr(lo))
      continue;

    r = new struct addr;
    if (!r)
      return -ENOMEM;

    r->lo = lo;
    r->hi = hi;
    r->mod = split[5];

    populate_maps_syms(split[5], lo);
    maps.push_back(r);
  }

  return 0;
}

void decode_ustack(pid_t pid, uintptr_t* ents, uint32_t num_ents) {
  for (uint32_t fn = 1; fn < num_ents; fn++) {
    struct addr *mod, *sym;
    uintptr_t ptr;

    ptr = ents[fn];
    mod = lookup_maps_addr(ptr);
    /* Probably a new .so, popoulate the mappings */
    if (!mod) {
      populate_maps(pid);
      mod = lookup_maps_addr(ptr);
    }

    sym = lookup_maps_syms_addr(ptr);

    std::string dsym;
    if (sym)
      dsym = cxx_demangle(sym->sym);
    else
      dsym = "unknown";

    printf("<%lx> %s [%s]\n", ptr, dsym.c_str(),
           mod ? mod->mod.c_str() : "unknown");
  }
}

__attribute__((unused)) void decode_kstack(uintptr_t* ents, uint32_t num_ents) {
  printf("-ENOSYS\n");
}

}  // namespace minion_decoder
