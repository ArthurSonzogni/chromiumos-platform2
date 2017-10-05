// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "run_oci/container_config_parser.h"

#include <sys/capability.h>
#include <sys/resource.h>
#include <unistd.h>

#include <map>
#include <regex>  // NOLINT(build/c++11)
#include <string>
#include <utility>
#include <vector>

#include <base/json/json_reader.h>
#include <base/strings/string_split.h>
#include <base/values.h>

namespace run_oci {

namespace {

// Gets a uint32 from the given dictionary.
bool ParseUint32FromDict(const base::DictionaryValue& dict, const char *name,
                         uint32_t* val_out) {
  double double_val;
  if (!dict.GetDouble(name, &double_val)) {
    return false;
  }
  *val_out = double_val;
  return true;
}

// Gets a uint64 from the given dictionary.
bool ParseUint64FromDict(const base::DictionaryValue& dict, const char *name,
                         uint64_t* val_out) {
  double double_val;
  if (!dict.GetDouble(name, &double_val)) {
    return false;
  }
  *val_out = double_val;
  return true;
}

// Parses basic platform configuration.
bool ParsePlatformConfig(const base::DictionaryValue& config_root_dict,
                         OciConfigPtr const& config_out) {
  // |platform_dict| stays owned by |config_root_dict|
  const base::DictionaryValue* platform_dict = nullptr;
  if (!config_root_dict.GetDictionary("platform", &platform_dict)) {
    LOG(ERROR) << "Fail to parse platform dictionary from config";
    return false;
  }

  if (!platform_dict->GetString("os", &config_out->platform.os)) {
    return false;
  }

  if (!platform_dict->GetString("arch", &config_out->platform.arch)) {
    return false;
  }

  return true;
}

// Parses root fs info.
bool ParseRootFileSystemConfig(const base::DictionaryValue& config_root_dict,
                               OciConfigPtr const& config_out) {
  // |rootfs_dict| stays owned by |config_root_dict|
  const base::DictionaryValue* rootfs_dict = nullptr;
  if (!config_root_dict.GetDictionary("root", &rootfs_dict)) {
    LOG(ERROR) << "Fail to parse rootfs dictionary from config";
    return false;
  }
  std::string path;
  if (!rootfs_dict->GetString("path", &path)) {
    LOG(ERROR) << "Fail to get rootfs path from config";
    return false;
  }
  config_out->root.path = base::FilePath(path);
  rootfs_dict->GetBoolean("readonly", &config_out->root.readonly);
  return true;
}

// Fills |config_out| with information about the capability sets in the
// container.
bool ParseCapabilitiesConfig(const base::DictionaryValue& capabilities_dict,
                             std::map<std::string, CapSet>* config_out) {
  constexpr const char* kCapabilitySetNames[] = {
      "effective", "bounding", "inheritable", "permitted", "ambient"};
  const std::string kAmbientCapabilitySetName = "ambient";

  CapSet caps_superset;
  for (const char* set_name : kCapabilitySetNames) {
    // |capset_list| stays owned by |capabilities_dict|.
    const base::ListValue* capset_list = nullptr;
    if (!capabilities_dict.GetList(set_name, &capset_list))
      continue;
    CapSet caps;
    cap_value_t cap_value;
    for (const auto* cap_name_value : *capset_list) {
      std::string cap_name;
      if (!cap_name_value->GetAsString(&cap_name)) {
        LOG(ERROR) << "Capability list " << set_name
                   << " contains a non-string";
        return false;
      }
      if (cap_from_name(cap_name.c_str(), &cap_value) == -1) {
        LOG(ERROR) << "Unrecognized capability name: " << cap_name;
        return false;
      }
      caps[cap_value] = true;
    }
    (*config_out)[set_name] = caps;
    caps_superset = caps;
  }

  // We currently only support sets that are identical, except that ambient is
  // optional.
  for (const char* set_name : kCapabilitySetNames) {
    auto it = config_out->find(set_name);
    if (it == config_out->end() && set_name == kAmbientCapabilitySetName) {
      // Ambient capabilities are optional.
      continue;
    }
    if (it == config_out->end()) {
      LOG(ERROR)
          << "If capabilities are set, all capability sets should be present";
      return false;
    }
    if (it->second != caps_superset) {
      LOG(ERROR)
          << "If capabilities are set, all capability sets should be identical";
      return false;
    }
  }

  return true;
}

const std::map<std::string, int> kRlimitMap = {
#define RLIMIT_MAP_ENTRY(limit) \
  { "RLIMIT_" #limit, RLIMIT_##limit }
    RLIMIT_MAP_ENTRY(CPU),
    RLIMIT_MAP_ENTRY(FSIZE),
    RLIMIT_MAP_ENTRY(DATA),
    RLIMIT_MAP_ENTRY(STACK),
    RLIMIT_MAP_ENTRY(CORE),
    RLIMIT_MAP_ENTRY(RSS),
    RLIMIT_MAP_ENTRY(NPROC),
    RLIMIT_MAP_ENTRY(NOFILE),
    RLIMIT_MAP_ENTRY(MEMLOCK),
    RLIMIT_MAP_ENTRY(AS),
    RLIMIT_MAP_ENTRY(LOCKS),
    RLIMIT_MAP_ENTRY(SIGPENDING),
    RLIMIT_MAP_ENTRY(MSGQUEUE),
    RLIMIT_MAP_ENTRY(NICE),
    RLIMIT_MAP_ENTRY(RTPRIO),
    RLIMIT_MAP_ENTRY(RTTIME),
#undef RLIMIT_MAP_ENTRY
};

// Fills |config_out| with information about the capability sets in the
// container.
bool ParseRlimitsConfig(const base::ListValue& rlimits_list,
                        std::vector<OciProcessRlimit>* rlimits_out) {
  size_t num_limits = rlimits_list.GetSize();
  for (size_t i = 0; i < num_limits; ++i) {
    const base::DictionaryValue* rlimits_dict;
    if (!rlimits_list.GetDictionary(i, &rlimits_dict)) {
      LOG(ERROR) << "Fail to get rlimit item " << i;
      return false;
    }

    std::string rlimit_name;
    if (!rlimits_dict->GetString("type", &rlimit_name)) {
      LOG(ERROR) << "Fail to get type of rlimit " << i;
      return false;
    }
    const auto it = kRlimitMap.find(rlimit_name);
    if (it == kRlimitMap.end()) {
      LOG(ERROR) << "Unrecognized rlimit name: " << rlimit_name;
      return false;
    }

    OciProcessRlimit limit;
    limit.type = it->second;
    if (!ParseUint32FromDict(*rlimits_dict, "hard", &limit.hard)) {
      LOG(ERROR) << "Fail to get hard limit of rlimit " << i;
      return false;
    }
    if (!ParseUint32FromDict(*rlimits_dict, "soft", &limit.soft)) {
      LOG(ERROR) << "Fail to get soft limit of rlimit " << i;
      return false;
    }
    rlimits_out->push_back(limit);
  }

  return true;
}

// Fills |config_out| with information about the main process to run in the
// container and the user it should be run as.
bool ParseProcessConfig(const base::DictionaryValue& config_root_dict,
                        OciConfigPtr const& config_out) {
  // |process_dict| stays owned by |config_root_dict|
  const base::DictionaryValue* process_dict = nullptr;
  if (!config_root_dict.GetDictionary("process", &process_dict)) {
    LOG(ERROR) << "Fail to get main process from config";
    return false;
  }
  process_dict->GetBoolean("terminal", &config_out->process.terminal);
  // |user_dict| stays owned by |process_dict|
  const base::DictionaryValue* user_dict = nullptr;
  if (!process_dict->GetDictionary("user", &user_dict)) {
    LOG(ERROR) << "Failed to get user info from config";
    return false;
  }
  if (!ParseUint32FromDict(*user_dict, "uid", &config_out->process.user.uid))
    return false;
  if (!ParseUint32FromDict(*user_dict, "gid", &config_out->process.user.gid))
    return false;
  // |args_list| stays owned by |process_dict|
  const base::ListValue* args_list = nullptr;
  if (!process_dict->GetList("args", &args_list)) {
    LOG(ERROR) << "Fail to get main process args from config";
    return false;
  }
  size_t num_args = args_list->GetSize();
  for (size_t i = 0; i < num_args; ++i) {
    std::string arg;
    if (!args_list->GetString(i, &arg)) {
      LOG(ERROR) << "Fail to get process args from config";
      return false;
    }
    config_out->process.args.push_back(arg);
  }
  // |env_list| stays owned by |process_dict|
  const base::ListValue* env_list = nullptr;
  if (process_dict->GetList("env", &env_list)) {
    size_t num_env = env_list->GetSize();
    for (size_t i = 0; i < num_env; ++i) {
      std::string env;
      if (!env_list->GetString(i, &env)) {
        LOG(ERROR) << "Fail to get process env from config";
        return false;
      }
      config_out->process.env.push_back(env);
    }
  }
  if (!process_dict->GetString("cwd", &config_out->process.cwd)) {
    LOG(ERROR) << "failed to get cwd of process";
    return false;
  }
  // selinuxLabel is optional.
  process_dict->GetString("selinuxLabel", &config_out->process.selinuxLabel);
  // |capabilities_dict| stays owned by |process_dict|
  const base::DictionaryValue* capabilities_dict = nullptr;
  if (process_dict->GetDictionary("capabilities", &capabilities_dict)) {
    if (!ParseCapabilitiesConfig(*capabilities_dict,
                                 &config_out->process.capabilities)) {
      return false;
    }
  }

  // |rlimit_list| stays owned by |process_dict|
  const base::ListValue* rlimits_list = nullptr;
  if (process_dict->GetList("rlimits", &rlimits_list)) {
    if (!ParseRlimitsConfig(*rlimits_list, &config_out->process.rlimits)) {
      return false;
    }
  }

  return true;
}

// Parses the 'mounts' field.  The necessary mounts for running the container
// are specified here.
bool ParseMounts(const base::DictionaryValue& config_root_dict,
                 OciConfigPtr const& config_out) {
  // |config_mounts_list| stays owned by |config_root_dict|
  const base::ListValue* config_mounts_list = nullptr;
  if (!config_root_dict.GetList("mounts", &config_mounts_list)) {
    LOG(ERROR) << "Fail to get mounts from config dictionary";
    return false;
  }

  for (size_t i = 0; i < config_mounts_list->GetSize(); ++i) {
    const base::DictionaryValue* mount_dict;
    if (!config_mounts_list->GetDictionary(i, &mount_dict)) {
      LOG(ERROR) << "Fail to get mount item " << i;
      return false;
    }
    OciMount mount;
    std::string value;
    if (!mount_dict->GetString("destination", &value)) {
      LOG(ERROR) << "Fail to get mount path for mount " << i;
      return false;
    }
    mount.destination = base::FilePath(value);
    if (!mount_dict->GetString("type", &mount.type)) {
      LOG(ERROR) << "Fail to get mount type for mount " << i;
      return false;
    }
    if (!mount_dict->GetString("source", &value)) {
      LOG(ERROR) << "Fail to get mount source for mount " << i;
      return false;
    }
    mount.source = base::FilePath(value);

    // |options| are owned by |mount_dict|
    const base::ListValue* options = nullptr;
    if (mount_dict->GetList("options", &options)) {
      for (size_t j = 0; j < options->GetSize(); ++j) {
        std::string this_opt;
        if (!options->GetString(j, &this_opt)) {
          LOG(ERROR) << "Fail to get option " << j << " from mount options";
          return false;
        }
        mount.options.push_back(this_opt);
      }
    }

    config_out->mounts.push_back(mount);
  }
  return true;
}

// Parses the linux resource list
bool ParseResources(const base::DictionaryValue& resources_dict,
                    OciLinuxResources* resources_out) {
  // |device_list| is owned by |resources_dict|
  const base::ListValue* device_list = nullptr;
  if (!resources_dict.GetList("devices", &device_list)) {
    // The device list is optional.
    return true;
  }
  size_t num_devices = device_list->GetSize();
  for (size_t i = 0; i < num_devices; ++i) {
    OciLinuxCgroupDevice device;

    const base::DictionaryValue* dev;
    if (!device_list->GetDictionary(i, &dev)) {
      LOG(ERROR) << "Fail to get device " << i;
      return false;
    }

    if (!dev->GetBoolean("allow", &device.allow)) {
      LOG(ERROR) << "Fail to get allow value for device " << i;
      return false;
    }
    if (!dev->GetString("access", &device.access))
      device.access = "rwm";  // Optional, default to all perms.
    if (!dev->GetString("type", &device.type))
      device.type = "a";  // Optional, default to both a means all.
    if (!ParseUint32FromDict(*dev, "major", &device.major))
      device.major = -1;  // Optional, -1 will map to all devices.
    if (!ParseUint32FromDict(*dev, "minor", &device.minor))
      device.minor = -1;  // Optional, -1 will map to all devices.

    resources_out->devices.push_back(device);
  }

  return true;
}

// Parses the list of namespaces and fills |namespaces_out| with them.
bool ParseNamespaces(const base::ListValue* namespaces_list,
                     std::vector<OciNamespace>* namespaces_out) {
  for (size_t i = 0; i < namespaces_list->GetSize(); ++i) {
    OciNamespace new_namespace;
    const base::DictionaryValue* ns;
    if (!namespaces_list->GetDictionary(i, &ns)) {
      LOG(ERROR) << "Failed to get namespace " << i;
      return false;
    }
    if (!ns->GetString("type", &new_namespace.type)) {
      LOG(ERROR) << "Namespace " << i << " missing type";
      return false;
    }
    ns->GetString("path", &new_namespace.path);
    namespaces_out->push_back(new_namespace);
  }
  return true;
}

// Parse the list of device nodes that the container needs to run.
bool ParseDeviceList(const base::DictionaryValue& linux_dict,
                     OciConfigPtr const& config_out) {
  // |device_list| is owned by |linux_dict|
  const base::ListValue* device_list = nullptr;
  if (!linux_dict.GetList("devices", &device_list)) {
    // The device list is optional.
    return true;
  }
  size_t num_devices = device_list->GetSize();
  for (size_t i = 0; i < num_devices; ++i) {
    OciLinuxDevice device;

    const base::DictionaryValue* dev;
    if (!device_list->GetDictionary(i, &dev)) {
      LOG(ERROR) << "Fail to get device " << i;
      return false;
    }
    std::string path;
    if (!dev->GetString("path", &device.path)) {
      LOG(ERROR) << "Fail to get path for dev";
      return false;
    }
    if (!dev->GetString("type", &device.type)) {
      LOG(ERROR) << "Fail to get type for " << device.path;
      return false;
    }
    if (!ParseUint32FromDict(*dev, "major", &device.major))
      return false;
    if (!ParseUint32FromDict(*dev, "minor", &device.minor))
      return false;
    if (!ParseUint32FromDict(*dev, "fileMode", &device.fileMode))
      return false;
    if (!ParseUint32FromDict(*dev, "uid", &device.uid))
      return false;
    if (!ParseUint32FromDict(*dev, "gid", &device.gid))
      return false;

    config_out->linux_config.devices.push_back(device);
  }

  return true;
}

// Parses the list of ID mappings and fills |mappings_out| with them.
bool ParseLinuxIdMappings(const base::ListValue* id_map_list,
                          std::vector<OciLinuxNamespaceMapping>* mappings_out) {
  for (size_t i = 0; i < id_map_list->GetSize(); ++i) {
    OciLinuxNamespaceMapping new_map;
    const base::DictionaryValue* map;
    if (!id_map_list->GetDictionary(i, &map)) {
      LOG(ERROR) << "Fail to get id map " << i;
      return false;
    }
    if (!ParseUint32FromDict(*map, "hostID", &new_map.hostID))
      return false;
    if (!ParseUint32FromDict(*map, "containerID", &new_map.containerID))
      return false;
    if (!ParseUint32FromDict(*map, "size", &new_map.size))
      return false;
    mappings_out->push_back(new_map);
  }
  return true;
}

// Parses seccomp syscall args.
bool ParseSeccompArgs(const base::DictionaryValue& syscall_dict,
                      OciSeccompSyscall* syscall_out) {
  const base::ListValue* args = nullptr;
  if (syscall_dict.GetList("args", &args)) {
    for (size_t i = 0; i < args->GetSize(); ++i) {
      const base::DictionaryValue* args_dict = nullptr;
      if (!args->GetDictionary(i, &args_dict)) {
        LOG(ERROR) << "Failed to pars args dict for " << syscall_out->name;
        return false;
      }
      OciSeccompArg this_arg;
      if (!ParseUint32FromDict(*args_dict, "index", &this_arg.index))
        return false;
      if (!ParseUint64FromDict(*args_dict, "value", &this_arg.value))
        return false;
      if (!ParseUint64FromDict(*args_dict, "value2", &this_arg.value2))
        return false;
      if (!args_dict->GetString("op", &this_arg.op)) {
        LOG(ERROR) << "Failed to parse op for arg " << this_arg.index
                   << " of " << syscall_out->name;
        return false;
      }
      syscall_out->args.push_back(this_arg);
    }
  }
  return true;
}

// Parses the seccomp node if it is present.
bool ParseSeccompInfo(const base::DictionaryValue& seccomp_dict,
                      OciSeccomp* seccomp_out) {
  if (!seccomp_dict.GetString("defaultAction",
                              &seccomp_out->defaultAction))
    return false;

  // Gets the list of architectures.
  const base::ListValue* architectures = nullptr;
  if (!seccomp_dict.GetList("architectures", &architectures)) {
    LOG(ERROR) << "Fail to read seccomp architectures";
    return false;
  }
  for (size_t i = 0; i < architectures->GetSize(); ++i) {
    std::string this_arch;
    if (!architectures->GetString(i, &this_arch)) {
      LOG(ERROR) << "Fail to parse seccomp architecture list";
      return false;
    }
    seccomp_out->architectures.push_back(this_arch);
  }

  // Gets the list of syscalls.
  const base::ListValue* syscalls = nullptr;
  if (!seccomp_dict.GetList("syscalls", &syscalls)) {
    LOG(ERROR) << "Fail to read seccomp syscalls";
    return false;
  }
  for (size_t i = 0; i < syscalls->GetSize(); ++i) {
    const base::DictionaryValue* syscall_dict = nullptr;
    if (!syscalls->GetDictionary(i, &syscall_dict)) {
      LOG(ERROR) << "Fail to parse seccomp syscalls list";
      return false;
    }
    OciSeccompSyscall this_syscall;
    if (!syscall_dict->GetString("name", &this_syscall.name)) {
      LOG(ERROR) << "Fail to parse syscall name " << i;
      return false;
    }
    if (!syscall_dict->GetString("action", &this_syscall.action)) {
      LOG(ERROR) << "Fail to parse syscall action for " << this_syscall.name;
      return false;
    }
    if (!ParseSeccompArgs(*syscall_dict, &this_syscall))
      return false;
    seccomp_out->syscalls.push_back(this_syscall);
  }

  return true;
}

// Parses the linux node which has information about setting up a user
// namespace, and the list of devices for the container.
bool ParseLinuxConfigDict(const base::DictionaryValue& runtime_root_dict,
                          OciConfigPtr const& config_out) {
  // |linux_dict| is owned by |runtime_root_dict|
  const base::DictionaryValue* linux_dict = nullptr;
  if (!runtime_root_dict.GetDictionary("linux", &linux_dict)) {
    LOG(ERROR) << "Fail to get linux dictionary from the runtime dictionary";
    return false;
  }

  // |uid_map_list| is owned by |linux_dict|
  const base::ListValue* uid_map_list = nullptr;
  if (!linux_dict->GetList("uidMappings", &uid_map_list)) {
    LOG(ERROR) << "Fail to get uid mappings list";
    return false;
  }
  ParseLinuxIdMappings(uid_map_list, &config_out->linux_config.uidMappings);

  // |gid_map_list| is owned by |linux_dict|
  const base::ListValue* gid_map_list = nullptr;
  if (!linux_dict->GetList("gidMappings", &gid_map_list)) {
    LOG(ERROR) << "Fail to get gid mappings list";
    return false;
  }
  ParseLinuxIdMappings(gid_map_list, &config_out->linux_config.gidMappings);

  if (!ParseDeviceList(*linux_dict, config_out))
    return false;

  const base::DictionaryValue* resources_dict = nullptr;
  if (linux_dict->GetDictionary("resources", &resources_dict)) {
    if (!ParseResources(*resources_dict, &config_out->linux_config.resources))
      return false;
  }

  const base::ListValue* namespaces_list = nullptr;
  if (linux_dict->GetList("namespaces", &namespaces_list)) {
    if (!ParseNamespaces(namespaces_list, &config_out->linux_config.namespaces))
      return false;
  }

  const base::DictionaryValue* seccomp_dict = nullptr;
  if (linux_dict->GetDictionary("seccomp", &seccomp_dict)) {
    if (!ParseSeccompInfo(*seccomp_dict, &config_out->linux_config.seccomp))
      return false;
  }

  return true;
}

bool HostnameValid(const std::string& hostname) {
  if (hostname.length() > 255)
    return false;

  const std::regex name("^[0-9a-zA-Z]([0-9a-zA-Z-]*[0-9a-zA-Z])?$");
  if (!std::regex_match(hostname, name))
    return false;

  const std::regex double_dash("--");
  if (std::regex_match(hostname, double_dash))
    return false;

  return true;
}

bool ParseHooksList(const base::ListValue& hooks_list,
                    std::vector<OciHook>* hooks_out,
                    const std::string& hook_type) {
  size_t num_hooks = hooks_list.GetSize();
  for (size_t i = 0; i < num_hooks; ++i) {
    OciHook hook;
    const base::DictionaryValue* hook_dict;
    if (!hooks_list.GetDictionary(i, &hook_dict)) {
      LOG(ERROR) << "Fail to get " << hook_type << " hook item " << i;
      return false;
    }

    if (!hook_dict->GetString("path", &hook.path)) {
      LOG(ERROR) << "Fail to get path of " << hook_type << " hook " << i;
      return false;
    }

    const base::ListValue* hook_args;
    // args are optional.
    if (hook_dict->GetList("args", &hook_args)) {
      size_t num_args = hook_args->GetSize();
      for (size_t j = 0; j < num_args; ++j) {
        std::string arg;
        if (!hook_args->GetString(j, &arg)) {
          LOG(ERROR) << "Fail to get arg " << j << " of " << hook_type
                     << " hook " << i;
          return false;
        }
        hook.args.push_back(arg);
      }
    }

    const base::ListValue* hook_envs;
    // envs are optional.
    if (hook_dict->GetList("env", &hook_envs)) {
      size_t num_env = hook_envs->GetSize();
      for (size_t j = 0; j < num_env; ++j) {
        std::string env;
        if (!hook_envs->GetString(j, &env)) {
          LOG(ERROR) << "Fail to get env " << j << " of " << hook_type
                     << " hook " << i;
          return false;
        }
        std::vector<std::string> kvp = base::SplitString(
            env, "=", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
        if (kvp.size() != 2) {
          LOG(ERROR) << "Fail to parse env \"" << env
                     << "\". Must be in name=value format.";
          return false;
        }
        hook.env.insert(std::make_pair(kvp[0], kvp[1]));
      }
    }

    int timeout_seconds;
    // timeout is optional.
    if (hook_dict->GetInteger("timeout", &timeout_seconds)) {
      hook.timeout = base::TimeDelta::FromSeconds(timeout_seconds);
    } else {
      hook.timeout = base::TimeDelta::Max();
    }

    hooks_out->emplace_back(std::move(hook));
  }
  return true;
}

bool ParseHooks(const base::DictionaryValue& config_root_dict,
                OciConfigPtr const& config_out) {
  const base::DictionaryValue* hooks_config_dict;
  if (!config_root_dict.GetDictionary("hooks", &hooks_config_dict)) {
    // Hooks are optional.
    return true;
  }

  const base::ListValue* hooks_list;
  if (hooks_config_dict->GetList("prestart", &hooks_list)) {
    if (!ParseHooksList(*hooks_list, &config_out->pre_start_hooks, "prestart"))
      return false;
  }
  if (hooks_config_dict->GetList("poststart", &hooks_list)) {
    if (!ParseHooksList(
            *hooks_list, &config_out->post_start_hooks, "poststart"))
      return false;
  }
  if (hooks_config_dict->GetList("poststop", &hooks_list)) {
    if (!ParseHooksList(*hooks_list, &config_out->post_stop_hooks, "poststop"))
      return false;
  }
  return true;
}

// Parses the configuration file for the container.  The config file specifies
// basic filesystem info and details about the process to be run.  namespace,
// cgroup, and syscall configurations are also specified
bool ParseConfigDict(const base::DictionaryValue& config_root_dict,
                     OciConfigPtr const& config_out) {
  if (!config_root_dict.GetString("ociVersion", &config_out->ociVersion)) {
    LOG(ERROR) << "Failed to parse ociVersion";
    return false;
  }
  if (!config_root_dict.GetString("hostname", &config_out->hostname)) {
    LOG(ERROR) << "Failed to parse hostname";
    return false;
  }
  if (!HostnameValid(config_out->hostname)) {
    LOG(ERROR) << "Invalid hostname " << config_out->hostname;
    return false;
  }

  // Platform info
  if (!ParsePlatformConfig(config_root_dict, config_out)) {
    return false;
  }

  // Root fs info
  if (!ParseRootFileSystemConfig(config_root_dict, config_out)) {
    return false;
  }

  // Process info
  if (!ParseProcessConfig(config_root_dict, config_out)) {
    return false;
  }

  // Get a list of mount points and mounts.
  if (!ParseMounts(config_root_dict, config_out)) {
    LOG(ERROR) << "Failed to parse mounts";
    return false;
  }

  // Hooks info
  if (!ParseHooks(config_root_dict, config_out)) {
    return false;
  }

  // Parse linux node.
  if (!ParseLinuxConfigDict(config_root_dict, config_out)) {
    LOG(ERROR) << "Failed to parse the linux node";
    return false;
  }

  return true;
}

}  // anonymous namespace

bool ParseContainerConfig(const std::string& config_json_data,
                          OciConfigPtr const& config_out) {
  std::string error_msg;
  std::unique_ptr<const base::Value> config_root_val =
      base::JSONReader::ReadAndReturnError(config_json_data,
                                           base::JSON_PARSE_RFC,
                                           nullptr /* error_code_out */,
                                           &error_msg,
                                           nullptr /* error_line_out */,
                                           nullptr /* error_column_out */);
  if (!config_root_val) {
    LOG(ERROR) << "Fail to parse config.json: " << error_msg;
    return false;
  }
  const base::DictionaryValue* config_dict = nullptr;
  if (!config_root_val->GetAsDictionary(&config_dict)) {
    LOG(ERROR) << "Fail to parse root dictionary from config.json";
    return false;
  }
  if (!ParseConfigDict(*config_dict, config_out)) {
    return false;
  }

  return true;
}

}  // namespace run_oci
