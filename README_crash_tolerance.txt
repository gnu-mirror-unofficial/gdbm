
Crash Tolerance for GNU dbm
===========================

This file describes a new (as of release 1.21) feature that can be
enabled at compile time and used in environments with appropriate
support from the OS (currently Linux) and filesystem (currently XFS,
BtrFS, and OCFS2).  The feature is a "pure opt-in," in the sense that
it has no effect whatsoever unless it is explicitly enabled at
compile time and used by applications.  It has been tested on
late-2020-vintage Fedora Linux and XFS.

See the "Drill Bits" column in the July/August 2021 issue of ACM
_Queue_ magazine for a broader discussion of crash-tolerant GNU dbm.
If for whatever reason you can't access this column, contact the
author (Kelly).

Read and thoroughly understand this file before attempting to use the
new feature.  Address questions/feedback to the maintainer(s) and to
Terence Kelly, tpkelly@{acm.org, cs.princeton.edu, eecs.umich.edu}.

- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

Background:

Historically GNU dbm did not tolerate crashes: An ill-timed crash due
to a power outage, an operating system kernel panic, or an abnormal
application process termination could corrupt or destroy data in the
database file.  Corruption is likely if a crash occurs during updates
to the GDBM database file, e.g., during a gdbm_store() or gdbm_sync()
call.  Therefore GNU dbm was not suitable for applications that
require the ability to recover an up-to-date consistent state of
their persistent data following a crash.  Such applications resorted
instead to alternative "transactional" NoSQL data stores such as
BerkeleyDB or Kyoto Cabinet, or even full-blown SQL databases such as
MySQL or SQLite.  Which is unfortunate if all the application really
needs is a crash-tolerant GDBM.

New crash-tolerance feature:

GNU dbm now includes an optional crash-tolerance mechanism that, when
used correctly, guarantees that a consistent recent state of
application data can be recovered followng a crash.  Specifically, it
guarantees that the state of the database file corresponding to the
most recent successful gdbm_sync() call can be recovered.  Crash
tolerance must be enabled when the GNU dbm library is compiled, and
applications must request crash tolerance for each GDBM_FILE by
calling a new API.

If the new mechanism is used correctly, crashes such as power
outages, OS kernel panics, and (some) application process crashes
will be tolerated.  Non-tolerated failures include physical
destruction of storage devices and corruption due to bugs in
application logic.  For example, the new mechanism won't help if a
pointer bug in your application corrupts gdbm's private in-memory
data which in turn corrupts the database file.

Using crash tolerance:

(1) The GNU dbm library must be built with an additional C compiler
#define flag.  After unpacking the tarball, from the C-shell command
line it suffices to do the following before running make:

    % setenv CFLAGS -DGDBM_FAILURE_ATOMIC
    % ./configure CFLAGS=-DGDBM_FAILURE_ATOMIC >& configure.out

(2) You must use a filesystem that supports reflink copying.
Currently XFS, BtrFS, and OCFS2 support reflink.  You can create such
a filesystem if you don't have one already.  (Note that reflink
support may require that special options be specified at the time of
filesystem creation; this is true of XFS.)  The most conventional way
to create a filesystem is on a dedicated storage device.  However it
is also possible to create a filesystem *within an ordinary file* on
some other filesystem.  For example, executing the following commands
from the C-shell command line will create a smallish XFS filesystem
inside a file on an ext4 filesystem:

    % mkdir XFS
    % cd XFS
    % sudo truncate --size 512m XFSfile
    % sudo mkfs.xfs -m crc=1 -m reflink=1 XFSfile
    % sudo mkdir XFSmountpoint
    % sudo mount -o loop XFSfile XFSmountpoint
    % sudo xfs_info XFSmountpoint
    % cd XFSmountpoint
    % sudo mkdir test
    % set me = `whoami`':'`whoami`
    % sudo chown $me test
    % cd test
    % echo foo > bar
    % ls -l bar

After executing the commands above, from the diretory where you
started you should see a directory XFS/XFSmountpoint/test/ where your
unprivileged user account may create and delete files.  Reflink
copying via ioctl(FICLONE) should work for files in and below this
directory.  You can test reflink copying using the GNU "cp"
command-line utility: "cp --reflink=always file1 file2".  Read the
manpage for the Linux-specific API "ioctl_ficlone(2)" for additional
information.

Your GNU dbm database file and two other files described below must
all reside on the same reflink-capable filesystem.

(3) In your application source code, #define GDBM_FAILURE_ATOMIC
before you #include <gdbm.h>.

(4) Open a GNU dbm database with gdbm_open().  Unless you know what
you are doing, do *not* specify the GDBM_SYNC flag when opening the
database.  The reason is that you want your application to explicitly
control when gdbm_sync() is called; you don't want an implicit sync
on every database operation.

(5) Request crash tolerance by invoking the following new interface:

    gdbm_failure_atomic(GDBM_FILE dbf, const char *even, const char *odd);

"even" and "odd" are the pathnames of two files that will be created
and filled with snapshots of the database file.  These two files must
*not* exist when gdbm_failure_atomic() is called and must reside on the
same filesystem as the database file.  The filesystem must support
reflink copying, i.e., ioctl(FICLONE) must work.

After you call gdbm_failure_atomic(), every call to gdbm_sync() will
make an efficient reflink snapshot of the database file in either the
"even" or the "odd" snapshot file; consecutive gdbm_sync() calls
alternate between the two, hence the names.  The permission bits and
last-mod timestamps on the snapshot files determine which one
contains the state of the database file corresponding to the most
recent successful gdbm_sync().  Post-crash recovery is described
below.

(6) When your application knows that the state of the database is
consistent (i.e., all relevant application-level invariants hold),
you may call gdbm_sync().  For example, if your application manages
bank accounts, transferring money from one account to another should
maintain the invariant that the sum of the two accounts is the same
before and after the transfer: It is correct to decrement account A
by $7, increment account B by $7, and then call gdbm_sync().  However
it is *not* correct to call gdbm_sync() *between* the decrement of A
and the increment of B, because a crash immediately after that call
would destroy money.  The general rule is simple, sensible, and
memorable: Call gdbm_sync() only when the database is in a state from
which you are willing and able to recover following a crash.  (If you
think about it you'll realize that there's never any other moment
when you'd really want to call gdbm_sync(), regardless of whether
crash-tolerance is enabled.  Why on earth would you push the state of
an inconsistent unrecoverable database down to durable media?).

(7) If a crash occurs, the snapshot file ("even" or "odd") containing
the database state reflecting the most recent successful gdbm_sync()
call is the snapshot file whose permission bits are read-only and
whose last-modification timestamp is greatest.  If both snapshot
files are readable, we choose the one with the most recent
last-modification timestamp.  Following a crash, *do not* do anything
that could change the file permissions or last-mod timestamp on
either snapshot file!

The gdbm_latest() function takes two filename arguments---the "even"
and "odd" snapshot filenames---and tells you which is the most recent
readable file.  That's the snapshot file that should replace the
original database file, which may have been corrupted by the crash.

Return values:

Both new functions, gdbm_failure_atomic() and gdbm_latest(), pinpoint
mishaps by returning the *negation* of the source code line number on
which something went wrong: "return (-1 * __LINE__)".  So to diagnose
problems, "use the Source, Luke!"

Note that the values returned by the gdbm_sync() function may change
as a result of enabling crash tolerance.  Applications unprepared for
the new return values might become confused.

Performance:

The purpose of a parachute is not to hasten descent.  Crash tolerance
is a safety mechanism, not a performance accelerator.  Reflink
copying is designed to be as efficient as possible, but making
snapshots of the GNU dbm database file on every gdbm_sync() call
entails overheads.  The performance impact of GDBM crash tolerance
will depend on many factors including the type and configuration of
the underlying storage system, how often the application calls
gdbm_sync(), and the extent of changes to the database file between
consecutive calls to gdbm_sync().

Availability:

To ensure that application data can survive the failure of one or
more storage devices, replicated storage (e.g., RAID) may be used
beneath the reflink-capable filesystem.  Some cloud providers offer
block storage services that mimic the interface of individual storage
devices but that are implemented as high-availability fault-tolerant
replicated distributed storage systems.  Installing a reflink-capable
filesystem atop a high-availability storage system is a good starting
point for a high-availability crash-tolerant GDBM.

