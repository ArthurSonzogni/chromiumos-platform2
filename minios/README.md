# MiniOS
MiniOS is a subset of thanOS, also known as the Network Based Recovery Project
(go/cros-nbr). MiniOS consumer facing feature used to help recover devices
using only a network or ethernet connection. Once connected to the network,
MiniOS will automatically repartition the disk, wipe the stateful partition and
all user data, and update to the latest stable version. The UI theme and
elements are an extension of the Groot recovery flow.

The main MiniOS components include D-bus, update_engine, and network manager.

## Upstart
MiniOS is brought up with Upstart, similar to ChromeOS. The majority of the
scripts are located in [platform2/init/upstart] with some MiniOS specific
processes located in [minios/init].

## Frecon
MiniOS graphics are done with frecon-lite and can show images and draw shapes.
The layout of MiniOS elements is based on the frecon canvas size and scaling
factor which are from [frecon.conf]. The images are created with Pango during
build time with no run time size or color changes supported.

## KeyReader
KeyReader is how MiniOS processes input. There are two types of input
KeyReader handles.

The first type is basic navigation using the up, down, and
enter keys. This is used to navigate between screens and make basic selections.
This type of input uses an Epoll file descriptor which sends all input to
ScreenController so each screen can decide what action to take based on user
input.

The other kind of functionality KeyReader supports is taking in alphanumeric
input, which is used for things like the network password; this does not use
the watcher. For this type of input, KeyReader must be given a keyboard layout
to correctly map key codes to characters. Currently, there is no foolproof way
to do this, but the region information is read from VPD and then mapped to a
list of supported chrome keyboard layouts using cros-regions.json. Due to the
size and complexity constraints of MiniOS, only standard ASCII characters are
processed. Other special characters such as "ê" are ignored even if they are a
part of the keyboard layout.

[platform2/init/upstart]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/init/upstart/
[minios/init]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/minios
[frecon.conf]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/minios/init/frecon.conf
