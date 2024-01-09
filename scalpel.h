// Scalpel Copyright (C) 2005-6 by Golden G. Richard III.
// Written by Golden G. Richard III.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301, USA.

// Scalpel is a complete rewrite of the foremost 0.69 file carver to
// increase speed and support execution on machines with minimal
// resources (e.g., < 256MB RAM).
//
// Thanks to Kris Kendall, Jesse Kornblum, et al for their work on
// foremost.
//

#ifndef SCALPEL_H
#define SCALPEL_H
#define SCALPEL_VERSION    "1.60"

#define _USE_LARGEFILE              1
#define _USE_FILEOFFSET64           1
#define _USE_LARGEFILE64            1
#define _LARGEFILE_SOURCE           1
#define _LARGEFILE64_SOURCE         1
#define _FILE_OFFSET_BITS           64

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <dirent.h>
#include <stdarg.h>
#include <math.h>
#include "base_name.h"
#include "prioque.h"

//
// GGRIII: WARNING: Scalpel has NOT yet been thoroughly tested on OpenBSD, but is
// known to compile and work correctly on Mac OS X (a BSD variant).
//

#ifdef __OPENBSD
#define __UNIX
#include <sys/ttycom.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <libgen.h>
// off_t on Mac OS X is 64 bits
#define off64_t  off_t      
#endif  /* ifdef __OPENBSD */

#ifdef __LINUX
#define __UNIX
#include <linux/hdreg.h>
#include <libgen.h>
#include <error.h>
#endif  /* ifdef __LINUX */

#ifdef __WIN32
#include <windows.h>
#include <sys/timeb.h>
#define gettimeofday(A,B) QueryPerformanceCounter(A)
#define ftello   ftello64
#define fseeko   fseeko64
#define sleep    Sleep
#define  snprintf         _snprintf
// macros for the Windows equivalent UNIX functions.  (No worries
// about lstat to stat; Windows doesn't have symbolic links)
#define lstat(A,B)      stat(A,B)
char *basename(char *path);
extern char *optarg;
extern int optind;
int getopt(int argc, char *const argv[], const char *optstring);

#ifdef __MINGW32__
#define realpath(A,B)    _fullpath(B,A,PATH_MAX)
#endif

#ifdef __CYGWIN32__
#define realpath(A,B) \
  (getenv ("CYGWIN_USE_WIN32_PATHS") \
   ? (cygwin_conv_to_full_win32_path ((A), (B)), B) \
   : realpath ((A), (B)))
#endif
#endif    /* ifdef __WIN32 */

#ifndef __GLIBC__
void setProgramName(char *s);
#endif  /* ifdef __GLIBC__ */

#ifndef __WIN32
#include <sys/mount.h>
#endif 


#define TRUE   1
#define FALSE  0

#define SEARCHTYPE_FORWARD      0
#define SEARCHTYPE_REVERSE      1
#define SEARCHTYPE_FORWARD_NEXT 2

// GGRIII: If you like having hair (on your head, not in clumps, in your
// hands), then the following parens are pretty important

#define KILOBYTE                  1024
#define MEGABYTE                  (1024 * KILOBYTE)
#define GIGABYTE                  (1024 * MEGABYTE)
#define TERABYTE                  (1024 * GIGABYTE)
#define PETABYTE                  (1024 * TERABYTE)
#define EXABYTE                   (1024 * PETABYTE)

// SIZE_OF_BUFFER indicates how much data to read from an image file
// at a time. With the elimination of "quick" mode in Scalpel, the
// restrictions on divisibility by block size are irrelevant, but they
// don't hurt anything.
#define SIZE_OF_BUFFER            (10 * MEGABYTE)

#define SCALPEL_SIZEOFBUFFER_PANIC_STRING \
"PANIC: SIZE_OF_BUFFER has been incorrectly configured.\n"


#define SCALPEL_BLOCK_SIZE           512
#define MAX_STRING_LENGTH            4096
#define MAX_NEEDLES                   254
#define NUM_SEARCH_SPEC_ELEMENTS        6
#define MAX_SUFFIX_LENGTH               8
#define MAX_FILE_TYPES                100

#define MAX_FILES_PER_SUBDIRECTORY    1000


#define SCALPEL_OK                     0
#define SCALPEL_ERROR_NO_SEARCH_SPEC   1
#define SCALPEL_ERROR_FILE_OPEN        2
#define SCALPEL_ERROR_FILE_READ        3
#define SCALPEL_ERROR_FILE_WRITE       4
#define SCALPEL_ERROR_FILE_CLOSE       5
#define SCALPEL_ERROR_TOO_MANY_TYPES   6
#define SCALPEL_ERROR_FATAL_READ       7
#define SCALPEL_GENERAL_ABORT        999

#define UNITS_BYTES                     0
#define UNITS_KILOB                     1
#define UNITS_MEGAB                     2
#define UNITS_GIGAB                     3
#define UNITS_TERAB                     4
#define UNITS_PETAB                     5
#define UNITS_EXAB                      6

// GLOBALS

// signal has been caught by signal handler
extern int signal_caught; 

// current wildcard character
extern char wildcard;

// width of tty, for progress bar
extern int ttywidth;

extern char *__progname;
extern int  errno;


#define SCALPEL_NOEXTENSION_SUFFIX "NONE"
#define SCALPEL_NOEXTENSION '\xFF'

#define SCALPEL_DEFAULT_WILDCARD       '?'
#define SCALPEL_DEFAULT_CONFIG_FILE    "scalpel.conf"

#define SCALPEL_DEFAULT_OUTPUT_DIR     "scalpel-output"

#define SCALPEL_BANNER_STRING \
"Scalpel version %s\n"\
"Written by Golden G. Richard III, based on Foremost 0.69.\n", SCALPEL_VERSION

#define SCALPEL_COPYRIGHT_STRING \
"Scalpel is (c) 2005-6 by Golden G. Richard III.\nThis program is based on Foremost 0.69, by Kris Kendall and Jesse Kornblum."

#define NONEMPTYDIR_ERROR_MSG  \
"ERROR: You have attempted to use a non-empty output directory. In order\n"\
"       to maintain forensic soundness, this is not allowed.\n"

// During the file carving operations (which occur after an initial
// scan of an image file to build the header/footer database), we want
// to read the image file only once more, sequentially, for all
// carves.  The following structure tracks the filename and first/last
// bytes in the image file for a single file to be carved.  When the
// read buffer includes the first byte of a file, the file is opened
// and the first write occurs.  When the read buffer includes the end
// byte, the last write operation occurs, the file is closed, and the
// struct can be reused.

// *****GGRIII: use of priority field to store these flags and the 
// data structures which track CarveInfo structs needs to be better
// documented

#define STARTCARVE      1       // carve operation for this CarveInfo struct
                                // starts in current buffer
#define STOPCARVE       2       // carve operation stops in current buffer
#define STARTSTOPCARVE  3       // carve operation both starts and stops in
                                // current buffer
#define CONTINUECARVE   4       // carve operation includes entire contents
                                // of current buffer

typedef struct CarveInfo {
  char *filename;            // output filename for file to carve
  FILE *fp;                  // file descriptor for file to carve
  unsigned long long start;  // offset of first byte in file
  unsigned long long stop;   // offset of last byte in file
  char chopped;              // is carved file's length constrained
                             // by max file size for type? (i.e., could
                             // the file actually be longer?
} CarveInfo;


// Each struct SearchSpecLine defines a particular file type,
// including header and footer information.  The following structure,
// SearchSpecOffsets, defines the absolute locations of all matching
// headers and footers for a particular file type.  Because the entire
// header/footer database is built during a single pass over an image
// or device file, the header and footer locations are sorted in
// ascending order.

typedef struct SearchSpecOffsets {
  unsigned long long *headers;                 // positions of discovered headers
  unsigned long long headerstorage;            // space allocated for this many header offsets
  unsigned long long numheaders;               // # stored header positions
  unsigned long long *footers;                 // positions of discovered headers
  unsigned long long footerstorage;            // space allocated for this many footer offsets
  unsigned long long numfooters;               // # stored footer positions
} SearchSpecOffsets;

// max files to open at once during carving--modify if you get
// a "too many files open" error message during the second carving phase.
#ifdef __WIN32
#define MAX_FILES_TO_OPEN            20
#else
#define MAX_FILES_TO_OPEN            512
#endif


typedef struct SearchSpecLine {
  char *suffix;
  int casesensitive;
  unsigned long long length;     
  char *begin;
  int beginlength;
  size_t begin_bm_table[UCHAR_MAX+1];
  char *end;
  int endlength;
  size_t end_bm_table[UCHAR_MAX+1];
  int searchtype; // FORWARD, NEXT, REVERSE search type for footer
  struct SearchSpecOffsets offsets;
  unsigned long long numfilestocarve;      // # files to carve of this type
  unsigned long organizeDirNum;            // subdirectory # for organization 
                                           // of files of this type
} SearchSpecLine;


typedef struct scalpelState {
  char *imagefile; 
  char *conffile; 
  char *outputdirectory;
  int specLines;
  struct SearchSpecLine* SearchSpec;
  unsigned long long fileswritten;
  int modeVerbose;
  int modeNoSuffix;
  FILE *auditFile;
  char *invocation;
  unsigned long long skip;
  char *coveragedirectory;
  unsigned int coverageblocksize;
  FILE *coverageblockmap;
  unsigned char *coveragebitmap;
  unsigned long long coveragenumblocks;
  int useInputFileList;
  char *inputFileList; 
  int carveWithMissingFooters;
  int noSearchOverlap;
  int ignoreEmbedded;
  int generateHeaderFooterDatabase;
  int updateCoverageBlockmap;
  int useCoverageBlockmap;
  int organizeSubdirectories;
  unsigned long long organizeMaxFilesPerSub;
  int blockAlignedOnly;
  unsigned int alignedblocksize;
  int previewMode;
} scalpelState;


// one extent for a fragmented file.  'start' and 'stop'
// are real disk image addresses that define the fragment's
// location.

typedef struct Fragment {
  unsigned long long start;
  unsigned long long stop;
} Fragment;

  
// prototypes for visible dig.c functions
int digImageFile(struct scalpelState *state);
int carveImageFile(struct scalpelState *state);


// prototypes for visible helpers.c functions
void checkMemoryAllocation(struct scalpelState *state, void *ptr, int line, 
			   char *file, char *structure);
int skipInFile(struct scalpelState *state, FILE *infile);
void scalpelLog(struct scalpelState *state, char *format, ...);
void handleError(struct scalpelState *s, int error);
int memwildcardcmp(const void* s1, const void* s2, 
		   size_t n, int caseSensitive);

void init_bm_table(char *needle, size_t table[UCHAR_MAX + 1], 
		   size_t len, int casesensitive);
int findLongestNeedle(struct SearchSpecLine* SearchSpec);

char *bm_needleinhaystack(char *needle, size_t needle_len,
                          char *haystack, size_t haystack_len,
                          size_t table[UCHAR_MAX + 1], int casesensitive);
int translate(char *str);
char *skipWhiteSpace(char *str);
void setttywidth();

// prototypes for visible files.c functions
unsigned long long measureOpenFile(FILE *f, struct scalpelState *state);
int openAuditFile(struct scalpelState* state);
int closeFile(FILE* f);


// WIN32 string.h wierdness
#ifdef __WIN32
extern const char *strsignal(int sig);
#else
extern char *strsignal(int sig);
#endif  /*  ifdef __WIN32 */

#endif   /* ifndef SCALPEL_H */

