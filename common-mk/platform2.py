#!/usr/bin/env python3
# Copyright 2013 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Wrapper for building the Chromium OS platform.

Takes care of running GN/ninja/etc... with all the right values.
"""

import collections
import glob
import itertools
import json
import os
import shlex
import sys

import common_utils
import ebuild_function


_HACK_VAR_TO_DISABLE_ISORT = "hack"

# pylint: disable=wrong-import-position
import chromite_init  # pylint: disable=unused-import

from chromite.lib import commandline
from chromite.lib import cros_build_lib
from chromite.lib import osutils
from chromite.lib import portage_util


# USE flags used in BUILD.gn should be listed in _IUSE or _IUSE_TRUE.

# USE flags whose default values are false.
_IUSE = [
    "amd_oemcrypto",
    "amd64",
    "android_container_pi",
    "android_container_rvc",
    "android_vm_rvc",
    "android_vm_tm",
    "android_vm_master",
    "apply_landlock_policy",
    "arc_adb_sideloading",
    "arc_erofs",
    "arc_hw_oemcrypto",
    "arcpp",
    "arcvm",
    "arcvm_gki",
    "arm",
    "asan",
    "attestation",
    "bluetooth_suspend_management",
    "borealis_host",
    "camera_angle_backend",
    "camera_diagnostics",
    "camera_feature_auto_framing",
    "camera_feature_effects",
    "camera_feature_face_detection",
    "camera_feature_frame_annotator",
    "camera_feature_hdrnet",
    "camera_feature_portrait_mode",
    "camera_feature_super_res",
    "cdm_factory_daemon",
    "cellular",
    "cert_provision",
    "cfm",
    "cfm_enabled_device",
    "cheets",
    "chromeless_tty",
    "compdb_only",
    "compilation_database",
    "containers",
    "coverage",
    "cpp20",
    "cr50_onboard",
    "cros_arm64",
    "cros_debug",
    "cros_host",
    "cros_i686",
    "cross_domain_context",
    "cross_domain_context_borealis",
    "crosvm_fixed_blob_mapping",
    "crosvm_virtio_video",
    "crosvm_wl_dmabuf",
    "crypto",
    "dbus",
    "debug",
    "device_mapper",
    "dhcpv6",
    "direncryption",
    "dlc",
    "enable_slow_boot_notify",
    "encrypted_reboot_vault",
    "encrypted_stateful",
    # TODO(b:262689487): Remove when all limitations of enterprise rollback in
    # reven have been addressed.
    "enterprise_rollback_reven",
    "factory_runtime_probe",
    "fake_drivefs_launcher",
    "fbpreprocessord",
    "feature_management",
    "feedback",
    "flex_internal",
    "floss",
    "floss_rootcanal",
    "fp_on_power_button",
    "fsverity",
    "ftdi_tpm",
    "function_elimination_experiment",
    "fuzzer",
    "generic_tpm2",
    "hammerd_api",
    "hevc_codec",
    "hibernate",
    "houdini",
    "houdini64",
    "hw_details",
    "hwid_override",
    "iioservice",
    "iioservice_proximity",
    "inference_accuracy_eval",
    "input_devices_elan_i2chid",
    "input_devices_emright",
    "input_devices_eps2pstiap",
    "input_devices_g2touch",
    "input_devices_goodix",
    "input_devices_himax",
    "input_devices_ilitek_its",
    "input_devices_ilitek_tddi",
    "input_devices_melfas",
    "input_devices_nvt_ts",
    "input_devices_pixart",
    "input_devices_sis",
    "input_devices_spi_heatmap",
    "input_devices_synaptics",
    "input_devices_wacom",
    "input_devices_weida",
    "input_devices_zinitix",
    "intel_oemcrypto",
    "internal",
    "ipu6",
    "ipu6ep",
    "ipu6epmtl",
    "ipu6epadln",
    "ipu6se",
    "iwlwifi_dump",
    "jetstream_routing",
    "keymint",
    "key_eviction",
    "kvm_guest",
    "kvm_host",
    "legacy_usb_passthrough",
    "libcamera",
    "libglvnd",
    "local_ml_core_internal",
    "login_apply_no_new_privs",
    "login_enable_crosh_sudo",
    "lto_experiment",
    "lvm_migration",
    "lvm_stateful_partition",
    "manage_efi_boot_entries",
    "mesa_reven",
    "metrics_uploader",
    "ml_benchmark_drivers",
    "mojo",
    "mount_oop",
    "msan",
    "mtd",
    "ndk_translation",
    "oemcrypto_v16",
    "oemcrypto_v17",
    "ondevice_document_scanner",
    "ondevice_document_scanner_dlc",
    "ondevice_grammar",
    "ondevice_handwriting",
    "ondevice_handwriting_dlc",
    "ondevice_image_content_annotation",
    "ondevice_speech",
    "ondevice_text_suggestions",
    "opengles",
    "optee_oemcrypto",
    "os_install_service",
    "passive_metrics",
    "pavp_4_3",
    "pigweed",
    "pinweaver",
    "postinst_metrics",
    "postinstall_cgpt_repair",
    "postinstall_config_efi_and_legacy",
    "power_management",
    "prjquota",
    "profiling",
    "protected_av1",
    "proto_force_optimize_speed",
    "qualcomm_camx",
    "qrtr",
    "reven_oobe_config",
    "reven_partition_migration",
    "secagentd_min_core_btf",
    "selinux",
    "slow_mount",
    "systemd",
    "system_wide_scudo",
    "tcmalloc",
    "test",
    "ti50_onboard",
    "timers",
    "tpm",
    "tpm_dynamic",
    "tpm_insecure_fallback",
    "tpm2",
    "tpm2_simulator",
    "ubsan",
    "udev",
    "usb",
    "user_session_isolation",
    "v4l2_codec",
    "vaapi",
    "venus_gwp_asan",
    "video_cards_msm",
    "virtgpu_native_context",
    "vm_borealis",
    # TODO(b/296341333): This flag is temporary to avoid a compilation error
    # under tests. When that issue is fixed, remove this use flag.
    "vmt_tracing",
    "vpn",
    "vtpm_proxy",
    "vulkan",
    "wake_on_wifi",
    "wilco",
    "wpa3_sae",
    "x86",
]

# USE flags whose default values are true.
_IUSE_TRUE = [
    "chrome_kiosk_app",
    "chrome_network_proxy",
]


class Platform2:
    """Main builder logic for platform2"""

    def __init__(
        self,
        use_flags=None,
        board=None,
        host=False,
        libdir=None,
        incremental=True,
        target_os=None,
        verbose=False,
        enable_tests=False,
        cache_dir=None,
        strategy=None,
        user=None,
        bind_mount_dev=False,
        jobs=None,
        platform_subdir=None,
    ):
        self.board = board
        self.host = host
        self.incremental = incremental
        self.jobs = jobs
        self.target_os = target_os
        self.verbose = verbose
        self.platform_subdir = platform_subdir
        self.strategy = strategy
        self.user = user
        self.bind_mount_dev = bind_mount_dev

        if use_flags is not None:
            self.use_flags = use_flags
        elif self.board is None:
            self.use_flags = []
        else:
            self.use_flags = portage_util.GetBoardUseFlags(self.board)

        if enable_tests:
            self.use_flags.add("test")

        if self.host:
            self.sysroot = "/"
        else:
            board_vars = self.get_portageq_envvars(["SYSROOT"], board=board)
            self.sysroot = board_vars["SYSROOT"]

        if libdir:
            self.libdir = libdir
        else:
            self.libdir = "/usr/lib"

        if cache_dir:
            self.cache_dir = cache_dir
        else:
            self.cache_dir = os.path.join(
                self.sysroot, "var/cache/portage/chromeos-base/platform2"
            )

        self.libbase_ver = os.environ.get("BASE_VER", "")
        if not self.libbase_ver:
            # If BASE_VER variable not set, read the content of
            # $SYSROOT/usr/share/libchrome/BASE_VER
            # file which contains the default libchrome revision number.
            base_ver_file = os.path.join(
                self.sysroot, "usr/share/libchrome/BASE_VER"
            )
            try:
                self.libbase_ver = osutils.ReadFile(base_ver_file).strip()
            except FileNotFoundError:
                # Software not depending on libchrome still uses platform2.py,
                # Instead of asserting here. Provide a human readable bad value
                # that is not supposed to be used.
                self.libbase_ver = "NOT-INSTALLED"

    def get_src_dir(self):
        """Return the path to build tools and common GN files"""
        return os.path.realpath(os.path.dirname(__file__))

    def get_platform2_root(self):
        """Return the path to src/platform2"""
        return os.path.dirname(self.get_src_dir())

    def get_buildroot(self):
        """Return the path to the folder where build artifacts are located."""
        if not self.incremental:
            workdir = os.environ.get("WORKDIR")
            if workdir:
                # Matches $(cros-workon_get_build_dir) behavior.
                return os.path.join(workdir, "build")
            else:
                return os.getcwd()
        else:
            return self.cache_dir

    def get_products_path(self):
        """Return the path to the folder where build product are located."""
        return os.path.join(self.get_buildroot(), "out/Default")

    def get_portageq_envvars(self, varnames, board=None):
        """Returns the values of a given set of variables using portageq."""

        # See if the env already has these settings.  If so, grab them directly.
        # This avoids the need to specify --board at all most of the time.
        try:
            board_vars = {}
            for varname in varnames:
                board_vars[varname] = os.environ[varname]
            return board_vars
        except KeyError:
            pass

        if board is None and not self.host:
            board = self.board

        # Portage will set this to an incomplete list which breaks portageq
        # walking all of the repos.  Clear it and let the value be repopulated.
        os.environ.pop("PORTDIR_OVERLAY", None)

        return portage_util.PortageqEnvvars(
            varnames, board=board, allow_undefined=True
        )

    def get_build_environment(self):
        """Returns a dict of environment variables we will use to run GN.

        We do this to set the various toolchain names for the target board.
        """
        varnames = ["ARCH", "CHOST", "AR", "CC", "CXX", "PKG_CONFIG"]
        board_env = self.get_portageq_envvars(varnames)

        tool_names = {
            "AR": "ar",
            "CC": "gcc",
            "CXX": "g++",
            "PKG_CONFIG": "pkg-config",
        }

        env = {
            "ARCH": board_env.get("ARCH"),
        }
        for var, tool in tool_names.items():
            env["%s_target" % var] = (
                board_env[var]
                if board_env[var]
                else "%s-%s" % (board_env["CHOST"], tool)
            )

        return env

    def get_components_glob(self):
        """Return a glob of marker files for built components/projects.

        Each project spits out a file whilst building: we return a glob of them
        so we can install/test those projects or reset between compiles to
        ensure components that are no longer part of the build don't get
        installed.
        """
        return glob.glob(
            os.path.join(self.get_products_path(), "gen/components_*")
        )

    def can_use_gn(self):
        """Returns true if GN can be used on configure.

        All packages in platform2/ should be configured by GN.
        """
        build_gn = os.path.join(
            self.get_platform2_root(), self.platform_subdir, "BUILD.gn"
        )
        return os.path.isfile(build_gn)

    def configure(self, _args):
        """Runs the configure step of the Platform2 build.

        Creates the build root if it doesn't already exists.  Then runs the
        appropriate configure tool. Currently only GN is supported.
        """
        assert self.can_use_gn()

        if not os.path.isdir(self.get_buildroot()):
            os.makedirs(self.get_buildroot())

        if not self.incremental:
            osutils.RmDir(self.get_products_path(), ignore_missing=True)

        self.configure_gn()

    def gen_common_args(self, should_parse_shell_string):
        """Generates common arguments for the tools to configure as a dict.

        Returned value types are str, bool or list of strs.
        Lists are returned only when should_parse_shell_string is set to True.
        """

        def flags(s):
            if should_parse_shell_string:
                return common_utils.parse_shell_args(s)
            return s

        args = {
            "OS": "linux",
            "sysroot": self.sysroot,
            "libdir": self.libdir,
            "build_root": self.get_buildroot(),
            "platform2_root": self.get_platform2_root(),
            "libbase_ver": self.libbase_ver,
            "enable_exceptions": os.environ.get("CXXEXCEPTIONS", 0) == "1",
            "external_cflags": flags(os.environ.get("CFLAGS", "")),
            "external_cxxflags": flags(os.environ.get("CXXFLAGS", "")),
            "external_cppflags": flags(os.environ.get("CPPFLAGS", "")),
            "external_ldflags": flags(os.environ.get("LDFLAGS", "")),
        }
        return args

    def configure_gn_args(self):
        """Configure with GN.

        Generates flags to run GN with, and then runs GN.
        """

        def to_gn_string(s):
            return '"%s"' % s.replace('"', '\\"')

        def to_gn_list(strs):
            return "[%s]" % ",".join([to_gn_string(s) for s in strs])

        def to_gn_args_args(gn_args):
            for k, v in gn_args.items():
                if isinstance(v, bool):
                    v = str(v).lower()
                elif isinstance(v, list):
                    v = to_gn_list(v)
                elif isinstance(v, str):
                    v = to_gn_string(v)
                else:
                    raise AssertionError(
                        "Unexpected %s, %r=%r" % (type(v), k, v)
                    )
                yield "%s=%s" % (k.replace("-", "_"), v)

        buildenv = self.get_build_environment()

        # Map Gentoo ARCH to GN target_cpu. Sometimes they're the same.
        arch_to_target_cpu = {
            "amd64": "x64",
            "mips": "mipsel",
        }
        target_cpu = buildenv.get("ARCH")
        target_cpu = arch_to_target_cpu.get(target_cpu, target_cpu)
        assert target_cpu, "$ARCH is missing from the env"

        gn_args = {
            "platform_subdir": self.platform_subdir,
            "cc": buildenv.get("CC_target", buildenv.get("CC", "")),
            "cxx": buildenv.get("CXX_target", buildenv.get("CXX", "")),
            "ar": buildenv.get("AR_target", buildenv.get("AR", "")),
            "pkg-config": buildenv.get(
                "PKG_CONFIG_target", buildenv.get("PKG_CONFIG", "")
            ),
            "target_cpu": target_cpu,
            "target_os": "linux" if self.target_os is None else self.target_os,
        }

        gn_args["clang_cc"] = "clang" in gn_args["cc"]
        gn_args["clang_cxx"] = "clang" in gn_args["cxx"]
        gn_args.update(self.gen_common_args(True))
        gn_args_args = list(to_gn_args_args(gn_args))

        # Set use flags as a scope.
        uses = {}
        for flag in _IUSE:
            uses[flag] = False
        for flag in _IUSE_TRUE:
            uses[flag] = True
        for x in self.use_flags:
            uses[x.replace("-", "_")] = True
        use_args = ["%s=%s" % (k, str(v).lower()) for k, v in uses.items()]
        gn_args_args += ["use={%s}" % (" ".join(use_args))]

        # TODO(b/306186914):
        # Identify better solution to override GN build arguments.
        # Check if project specific args need to be passed to GN.
        gn_arg_file = os.path.join(
            self.get_platform2_root(), self.platform_subdir, "GN_ARGS.json"
        )
        if os.path.exists(gn_arg_file):
            with open(gn_arg_file, "rb") as fp:
                gn_json_object = json.load(fp)
                custom_gn_args = list(to_gn_args_args(gn_json_object))
                assert all(
                    x.startswith("pw_") for x in custom_gn_args
                ), f"{gn_arg_file}: Only pigweed args allowed"
                gn_args_args += custom_gn_args

        return gn_args_args

    def configure_gn(self):
        """Configure with GN.

        Runs gn gen with generated flags.
        """
        gn_args_args = self.configure_gn_args()

        gn_args = ["gn", "gen"]
        if self.verbose:
            gn_args += ["-v"]
        gn_args += [
            "--root=%s" % self.get_platform2_root(),
            "--args=%s" % " ".join(gn_args_args),
            self.get_products_path(),
        ]
        try:
            cros_build_lib.run(
                gn_args,
                extra_env=self.get_build_environment(),
                cwd=self.get_platform2_root(),
            )
        except cros_build_lib.RunCommandError:
            cros_build_lib.Die(
                "Unable to configure GN. Please check if USE "
                "flags have been added to _IUSE. See "
                "http://go/chromeos-gn for more details."
            )

    def gn_desc(self, *args):
        """Describe BUILD.gn.

        Runs gn desc with generated flags.
        """
        gn_args_args = self.configure_gn_args()

        cmd = [
            "gn",
            "desc",
            self.get_products_path(),
            "//%s/*" % self.platform_subdir,
            "--root=%s" % self.get_platform2_root(),
            "--args=%s" % " ".join(gn_args_args),
            "--format=json",
        ]
        cmd += args
        result = cros_build_lib.run(
            cmd,
            extra_env=self.get_build_environment(),
            cwd=self.get_platform2_root(),
            stdout=True,
            encoding="utf-8",
        )
        return json.loads(result.stdout)

    def compile(self, args):
        """Runs the compile step of the Platform2 build.

        Removes any existing component markers that may exist (so we don't run
        tests/install for projects that have been disabled since the last
        build). Builds arguments for running Ninja and then runs Ninja.
        """
        for component in self.get_components_glob():
            os.remove(component)

        args = ["%s:%s" % (self.platform_subdir, x) for x in args]
        ninja_args = ["ninja", "-C", self.get_products_path()]
        if self.jobs:
            ninja_args += ["-j", str(self.jobs)]
        ninja_args += args

        if self.verbose:
            ninja_args.append("-v")

        if os.environ.get("NINJA_ARGS"):
            ninja_args.extend(os.environ["NINJA_ARGS"].split())

        try:
            cros_build_lib.run(ninja_args)
        except cros_build_lib.RunCommandError:
            cros_build_lib.Die("Ninja failed")

    def deviterate(self, args):
        """Runs the configure and compile steps of the Platform2 build.

        This is the default action, to allow easy iterative testing of changes
        as a developer.
        """
        self.configure([])
        self.compile(args)

    def configure_test(self):
        """Generates test options from GN."""

        def to_options(options):
            """Convert dict to shell string."""
            result = []
            for key, value in options.items():
                if isinstance(value, bool):
                    if value:
                        result.append("--%s" % key)
                    continue
                if key == "raw":
                    result.append(value)
                    continue
                result.append("--%s=%s" % (key, value))
            return result

        conf = self.gn_desc("--all", "--type=executable")
        group_all = conf.get("//%s:all" % self.platform_subdir, {})
        group_all_deps = group_all.get("deps", [])
        options_list = []
        for target_name in group_all_deps:
            test_target = conf.get(target_name)
            outputs = test_target.get("outputs", [])
            if len(outputs) != 1:
                continue
            output = outputs[0]
            metadata = test_target.get("metadata", {})
            run_test = unwrap_value(metadata, "_run_test", False)
            if not run_test:
                continue
            test_config = unwrap_value(metadata, "_test_config", {})

            p2_test_py = os.path.join(self.get_src_dir(), "platform2_test.py")
            options = [
                p2_test_py,
                "--action=run",
                "--sysroot=%s" % self.sysroot,
            ]
            if self.strategy:
                options += [f"--strategy={self.strategy}"]
            if self.user:
                options += [f"--user={self.user}"]
            if self.bind_mount_dev:
                options += ["--bind-mount-dev"]
            if self.host:
                options += ["--host"]
            if os.environ.get("PLATFORM_HOST_DEV_TEST") == "yes":
                options += ["--bind-mount-dev"]
            if self.jobs is not None:
                options += [f"--jobs={self.jobs}"]
            p2_test_filter = os.environ.get("P2_TEST_FILTER")
            if p2_test_filter:
                options += ["--user_gtest_filter=%s" % p2_test_filter]
            options += to_options(test_config)
            options += ["--", output]

            options_list.append(options)
        return options_list

    def test_all(self, _args):
        """Runs all tests described from GN."""
        test_options_list = self.configure_test()
        if not test_options_list:
            print("WARNING: no unittests found", file=sys.stderr)
        for test_options in test_options_list:
            cros_build_lib.run(test_options, encoding="utf-8")

    def configure_install(self):
        """Generates installation commands of ebuild."""
        conf = self.gn_desc("--all")
        group_all = conf.get("//%s:all" % self.platform_subdir, {})
        group_all_deps = group_all.get("deps", [])
        config_group = collections.defaultdict(list)
        for target_name in group_all_deps:
            target_conf = conf.get(target_name, {})
            metadata = target_conf.get("metadata", {})
            install_config = unwrap_value(metadata, "_install_config")
            if not install_config:
                continue
            sources = install_config.get("sources")
            if not sources:
                continue
            install_path = install_config.get("install_path")
            outputs = install_config.get("outputs")
            symlinks = install_config.get("symlinks")
            recursive = install_config.get("recursive")
            options = install_config.get("options")
            command_type = install_config.get("type")
            do_glob = install_config.get("glob")
            tree_relative_to = install_config.get("tree_relative_to")
            if do_glob:
                sources = list(
                    itertools.chain.from_iterable(
                        # glob is always recursive to support **.
                        glob.glob(x, recursive=True)
                        for x in sources
                    )
                )
            if tree_relative_to:
                for source in sources:
                    new_install_path = install_path
                    relpath = os.path.relpath(source, tree_relative_to)
                    new_install_path = os.path.join(
                        install_path, os.path.dirname(relpath)
                    )
                    config_key = (
                        new_install_path,
                        recursive,
                        options,
                        command_type,
                    )
                    config_group[config_key].append(
                        ([source], outputs, symlinks)
                    )
            else:
                config_key = (install_path, recursive, options, command_type)
                config_group[config_key].append((sources, outputs, symlinks))
        cmd_list = []
        for install_config, install_args in config_group.items():
            args = []
            # Commands to install sources without explicit outputs nor symlinks
            # can be merged into one. Concat all such sources.
            sources = sum(
                [
                    sources
                    for sources, outputs, symlinks in install_args
                    if not outputs and not symlinks
                ],
                [],
            )
            if sources:
                args.append((sources, None, None))
            # Append all remaining sources/outputs/symlinks.
            args += [
                (sources, outputs, symlinks)
                for sources, outputs, symlinks in install_args
                if outputs or symlinks
            ]
            # Generate the command line.
            install_path, recursive, options, command_type = install_config
            for sources, outputs, symlinks in args:
                cmd_list += ebuild_function.generate(
                    sources=sources,
                    install_path=install_path,
                    outputs=outputs,
                    symlinks=symlinks,
                    recursive=recursive,
                    options=options,
                    command_type=command_type,
                )
        return cmd_list

    def install(self, _args):
        """Outputs the installation commands of ebuild as a standard output."""
        # Some commands modify ambient state and don't need to be rerun when
        # they use the same arguments.  This can save a little bit of time with
        # packages that install many files to diff dirs (e.g. system_api).
        cmd_cache = {
            "exeinto": None,
            "exeopts": None,
            "insinto": None,
            "insopts": None,
            "into": None,
        }

        install_cmd_list = self.configure_install()
        for install_cmd in install_cmd_list:
            cmd = install_cmd[0]
            if cmd in cmd_cache:
                new_args = install_cmd[1:]
                old_args = cmd_cache[cmd]
                if new_args == old_args:
                    continue
                cmd_cache[cmd] = new_args

            print(" ".join(shlex.quote(x) for x in install_cmd))


def unwrap_value(metadata, attr, default=None):
    """Gets a value like dict.get() with unwrapping it."""
    data = metadata.get(attr)
    if data is None:
        return default
    return data[0]


def GetParser():
    """Return a command line parser."""
    actions = ["configure", "compile", "deviterate", "test_all", "install"]

    parser = commandline.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--action", default="deviterate", choices=actions, help="action to run"
    )
    parser.add_argument("--board", help="board to build for")
    parser.add_argument(
        "--cache_dir", help="directory to use as cache for incremental build"
    )
    parser.add_argument(
        "--disable_incremental",
        action="store_false",
        dest="incremental",
        help="disable incremental build",
    )
    parser.add_argument(
        "--enable_tests", action="store_true", help="build and run tests"
    )
    parser.add_argument(
        "--host",
        action="store_true",
        help="specify that we're building for the host",
    )
    parser.add_argument(
        "--libdir", help="the libdir for the specific board, eg /usr/lib64"
    )
    parser.add_argument("--target_os", help="specify the target OS")
    parser.add_argument(
        "--use_flags", action="split_extend", help="USE flags to enable"
    )
    parser.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=None,
        help="number of jobs to run in parallel",
    )
    parser.add_argument(
        "--strategy",
        default="sudo",
        choices=("sudo", "unprivileged"),
        help="strategy to enter sysroot, passed to platform2_test.py",
    )
    parser.add_argument(
        "--user", help="user to run as, passed to platform2_test.py"
    )
    parser.add_argument(
        "--bind-mount-dev",
        action="store_true",
        default=False,
        help="bind mount /dev instead of creating a pseudo one, "
        + "passed to platform2_test.py",
    )
    parser.add_argument(
        "--platform_subdir",
        required=True,
        help="subdir in platform2 where the package is located",
    )
    parser.add_argument("args", nargs="*")

    return parser


def main(argv):
    parser = GetParser()

    # Temporary measure. Moving verbose argument, but can't do it all in one
    # sweep due to CROS_WORKON_MANUAL_UPREVed packages. Use parse_known_args
    # and manually handle verbose parsing to maintain compatibility.
    options, unknown = parser.parse_known_args(argv)

    if not hasattr(options, "verbose"):
        options.verbose = "--verbose" in unknown

    if "--verbose" in unknown:
        unknown.remove("--verbose")
    if unknown:
        parser.error("Unrecognized arguments: %s" % unknown)

    if options.host and options.board:
        raise AssertionError("You must provide only one of --board or --host")

    if not options.verbose:
        # Should convert to cros_build_lib.BooleanShellValue.
        options.verbose = os.environ.get("VERBOSE", "0") == "1"
    p2 = Platform2(
        options.use_flags,
        options.board,
        options.host,
        options.libdir,
        options.incremental,
        options.target_os,
        options.verbose,
        options.enable_tests,
        options.cache_dir,
        options.strategy,
        options.user,
        options.bind_mount_dev,
        jobs=options.jobs,
        platform_subdir=options.platform_subdir,
    )
    getattr(p2, options.action)(options.args)


if __name__ == "__main__":
    commandline.ScriptWrapperMain(lambda _: main)
