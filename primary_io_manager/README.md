# PrimaryIoManager

PrimaryIoManager is responsible for tracking 'primary' keyboard/mouse/trackpack devices on
chromebox-format devices. For this service, mouse and trackpad devices are treated as mice.

The system listens on a Udev monitor for input subsystem events, and uses udev identifiers to
distinguish keyboard/mouse devices (this is not foolproof, but should be precise enough for the
purposes of this service). It marks the first keyboard or mouse it sees as the primary
(theoretically one device could be both).
