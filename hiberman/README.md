# Hiberman - The Hibernate Manager

This package implements the Hibernate Manager, a userspace system utility that
orchestrates hibernate and resume on Chrome OS.

## What is hibernate

At a high level, hibernate is a form of suspend that enables better power
consumption levels than suspend to RAM at the cost of additional entrance and
exit latency. In a traditional suspend, the CPUs are powered off, but RAM
remains powered on and refreshing. This enables fast resume times, as only the
CPU and device state need to be restored. But keeping RAM powered on and
refreshing comes with a cost in terms of power consumption. Hibernate is a form
of suspend where we also power down RAM, enabling us to completely shut down the
system and save more power. Before going down, we save the contents of RAM to
disk, so it can be restored upon resume. The contents of RAM here contain every
used page, including the running kernel, drivers, applications, heap, VMs, etc.
Note that only used pages of RAM need actually be saved and restored.

## How the kernel hibernates

From the kernel's perspective, the traditional process of suspending for
hibernation looks roughly like this:

 * All userspace processes are frozen
 * The kernel suspends all devices (including disks!) and all other CPUs
 * The kernel makes a copy of all in-use RAM, called a "snapshot", and keeps
   that snapshot in memory
 * The kernel resumes all devices
 * The snapshot image is written out to disk (now that the disk is no longer
   suspended)
 * The system enters S4 or shuts down.

The system is now hibernated. The process of resuming looks like this:

 * The system is booted, in an identical manner to a fresh power on
 * The kernel is made aware that there is a hibernate image (traditionally
   either by usermode or a kernel commandline argument)
 * The hibernate image is loaded back into memory
   * Some pages will be restored to their rightful original location. Some
     locations that need to be restored may already be in use by the currently
     running kernel. Those pages are restored to temporary RAM pages that are
     free in both the hibernated kernel and the current kernel.
 * All userspace processes are frozen
 * The kernel suspends all devices and all other CPUs
 * In a small stub, the kernel restores the pages that were previously loaded
   into temporary RAM locations, putting them in their final location. At this
   point, the kernel is committed to resume, as it may have overwritten large
   chunks of itself doing those final restorations.
 * Execution jumps to the restored image
 * The restored image resumes all devices and all other CPUs
 * Execution continues from the usermode process that requested the hibernation

## How hiberman hibernates

The steps in the previous section outline what a traditional kernel-initiated
hibernation looks like. Hiberman gets a little more directly involved with this
process by utilizing a kernel feature called userland-swsusp. This feature
exposes a snapshot device at /dev/snapshot, and enables a usermode process to
individually orchestrate many of the steps listed above. This has a number of
key advantages for Chrome OS:

 * The hibernate image can be encrypted with a key derived from user
   authentication (eg the user's password and the TPM)
 * Image loading can be separated from image decryption, allowing us to
   frontload disk I/O latency while the user is still authenticating
 * The methods used for storing and protecting the image don't have to be
   generalized to the point of being acceptable to the upstream kernel community

With this in mind, hiberman will do the following to hibernate the system:

 * Load the public hibernate key, which is specific to the user and machine.
   Generation of this key is described later.
 * Preallocate file system files, reserving space up front for the hibernate
   image, hibernate metadata, and logs
 * Generate a random symmetric key, which will be used to encrypt the hibernate
   image
 * Freeze all other userspace processes, and begin logging to a file
 * Ask the kernel to create its hibernate snapshot
 * Write the snapshot out to the disk space underpinning the preallocated file,
   encrypting it using the random symmetric key
 * Encrypt the random symmetric key using the public key. Save the result along
   with some other metadata like the image size into a preallocated metadata
   file.
 * Set a cookie at a known location towards the beginning of the disk indicating
   there's a valid hibernate image
 * Shut the system down

Resume is slightly harder to follow, because there is the "resume" path, the
"failed resume" path, and the "no resume" path:

 * The system powers on like any other boot. AP firmware runs, the Chrome ball
   delights our senses, and chromeos_startup runs
 * chromeos_startup calls out to hiberman to read the cookie and determine if a
   hibernate resume is expected or not. This instantiation of hiberman always
   runs and exits quickly, as its only role this early in boot is to check the
   cookie and return.
 * From here, things fork into two paths. In the "resume path":
   * The hibernate cookie is set, indicating there is a valid hibernate image
     the system may want to resume to
   * Instead of mounting the stateful partition with read/write permissions, a
     dm-snapshot device is created on top of the stateful partition, and then
     the snapshot is mounted read/write.
     * Reads hit the real stateful partition, but writes are transparently
       diverted into a snapshot area in RAM
     * This is done to avoid modifying the stateful partition, which the
       hibernated image still has active mounts on
   * Hiberman is invoked with the "resume" subcommand by upstart, sometime
     around when system-services start
     * Hiberman will load the unencrypted portion of the hibernate metadata,
       which contains untrusted hints of the image size and header pages
     * The resume cookie and metadata are cleared from disk to prevent resume
       crashes from looping infinitely
     * Hiberman will divert its own logging to a file, anticipating a successful
       resume where writes from this boot are lost
     * The header pages can be loaded into the kernel early, causing the kernel
       to allocate its large chunk of memory to store the hibernate image.
     * Hiberman will begin loading the hibernate image into its own process
       memory, as a way to frontload the large and potentially slow disk read
       operation.
     * Eventually hiberman will either load the whole image, or will stop early
       to prevent the system from becoming too low on memory. At this point,
       hiberman blocks and waits
   * Boot continues to Chrome and the login screen.
   * The user authenticates (for example, typing in their password and hitting
     enter)
     * If the user who logs in is not the same as the user who hibernated, the
       hibernated image is discarded, the snapshot RAM image is merged into the
       stateful partition on disk, and future writes go directly to the stateful
       partition.
     * The rest of this section assumes the user who logs in is the same user
       that hibernated.
   * Cryptohome computes the hibernate key in a similar manner to how it
     computes the user's file system keys, and makes a d-bus call to hiberman to
     give it the hibernate secret key material.
   * Hiberman wakes up from its slumber with the secret seed in hand. It uses
     this to derive the private key corresponding to the public key used at the
     beginning of hibernate.
   * Hiberman uses the private key to decrypt the private portion of the
     metadata.
   * Hiberman validates the hash of the header pages in the private metadata
     against what it observed while loading earlier. Resume is aborted if these
     don't match.
   * Hiberman gets the random symmetric key used to encrypt the image
   * Hiberman can then push the now-mostly-in-memory hibernate image into the
     kernel (through the snapshot device), decrypting as it goes
   * Hiberman freezes all usermode processes except itself
   * Hiberman asks the kernel to jump into the resumed image via the atomic
     restore ioctl
     * Upon success, the resumed image is now running
     * Upon failure, the snapshot device is committed to disk, suspend and
       resume logs are replayed to the syslog, and a fresh boot login continues
       normally.
 * In the "no resume" path, where there is no valid hibernate image:
   * The cookie is not set, so the stateful partition is mounted directly with
     read/write permissions.
   * Hiberman is still invoked with the "resume" subcommand during init
     * Common to the successful resume case, hiberman attempts to read the
       metadata file, but discovers there is no valid image to resume
     * Also common to the successful resume case, hiberman blocks waiting for
       its secret key material
     * Eventually, a user logs in
     * Cryptohome makes its d-bus call to hand hibernate the secret key material
     * Hiberman computes the asymmetric hibernate key pair, but discards the
       private portion
     * The public portion is saved in a ramfs file, to be used in the first step
       of a future hibernate

Upon a successful resume, the tail end of the hiberman suspend paths runs. It
does the following:
 * Unfreezes all other userspace processes
 * Reads the suspend and resume log files from disk, and replays them to the
   syslogger
 * Exits

## Wrinkles and Constraints

### Half of memory

In the description of how the kernel hibernates, you'll notice that the kernel
creates the hibernate snapshot image in memory, but that image itself represents
all of used memory. You can see the challenge here with storing the contents of
memory in memory. The constraint that falls out of this mechanism is that at
least 50% of RAM must be free for the hibernate image to be successfully
generated (at best you can store half of memory in the other half of memory).

In cases where more than 50% of RAM is in use when hibernate is initiated, swap
comes to the rescue. When the kernel allocates space for its hibernate image,
this forces some memory out to swap. We can enable this swap just before
hibernate is initiated. Once the system resumes, swap can be turned off, with
the system being usable as pages are actively (rather than passively) faulted
back in after resume.

### No RW mounts

Another interesting challenge presented by hibernate is the fact that the
hibernated image maintains active mounts on file systems. This means that in
between the time the snapshot is taken, and when it's been fully resumed,
modifications to areas like the stateful partition are not allowed. If this is
violated, the resumed kernel's in-memory file system structures will not be
consistent with what's on disk, likely resulting in corruption or failed
accesses.

This presents a challenge for the entire resume process, which consists of
booting Chrome all the way through the login prompt in order to get the
authentication-derived asymmetric key pair. To get this far in boot without
modifying the stateful partition, we utilize dm-snapshot (not to be confused
with the hibernate snapshot). With dm-snapshot we can create a block device
where reads come from another block device, but writes are diverted elsewhere.
This gives the system the appearance of having a read/write file system, but in
this case all writes are diverted to a loop device backed by a ramfs file. Upon
a successful resume, all writes to the stateful partition that happened during
this resume boot are effectively lost. It's as if that resume boot never
happened, which is exactly what the hibernated kernel needs. Upon a failed or
aborted resume, we merge the RAM writes back down to the stateful partition.
Once this is complete, we transparently rearrange the dm table so that future
writes go directly to the stateful partition, instead of diverting elsewhere.

One constraint of the dm-snapshot approach is that it's important that nothing
attempts to write too wildly to the stateful partition, since the writes are
stored in RAM. Most components are quiet before any user has logged in, but
system components like update_engine and possibly ARCVM will need to be aware of
hibernate resume boots. The consequences of violating this constraint are that
stateful writes begin failing, which doesn't pose any risk to user data, but
does require a reboot to recover from.

### Saving hibernate state

There's another side effect of not being able to touch the stateful file system
after the hibernate snapshot is created: where do we store the hibernate image
and metadata itself? The traditional route is to use a dedicated partition for
hibernate data. This has several drawbacks for Chrome OS:

 * Disk space is permanently reserved for hibernate, becoming forever
   unavailable to the user
 * Due to the way Chrome OS structures its build around "boards", the hibernate
   image would have to be sized to support the maximum amount of RAM for any
   device in that board family. This would result in large amounts of storage
   permanently wasted for smaller RAM configurations.
 * Enabling hibernate on already shipped board families would likely be
   impossible due to the risky online partition layout change that would need to
   happen to reserve space for the hibernate partition.

Instead, we chose to use the stateful file system as a sort of disk area
reservation system. This enables us to create and destroy the hibernate data
area at will, size it per-machine based on the amount of RAM installed, and ship
the hibernate service on boards post-FSI.

What we do is create files in the stateful partition before the hibernate
snapshot image is taken (at a time when file system modifications are still
fine). We use the fallocate() system call to both size the file appropriately,
and ensure that disk space is completely committed for the whole file (in other
words, there are no "holes" in the file). We then use the FIEMAP ioctl to get
the file system to report the underlying disk regions backing the file. With
that information, we can read and write directly to the partition or disk at
those regions. The file system sees those extents as "uninitialized" (since the
file system has only reserved space for them, not written to them), and makes no
assumptions about the contents of those areas. Because we're not interacting
with file system calls, and hiberman is the only process running, we're
minimizing the risk that we'll accidentally modify the stateful partition after
the snapshot image is taken.

We use these "disk files" both to save the hibernate data and metadata, and to
pass logging information through from the suspend and resume process into the
final resumed system. We must be careful to operate on the partition with flags
(O_DIRECT) that ensure the kernel won't cache the disk content, giving us stale
reads on resume.

## Nickel Tour

Below is a quick overview of the code's organization, to help readers understand
how the app is put together:

 * main.rs - The entry point into the application. Handles command line
   processing and calling out to the main subcommand functions.
 * hiberman.rs - The main library file. It contains almost nothing but a couple
   wrappers to call other modules that do the real work, and a list of
   submodules within the library.
 * suspend.rs - Handles the high level orchestration of the suspend process
 * resume.rs - Handles the high level orchestration of the resume process
 * cat.rs - Handles the cat subcommand
 * cookie.rs - Handles the cookie subcommand and functionality

The rest of the files implement low level helper functionality, either an
individual component of the hibernate process or a collection of smaller
helpers:

 * crypto.rs - Handles bulk symmetric encryption of the big hibernate data image
 * dbus.rs - Handles dbus interactions
 * diskfile.rs - Provides a Read, Write, and Seek abstraction for "disk files",
   which operate directly on the partition areas underneath a particular file
   system file.
 * fiemap.rs - Provides a friendier interface to the fiemap ioctl, returning the
   set of extents on disk which comprise a file system file. This is used by the
   DiskFile object to get disk information for a file.
 * files.rs - A loose collection of functions that create or open the stateful
   files used by hibernate. Possibly to be overridden during testing.
 * hiberlog.rs - Handles the more-complicated-than-average task of logging. This
   module can store logs in memory, divert them to a DiskFile, push them out to
   the syslogger, and/or write them to /dev/kmsg.
 * hibermeta.rs - Encapsulates management of the hibernate metadata structure,
   including loading/storing it on disk, and encrypting/decrypting the private
   portion.
 * hiberutil.rs - A miscellaneous grab bag of functions used across several
   modules.
 * imagemover.rs - The "pump" of the hibernate image pipeline, this is the
   component calling read() and write() to move data between two file
   descriptors.
 * keyman.rs - Encapsluates creation and management of the asymmetric key pair
   used to protect the private hibernate metadata.
 * mmapbuf.rs - A helper object to create large aligned buffers (which are a
   requirement for files opened with O_DIRECT).
 * preloader.rs - An object that can be inserted in the image pipeline that will
   load contents from disk early and store it in memory for a spell.
 * snapdev.rs - Encapsulates ioctl interactions with /dev/snapshot.
 * splitter.rs - An object that can be inserted in the image pipeline that
   splits or merges the snapshot data into a header portion and a data portion.
   This is used to coerce the kernel into doing its giant resume allocation
   before preloading starts in earnest.
 * sysfs.rs - A miscellaneous file for temporarily modifying sysfs files during
   the hibernate process.
