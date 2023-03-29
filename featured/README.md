# ChromeOS Feature Daemon

`featured` is a service used for enabling and managing platform specific
features. Its main user is Chrome field trials.

## Components

In this directory are two main components: `feature_library` and featured.

### feature\_library

The `feature_library` is the main way most users will interact with variations
in platform2. (If you're familiar with
[base::Feature and base::FeatureList](https://source.chromium.org/chromium/chromium/src/+/main:base/feature_list.h)
in chrome, it will look very similar.)  Most documentation for this is in
`feature_library.h`, but there is also a C wrapper API as well as a Rust API.

As of March 2023, all features queried via `feature_library` must start with
CrOSLateBoot. For CrOSLateBoot features, `feature_library` is a thin wrapper
around a dbus call to ash-chrome, so state can only be queried **after** chrome
is up.

Work is underway to support "early boot" features as well; feel free to contact
OWNERS for more details.

### featured

The feature daemon (featured) is primarily responsible for managing
[share/platform-features.json](https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/featured/share/platform-features.json).
This file configures platform and kernel features that can be enabled
_dynamically_ at runtime, late in boot (that is, after user login).

Each entry in this file consists of a feature name, to be checked using
`feature_library`, an optional set of `support_check_commands` (to check whether
the device supports the feature), and a set of `commands` to be run when the
feature is supported and chrome determines that it should be enabled.

Support check commands include FileExists and FileNotExists. Commands to execute
include WriteFile and Mkdir, which respectively write specified contents to a
file and create a given directory.

Featured is heavily sandboxed: writing to a new location requires an update to
selinux policy in `platform2/sepolicy/policy/chromeos/cros_featured.te` as well
as the allow-list in service.cc (see `CheckPathPrefix`).

We are actively working (in 2023) on support for "early boot" features in
featured. The primary user interface for these will be via `feature_library`,
but featured will perform some work to support this, largely behind the scenes.
