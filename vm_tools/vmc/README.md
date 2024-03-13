# vmc - virtual machine command tool

This directory contains the code for the `vmc` utility, a command line tool
available for end users to send commands to VMs running in ChromeOS.

## Deploying to a ChromeOS image

`vmc` is built by the `chromeos-base/crostini_client` package, you can deploy it
with the following command:

```shell
$ cros deploy $DUT crostini_client
```

## Making changes

`vmc` depends on changes made to the `system_api` package to build protobuf and
dbus API definitions. These changes need to be built both at the chroot/system
level, and also at the board/device level, where they need to be deployed to the
DUT.

If your change does not involve adding a new API or changing the signature of
any protobuf/dbus message, you only need to `cros_workon` on the
`crostini_client` package:

```shell
$ cros_sdk cros_workon --board=$BOARD start crostini_client
$ FEATURES=test cros_sdk emerge-$BOARD crostini_client
```

However, if your change is more involved, like if you are adding a new command,
you will want to `cros_workon` on the `system_api` package for *both* the dev
chroot system where the packages are built, and also on the chromeos package
that is being deployed on the DUT. Don't forget to also deploy the `system_api`
changes to the DUT:

```shell
$ cros_sdk cros_workon --board=$BOARD start dev-rust/system_api
$ cros_sdk cros_workon --board=$BOARD start chromeos-base/system_api
$ cros_sdk emerge-$BOARD dev-rust/system_api
$ cros_sdk emerge-$BOARD chromeos-base/system_api
$ cros_sdk cros deploy $DUT chromeos-base/system_api
```

### Adding a new command

If you are adding a new command to the client, there is more piping involved as
more changes need to be made at different levels including possibly `crosvm` and
`concierge` (or similar `vm_host_tools` like `cicerone`, etc).

You can use these two example CLs for reference with the explanation below:

-   [crosvm_control](https://chromium-review.googlesource.com/c/crosvm/crosvm/+/5364552)
-   [vm_tools](https://chromium-review.googlesource.com/c/chromiumos/platform2/+/5348421)

If your command interfaces with `crosvm`, you will need to add an API entry
point in the `crosvm_control`. This will be compiled as a shared library by
`concierge` and other `vm_host_tools` and be called when the right dbus request
comes in from the `vmc` command.

Then, you will want to add the right bindings in `concierge/crosvm_control.cc`
to call that library.

You will need to generate a new dbus protobuf request and response for
`concierge` in the `system_api` package at `concierge_service.proto`.

You also need to add the proper dbus bindings at
`dbus_bindings/org.chromium.VmConcierge.xml`.

The rest of the code at the concierge level (`service.cc`, `vm_base_impl.cc`,
`vm_util.cc`) takes care of sanitizing the right inputs, opening files, passing
file descriptors around, and eventually interfacing with the running `crosvm`
instance via control requests.

If your command requires opening files by path, we opt to pass around open file
descriptors instead of fully qualified paths. You can obtain a unique path
accessible by the VM process by opening the equivalent file path at
`/proc/self/fd/$FD` where `$FD` is the opened file descriptor passed via dbus
RPCs.

The RPC flow is something like this:

vmc command --[dbus RPC proto]--> concierge --[crosvm_control]--> crosvm
