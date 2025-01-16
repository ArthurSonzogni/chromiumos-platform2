# CrOS Toolchain

This contains projects worked on by the ChromeOS toolchain team that need to be
made available to the ChromeOS CQ.

Some of our code is also located in the `src/third_party/toolchain-utils`
directory. That repo is designed to be shared with other projects (specifically,
Android), and the CQ there has no overlap with ChromeOS' "regular" CQ.

Subdirectories:

- `fortify-tests/` -- used by the chromeos-base/toolchain-tests ebuild to verify
  that FORTIFY crashes in expected cases.
