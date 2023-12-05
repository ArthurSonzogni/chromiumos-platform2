V4L2 media-ctl toolkit
======================

The V4L2 media-ctl toolkit, or simply `mctk`, is a collection of routines to
capture, manipulate, and restore the state of a Video4Linux2 "media controller"
and all of its related devices, within the framework/API of V4L2.



In-memory models of media-ctl devices and sub-devices
-----------------------------------------------------

By design, the tool works primarily on an in-memory representation of a
tree of a media-ctl device and its subnodes, hereforth called a "model".
Secondarily, it may also send any changes to this model back to real
devices, accessible via /dev/*.

There is always only one "active" media-ctl tree/model being updated/worked on.

Whether updates are sent to the kernel depends on the origin of the active
model:

  1) If the model was loaded from a real /dev/mediaX device, then the tool
     will keep all its file descriptors to the associated devices open, and
     changes to the model will be propagated to the kernel.

     NOTE: The tool does not refresh its model with new kernel values after
           setting a value in the kernel. That is, an `*_S_*` ioctl() is NOT
           followed by one or more `*_G_*` ioctl()s. It is up to the user to
           know about any intricacies of the V4L2 device being manipulated.

  2) If the model was loaded from a dump file, then any changes will be
     reflected only in memory.

In both cases, the final state of the memory model can be dumped to a new file.



Processing order
----------------

The parameters to this toolkit are processed sequentially, in the order they
are given on the command line. In effect, they form a "script" to be executed.

The idea is that
    # ./mctk --action1 paramX --action2 --action3 paramY

will first do action 1 with parameter X, then action 2 which does not take
a parameter, and finally action 3, which takes parameter Y.



Load
----

A load action is always the first.

It opens either a kernel device or a dump file, and creates an in-memory
model of a media-ctl device. All further operations will be executed on
this in-memory model.

If the model has been loaded from a kernel device, then actions will ALSO
be fed back to the kernel, via appropriate ioctl()s.

If the model has been loaded from a file, then that file will not be modified.
Please use the dump action to write the modified state to a new file.

If a new load action follows a previous one, then the tool will abort.


### Usage

The following command opens a real media-ctl device for processing,
and then quits without performing any action on it:

    # ./mctk --load-device /dev/media0


The following command opens a virtual media-ctl device from a YAML dump,
and then quits without performing any action on the resulting model:

    # ./mctk --load-yaml dump.yaml



Dump
----

Internally known as "Tool-1".

The currently active model is serialised to a file.


### Usage

The following command line prints a YAML formatted dump of /dev/media0
and its child nodes to stdout:

    # ./mctk --load-device /dev/media0 --dump-yaml /proc/self/fd/1



Merge
-----

Internally known as "Tool-3".

A (part of a) serialised state is read and applied to the currently active
in-memory model. If the active model is a kernel device, then the configuration
is also sent to the hardware (minus quirks in the driver/hardware that make the
restoration work less than 100% - it is up to the user to know these details).

If the input file to the merge action contains a remap section, then the
entities to be modified in the active model are identified by names rather
than by entity ID number.


### Usage

The following command line opens a real media-ctl device, disables all links
where possible, and then applies a configuration from a YAML file:

    # ./mctk --load-device /dev/media0 --reset-links --merge-yaml config.yaml


### Restrictions and assumptions

#### Existence and reordering

During a merge, it is assumed that the entities, pads, links, properties,
controls, etc. being configured actually exist in the target, and that the
identifiers (id, name, index, etc.) refer to the same things as when the
configuration file was created.

For example, if "CSI 0" and "CSI 1" refer to the front and back camera,
respectively, and then a driver update swaps these two numbers, then the
tool will be unable to detect this, and blindly attempt to apply one
camera's configuration to the other.

It is ultimately up to the user to ensure that the intended semantics of
a configuration file match the device being configured.

##### Examples

If e.g. a control being configured is absent, then the tool may
either skip updating the control in question, or abort.

If e.g. an integer control is to be assigned a string, then the tool may
either skip updating the parameter in question, or abort.

But if there has simply been a reordering between otherwise identical
entities, and both have the same controls, then the tool cannot detect
this and will blindly update the controls.

#### Configuration order

V4L2 drivers are not configured atomically.
Changing one value can result in a driver updating other values, too.

The tool will blindly configure parameters in the order in which they
are listed in the configuration file. If the V4L2 device is not in the
desired state afterwards, then a deeper understanding of the driver is
needed, and a manual reordering of configuration steps is required.

To this end, an entity may be named multiple times in a configuration
file, allowing parameters to be set in a specific order (and even
multiple times).



Autorouter
----------

NOTE: Unfinished old code and outdated documentation!

This tool attempts to detect sensors and route them to viable
outputs on the specified V4L media controller device.


### Usage

    # ./mctk --load-device /dev/media0 --reset-links --auto-route
    Resetting links.
    Autorouting sensors.
    SetSelection: ioctl(VIDIOC_SUBDEV_S_SELECTION): Inappropriate ioctl for device
    SetSelection: ioctl(VIDIOC_SUBDEV_S_SELECTION): Inappropriate ioctl for device
    SetSelection: ioctl(VIDIOC_SUBDEV_S_SELECTION): Inappropriate ioctl for device
    SetSelection: ioctl(VIDIOC_SUBDEV_S_SELECTION): Invalid argument
    SetFmtVideoCapture: ioctl(VIDIOC_S_FMT): Invalid argument
    SetSelection: ioctl(VIDIOC_S_SELECTION): Inappropriate ioctl for device
    Routed: /dev/video8 = ov8856 13-0036
    # yavta -c /dev/video8
    [ ... capture follows ... ]

NOTE: The above error messages are benign - the autorouter attempts to
      set properties, whether they are supported or not.


### How this works

1. The tool requests the entire topology of entities and links exposed
   by the media controller via MEDIA_IOC_ENUM_ENTITIES and
   MEDIA_IOC_ENUM_LINKS.

2. It disables all links between entities.

3. It picks the entities marked as MEDIA_ENT_T_V4L2_SUBDEV_SENSOR, and
   for each one, tries to find a path from one of their output pads to
   a /dev/videoX device.

   It recurses through the tree of links in a DFS fashion, writing down
   a path successfully found.

   If a device name matches one of a few known patterns, it is ignored
   as a routing hop - see v4l_mc_hack_is_ipu6_ignored_entity().

4. It enables the links along each path found.

5. It reads the sensor's picture format, and attempts to set the same
   format on each device along the path.

   For the final V4L /dev/videoX device, it takes a guess at the format,
   but only Bayer raw formats are currently translated.

6. It also sets each V4L selection, using two strategies (to allow for
   drivers that support one or the other).

7. It disables IPU6 image compression on any /dev/videoX device enabled
   in this process, in case it supports this feature.


### Restrictions and assumptions

Units that are already connected to something are ignored in any further
routing. That is, if there are multiple sensors, then multiple paths will
be generated, but they will never cross by using an entity twice.

It is assumed that DFS routing can result in valid paths for all sensors.
If sensor 0 can only be assigned to outputs {0,1} and sensor 1 can only
be assigned to output 0, then routing the second sensor will fail because
its only viable output will already be in use by sensor 0.



Miscellaneous design decisions
------------------------------

### Thread safety

This program is written with the assumption that it is run single-threaded.
