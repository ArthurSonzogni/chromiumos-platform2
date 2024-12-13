# Fingerprint Study Image Builder

This tool builds a ChromeOS image with the [Collection Tool] included and
configured.

This is done by enabling the `fpstudy` USE flag, which then enables the
inclusion of the
[chromeos-base/fingerprint_study package](https://crsrc.org/o/src/third_party/chromiumos-overlay/chromeos-base/fingerprint_study/fingerprint_study-9999.ebuild).

[Collection Tool]: ../collection-tool/

## Quick Start

```bash
make CONFIG=brya-latest-test
```

## Setup ChromeOS

See
https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html#_setting_up
to setup depot_tools.

## Formatting `TEMPLATE_README.md.in`

```bash
mdformat --in_place --compatibility --skip_extension_check TEMPLATE_README.md.in
```
