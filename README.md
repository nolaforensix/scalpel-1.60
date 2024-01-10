Note: This is the source code for Scalpel 1.60, a fast
header/footer-based file carver.  Version 1.60 is the most widely used
public release, but if you are still using 1.60, you should really be
using 2.02, which has more features and is much faster. That repo can
be found at https://github.com/nolaforensix/scalpel-2.02.

Neither this release nor 2.02 are being actively developed and if
you're simply doing file carving to retrieve deleted files of common
types, you probably should be using photorec instead, since photorec
has moved beyond the simplistic header/footer-based approach used by
Scalpel 1.60 and 2.02.  On the other hand, if you are developing
patterns to recover esoteric file types, you may find that Scalpel is
still useful.

There *are* some changes in this release, compared to the 1.60 release
that's been in the wild for roughly the last 15 years.  Some minor
memory leaks that went unnoticed until very recently have been
corrected, mostly in code that was imported from other open source
projects when Scalpel was first developed.  Thanks to Karley
Waguespack for assisting with tracking these down.

scalpel3 is now under development and supports not only
header/footer-based file carving, but also much more sophisticated
recovery options and high-performance recovery of fragmented files.  A
public repo for scalpel3 will appear when it's ready.

Cheers,

--Golden

Original documentation in README follows.

-------------------------------------------------------------------------

Scalpel is a complete rewrite of the Foremost 0.69 file carver.  This
version of Scalpel reads Foremost 0.69 configuration files--see the
default configuration file, scalpel.conf, for more details.

Important note: The default configuration file has all supported file
patterns commented out--you must edit this before before running
Scalpel.

More details on execution options can be found in the Scalpel man
page, "scalpel.1".

Currently supported operating systems:  Linux, Windows, Mac OS X.

If you decide to compile Scalpel on win32, you'll need to install
pthreads-win32 and hack the Makefile to reflect where you've installed
the pthreads include and lib directories.  If you want to run Scalpel
on win32 w/o these hassles, just use the win32 executable that's
provided in the distribution.


COMPILE INSTRUCTIONS:

Linux:    make

Win32:    make win32 [or mingw32-make win32]

Mac OS X: make bsd

and enjoy.  If you want to install the binary and man page in a more
permanent place, just copy "scalpel" and "scalpel.1" to appropriate
locations, e.g., on Linux,  "/usr/local/bin" and "/usr/local/man/man1", 
respectively.  On Windows, you'll also need to copy "pthreadGC1.dll"
into the same directory as "scalpel.exe".


LIMITATIONS:

Carving Windows physical and logical device files (e.g.,
\\.\physicaldrive0 or \\.\c:) isn't currently supported, but will be supported
in a future release.  


SUGGESTIONS:

Bug reports, comments, complaints, and feature requests should be
directed to the author at goldenrichard@gmail.com.

Cheers,

--Golden
