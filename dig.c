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

#include "scalpel.h"

/////////// GLOBALS ////////////
static char readbuffer[SIZE_OF_BUFFER];     // read buffer--process image
                                            // files in SIZE_OF_BUFFER-sized
                                            // chunks

// prototypes for private dig.c functions
static int writeHeaderFooterDatabase(struct scalpelState *state);
static int setupCoverageMaps(struct scalpelState *state, unsigned long long filesize);
static int auditUpdateCoverageBlockmap(struct scalpelState *state, struct CarveInfo *carve);
static int updateCoverageBlockmap(struct scalpelState *state, unsigned long long block);
static void generateFragments(struct scalpelState *state, Queue *fragments, struct CarveInfo *carve);
static unsigned long long positionUseCoverageBlockmap(struct scalpelState *state, unsigned long long position);
static void destroyCoverageMaps(struct scalpelState *state);
static int fseeko_use_coverage_map(struct scalpelState *state, FILE *fp, off64_t offset);
static off64_t ftello_use_coverage_map(struct scalpelState *state, FILE *fp);
static size_t fread_use_coverage_map(struct scalpelState *state, void *ptr, 
				    size_t size, size_t nmemb, FILE *stream);
static void printhex(char *s, int len);
static void clean_up(struct scalpelState* state, int signum);
static int displayPosition(int *units,
		    unsigned long long pos,
		    unsigned long long size, 
		    char *fn);
static void setupAuditFile(struct scalpelState* state);
static int bm_digBuffer(struct scalpelState *state, FILE *infile, 
		 unsigned long long lengthofbuf, 
		 unsigned long long offset);
//static void adjustForEmbedding(struct SearchSpecLine *currentneedle, 
//			       unsigned long long headerindex, unsigned long long *prevstopindex);



// output hex notation for chars in 's'
static void printhex(char *s, int len) {

  int i;
  for (i=0; i < len; i++) {
    printf("\\x%.2x", (unsigned char)s[i]);
  }
}

static void clean_up(struct scalpelState* state, int signum) {

  scalpelLog (state,"Cleaning up...\n");
  scalpelLog (state,
               "\nCaught signal: %s. Program is terminating early\n",
               (char*) strsignal(signum));
  if (closeFile(state->auditFile)) {
    scalpelLog(state,"Error closing %s/audit.txt -- %s",
                state->outputdirectory,
                (char*) strerror(ferror(state->auditFile)));
  }
  exit(1);
}


// display progress bar
static int displayPosition(int *units,
		    unsigned long long pos,
		    unsigned long long size, 
		    char *fn) {
  
  double percentDone = (((double)pos)/(double)(size) * 100);
  double position = (double) pos;
  int count;
  int barlength,i,len;
  double elapsed;
  long remaining;

  char buf[MAX_STRING_LENGTH];
  char line[MAX_STRING_LENGTH];

#ifdef __WIN32
  static LARGE_INTEGER start;
  LARGE_INTEGER now;
  static LARGE_INTEGER freq;
  QueryPerformanceFrequency(&freq);
#else
  static struct timeval start;
  struct timeval now, td;
#endif

  // get current time and remember start time when first chunk of 
  // an image file is read

  if (pos <= SIZE_OF_BUFFER) { 
    gettimeofday(&start, (struct timezone *)0);
  }
  gettimeofday(&now, (struct timezone *)0);

  // First, reduce the position to the right units 
  for (count = 0; count < *units; count++) {
    position = position / 1024;
  }
  
  // Now check if we've hit the next type of units 
  while (position > 1023) {
    position = position / 1024;
    (*units)++;
  }
		       
  switch (*units) {

  case UNITS_BYTES:
    sprintf(buf,"bytes");    break;
  case UNITS_KILOB:
    sprintf(buf,"KB");       break;
  case UNITS_MEGAB:
    sprintf(buf,"MB");       break;
  case UNITS_GIGAB:
    sprintf(buf,"GB");       break;
  case UNITS_TERAB:
    sprintf(buf,"TB");       break;
  case UNITS_PETAB:
    sprintf(buf,"PB");       break;
  case UNITS_EXAB:
    sprintf(buf,"EB");       break;

  default:
    fprintf (stdout, "Unable to compute progress.\n");
    return SCALPEL_OK;
  }

  len = 0;
  len += snprintf(line+len,sizeof(line)-len,"\r%s: %5.1f%% ",fn, percentDone);
  barlength = ttywidth - strlen(fn) - strlen(buf) - 32;
  if (barlength > 0) {
    i = barlength * (int) percentDone / 100;
    len += snprintf(line+len, sizeof(line)-len,
    		    "|%.*s%*s|", i,
    "****************************************************************************************************************************************************************",
		    barlength-i, "");
  }
    
  len += snprintf(line+len,sizeof(line)-len," %6.1f %s",position,buf);

#ifdef __WIN32
  elapsed = ((double)now.QuadPart - (double)start.QuadPart)/((double)freq.QuadPart);
  //printf("elapsed: %f\n",elapsed);
#else
  timersub(&now, &start, &td);
  elapsed = td.tv_sec + (td.tv_usec / 1000000.0);
#endif
  remaining = (100-percentDone)/percentDone*elapsed;
  //printf("Ratio remaining: %f\n",(100-percentDone)/percentDone);
  //printf("Elapsed time: %f\n",elapsed);
  if(remaining >= 100*(60*60)){  //60*60 is seconds per hour
    len +=snprintf(line+len, sizeof(line)-len," --:--ETA");
  }else{
    i = remaining / (60*60);
    if(i)
      len += snprintf(line+len,sizeof(line)-len," %2d:",i);
    else
      len += snprintf(line+len,sizeof(line)-len,"    ");
    i = remaining%(60*60);
    len += snprintf(line+len,sizeof(line)-len,"%02d:%02d ETA",i/60, i%60);
  }

  fprintf(stdout,"%s",line);
  fflush(stdout);

  return SCALPEL_OK;
}

// create initial entries in audit for each image file processed
static void setupAuditFile(struct scalpelState* state) {
  
  char imageFile[MAX_STRING_LENGTH];  

  realpath(state->imagefile,imageFile);
  
  scalpelLog(state,"\nOpening target \"%s\"\n\n", imageFile);

#ifdef __WIN32
  if (state->skip) {
    fprintf(state->auditFile,"Skipped the first %I64u bytes of %s...\n",
	    state->skip, state->imagefile);
    if (state->modeVerbose) {
      fprintf(stdout,"Skipped the first %I64u bytes of %s...\n",
	      state->skip, state->imagefile);
    }
  }
#else
  if (state->skip) {
    fprintf(state->auditFile,"Skipped the first %llu bytes of %s...\n",
	    state->skip, state->imagefile);
    if (state->modeVerbose) {
      fprintf(stdout,"Skipped the first %llu bytes of %s...\n",
	      state->skip, state->imagefile);
    }
  }
#endif

  fprintf(state->auditFile,"The following files were carved:\n");
  fprintf(state->auditFile,
	  "File\t\t  Start\t\t\tChop\t\tLength\t\tExtracted From\n");
}


// add entries to header/footer database during search of current
// buffer.

static int bm_digBuffer(struct scalpelState *state, FILE *infile, 
		 unsigned long long lengthofbuf, 
		 unsigned long long offset) {
  
  unsigned long long startLocation = 0;
  int needlenum;
  char *foundat;
  struct SearchSpecLine *currentneedle;
    
  // for each file type, find all headers and some (or all) footers
  for (needlenum=0; 
       state->SearchSpec[needlenum].suffix != NULL; 
       needlenum++) {
    
    currentneedle = &(state->SearchSpec[needlenum]);
    
    // header search first
    
    foundat = readbuffer;
    while (foundat) {
      // signal check
      if (signal_caught == SIGTERM || signal_caught == SIGINT){
	clean_up(state,signal_caught);
      }

      foundat = bm_needleinhaystack(currentneedle->begin, 
				    currentneedle->beginlength,
				    foundat,
				    (int)(lengthofbuf-(foundat-readbuffer)),
				    currentneedle->begin_bm_table,
				    currentneedle->casesensitive);

      startLocation = offset + (foundat-readbuffer);	
      if (foundat > 0) {
	// GGRIII: found a header--record location in header offsets
	// database...
	
	if (state->modeVerbose) {
#ifdef __WIN32
	  fprintf(stdout, "A %s header was found at : %I64u\n",
		  currentneedle->suffix, positionUseCoverageBlockmap(state, startLocation));
#else
	  fprintf(stdout, "A %s header was found at : %llu\n",
		   currentneedle->suffix, positionUseCoverageBlockmap(state, startLocation));
#endif
	}

	currentneedle->offsets.numheaders++;
	if (currentneedle->offsets.headerstorage <=
	    currentneedle->offsets.numheaders) {
	  // need more memory for header offset storage--add an
	  // additional 100 elements
	  currentneedle->offsets.headers = 
	    realloc(currentneedle->offsets.headers, 
		    sizeof(unsigned long long) * 
		    (currentneedle->offsets.numheaders + 100));
	  checkMemoryAllocation(state, currentneedle->offsets.headers, __LINE__, __FILE__, "header array");
	  currentneedle->offsets.headerstorage = 
	    currentneedle->offsets.numheaders + 100;

	  if (state->modeVerbose) {
#ifdef __WIN32
	    fprintf(stdout, "Memory reallocation performed, total header storage = %I64u\n",
		    currentneedle->offsets.headerstorage);
#else
	    fprintf(stdout, "Memory reallocation performed, total header storage = %llu\n",
		    currentneedle->offsets.headerstorage);
#endif
	  }

	}
	currentneedle->offsets.headers[currentneedle->offsets.numheaders-1] = 
	  startLocation;
      }

      if (foundat) {
	// move past match position.  Foremost 0.69 didn't find overlapping
	// headers/footers.  If you need that behavior, specify "-r" on the
	// command line.  Scalpel's default behavior is to find overlapping
	// headers/footers.
	
	if (state->noSearchOverlap) {
	  foundat = foundat + currentneedle->beginlength;
	}
	else {
	  foundat++;
	}
      }
    }

    // now footer search, if:
    //
    // there's a footer for this file type and
    // at least one header for that type has been previously seen and 
    // at least one header is viable--that is, it was found in the current
    // buffer, or it's less than the max carve distance behind the current
    // file offset
    //
    // OR
    // 
    // a header/footer database is being created.  In this case, ALL headers and
    // footers must be discovered.

    if (
	// regular case--want to search for only "viable" (in the sense that they are
	// useful for carving unfragmented files) footers, to save time
	(currentneedle->offsets.numheaders > 0 &&
	currentneedle->endlength &&
	(currentneedle->offsets.headers[currentneedle->offsets.numheaders-1] > offset ||
	 (offset - currentneedle->offsets.headers[currentneedle->offsets.numheaders-1] < currentneedle->length)))

	||
	
	// generating header/footer database, need to find all footers
	(currentneedle->endlength && state->generateHeaderFooterDatabase)) {

      foundat = readbuffer;
      while (foundat) {
	
	// signal check
	if (signal_caught == SIGTERM || signal_caught == SIGINT){
	  clean_up(state,signal_caught);
	}
	
	// GGRIII: look for footer
	foundat = bm_needleinhaystack(currentneedle->end, 
				      currentneedle->endlength,
				      foundat,
				      (int)(lengthofbuf-(foundat-readbuffer)),
				      currentneedle->end_bm_table,
				      currentneedle->casesensitive);
	
	if (foundat > 0) {
	  // GGRIII: found a footer--record location in footer offsets
	  // database...
	  
	  startLocation = offset + (foundat-readbuffer);	
	  
	  if (state->modeVerbose) {
#ifdef __WIN32
	    fprintf(stdout, "A %s footer was found at : %I64u\n",
		     currentneedle->suffix, positionUseCoverageBlockmap(state, startLocation));
#else
	    fprintf(stdout, "A %s footer was found at : %llu\n",
		     currentneedle->suffix, positionUseCoverageBlockmap(state, startLocation));
#endif	   
	  }
	  
	  currentneedle->offsets.numfooters++;
	  if (currentneedle->offsets.footerstorage <= 
	      currentneedle->offsets.numfooters) {
	    // need more memory for footer offset storage--add an
	    // additional 100 elements
	    currentneedle->offsets.footers = 
	      realloc(currentneedle->offsets.footers, 
		      sizeof(unsigned long long) * 
		      (currentneedle->offsets.numfooters + 100));
	    checkMemoryAllocation(state, currentneedle->offsets.footers, __LINE__, __FILE__, "footer array");
	    currentneedle->offsets.footerstorage = 
	      currentneedle->offsets.numfooters + 100;

	    if (state->modeVerbose) {
#ifdef __WIN32
	      fprintf(stdout, "Memory reallocation performed, total footer storage = %I64u\n",
		      currentneedle->offsets.footerstorage);
#else
	      fprintf(stdout, "Memory reallocation performed, total footer storage = %llu\n",
		      currentneedle->offsets.footerstorage);
#endif
	    }

	  }
	  currentneedle->offsets.footers[currentneedle->offsets.numfooters-1] = 
	    startLocation;
	  
	  // move past match position.  Foremost 0.69 didn't find overlapping
	  // headers/footers.  If you need that behavior, specify "-r" on the
	  // command line.  Scalpel's default behavior is to find overlapping
	  // headers/footers.
	  if (state->noSearchOverlap) {
	    foundat = foundat + currentneedle->endlength;
	  }
	  else {
	    foundat++;
	  }
	}
      }
    }
  }

  return SCALPEL_OK;
}


// GGRIII: Scalpel's approach dictates that this function dig an image
// file, building the header/footer offset database.  The task of
// extracting files from the image has been moved to carveImageFile(),
// which operates in a second pass over the image.  Digging for
// header/footer values proceeds in SIZE_OF_BUFFER sized chunks of the
// image file.  This buffer is now global and named "readbuffer".
int digImageFile(struct scalpelState* state) {
  
  FILE *infile;
  unsigned long long filesize = 0, bytesread = 0, 
    fileposition = 0, filebegin = 0, beginreadpos = 0;
  long err=0;
  int status, displayUnits = UNITS_BYTES;
  int success=0;
  int longestneedle;
  setupAuditFile(state);
  
  if (state->SearchSpec[0].suffix == NULL) {
    return SCALPEL_ERROR_NO_SEARCH_SPEC;
  }

  // GGRIII: Scalpel eliminates the large buffer in foremost 0.69 whose
  // size was governed by the variable maxchar [which has been
  // removed].  This allows scalpel to run with a memory footprint
  // less than 1/10 of the size of foremost, for typical
  // "foremost.conf" values.  Still need to know the longest needle,
  // so edge conditions on the buffer can be dealt with.

  longestneedle = findLongestNeedle(state->SearchSpec);
    
  // open current image file
  if ((infile = fopen(state->imagefile,"rb")) == NULL) {
    fprintf(stderr, "ERROR: Couldn't open input file: %s -- %s\n", 
	    (*(state->imagefile)=='\0')?"<blank>":state->imagefile,
	    strerror(errno));
    return SCALPEL_ERROR_FILE_OPEN;
  }

#ifdef __WIN32
  // set binary mode for Win32
  setmode(fileno(infile),O_BINARY);
#endif
#ifdef __LINUX
  fcntl(fileno(infile),F_SETFL, O_LARGEFILE);
#endif
  
  // skip initial portion of input file, if that cmd line option
  // was set
  if(state->skip > 0){
    if (!skipInFile(state,infile)) {
      return SCALPEL_ERROR_FILE_READ;
    }

    // ***GGRIII: want to update coverage bitmap when skip is specified????
    // ***GGRIII: want to update coverage bitmap when skip is specified????

  }

  filebegin = ftello(infile);
  if ((filesize = measureOpenFile(infile, state)) == -1) {
    fprintf (stderr,
	     "ERROR: Couldn't measure size of image file %s\n", 
	     state->imagefile);
    return SCALPEL_ERROR_FILE_READ;
  }
  
#ifdef __WIN32
  if (state->modeVerbose) {
    fprintf (stdout, "Total file size is %I64u bytes\n", filesize);
  }
#else
  if (state->modeVerbose) {
    fprintf (stdout, "Total file size is %llu bytes\n", filesize);
  }
#endif


  // allocate and initialize coverage bitmap and blockmap, if appropriate
  if ((err = setupCoverageMaps(state, filesize)) != SCALPEL_OK) {
    return err;
  }
  
  // GGRIII: process SIZE_OF_BUFFER-sized chunks of the current image
  // file and look for both headers and footers, recording their
  // offsets for use in the 2nd scalpel phase, when file data will 
  // be extracted.

  fprintf(stdout, "Image file pass 1/2.\n");
  success=1;
  while ((bytesread = 
	  fread_use_coverage_map(state, readbuffer,
				 1,
				 SIZE_OF_BUFFER, infile)) > longestneedle-1 || success==0) {

    if (state->modeVerbose) {
#ifdef __WIN32
      fprintf(stdout, "Read %I64u bytes from image file.\n", bytesread);
#else
      fprintf(stdout, "Read %llu bytes from image file.\n", bytesread);
#endif
    }

    if ((err = ferror(infile))) {
	return SCALPEL_ERROR_FILE_READ;      
    }
    success=1;
    
    // progress report needs a fileposition that doesn't depend on coverage map
    fileposition = ftello(infile);
    displayPosition(&displayUnits,fileposition-filebegin,
		    filesize,state->imagefile);

    // if carving is dependent on coverage map, need adjusted fileposition
    fileposition = ftello_use_coverage_map(state, infile);
    beginreadpos = fileposition - bytesread;
    
    //signal check
    if (signal_caught == SIGTERM || signal_caught == SIGINT)
      clean_up(state,signal_caught);
    
    // process current buffer
    if ((status = bm_digBuffer(state,infile,
			       bytesread,beginreadpos)) != SCALPEL_OK) {
      
      // GGRIII: error, just return status
      return status;
    }
    
    // move file position back a bit so headers and footers that fall
    // across SIZE_OF_BUFFER boundaries in the image file aren't
    // missed

    fseeko_use_coverage_map(state, infile, -1 * (longestneedle-1));
  }
  
  closeFile(infile);
  
  return SCALPEL_OK;
}

// GGRIII: carveImageFile() uses the header/footer offsets database
// created by digImageFile() to build a list of files to carve.  These
// files are then carved during a single, sequential pass over the
// image file.  The global 'readbuffer' is used as a buffer in this
// function.

int carveImageFile(struct scalpelState* state) {

  FILE *infile;
  struct SearchSpecLine *currentneedle;
  struct CarveInfo *carveinfo;
  char fn[MAX_STRING_LENGTH];        // temp buffer for output filename
  char orgdir[MAX_STRING_LENGTH];    // buffer for name of organizing subdirectory
  unsigned long long start, stop;    // temp begin/end bytes for file to carve
  unsigned long long prevstopindex;  // tracks index of first 'reasonable' 
                                     // footer
  int needlenum;
  unsigned long long filesize = 0, bytesread = 0, 
    fileposition = 0, filebegin = 0; 
  long err=0;
  int displayUnits = UNITS_BYTES;
  int success=0;
  unsigned long long i,j;
  int halt;
  char chopped;                     // file chopped because it exceeds
                                    // max carve size for type?
  int CURRENTFILESOPEN=0;           // number of files open (during carve)

  // index of header and footer within image file, in SIZE_OF_BUFFER
  // blocks
  unsigned long long headerblockindex, footerblockindex;    

  struct Queue *carvelists;   // one entry for each SIZE_OF_BUFFER bytes of
                              // input file


  // open image file and get size so carvelists can be allocated
  if ((infile = fopen(state->imagefile,"rb")) == NULL) {
    fprintf(stderr, "ERROR: Couldn't open input file: %s -- %s\n", 
	    (*(state->imagefile)=='\0')?"<blank>":state->imagefile,
	    strerror(errno));
    return SCALPEL_ERROR_FILE_OPEN;
  }

#ifdef __WIN32
  // explicit binary option for Win32
   setmode(fileno(infile),O_BINARY);
#endif
#ifdef __LINUX
  fcntl(fileno(infile),F_SETFL, O_LARGEFILE);
#endif
  
  // If skip was activated, then there's no way headers/footers were
  // found there, so skip during the carve operations, too

  if (state->skip > 0){
    if (!skipInFile(state,infile)) {
      return SCALPEL_ERROR_FILE_READ;
    }
  }

  filebegin = ftello(infile);
  if ((filesize = measureOpenFile(infile, state)) == -1) {
    fprintf (stderr,
	     "ERROR: Couldn't measure size of image file %s\n", 
	     state->imagefile);
    return SCALPEL_ERROR_FILE_READ;
  }

  // allocate memory for carvelists--we alloc a queue for each
  // SIZE_OF_BUFFER bytes in advance because it's simpler and an empty
  // queue doesn't consume much memory, anyway.

  carvelists = malloc(sizeof(Queue) * (2 + ( filesize / SIZE_OF_BUFFER)));
  checkMemoryAllocation(state, carvelists, __LINE__, __FILE__, "carvelists");

  // queue associated with each buffer of data holds pointers to
  // CarveInfo structures.

  fprintf(stdout, "Allocating work queues...\n");

  for (i=0; i < 2 + (filesize / SIZE_OF_BUFFER); i++) {
    init_queue(&carvelists[i],
	       sizeof(struct CarveInfo *),
	       TRUE, 0);
  }
  fprintf(stdout, "Work queues allocation complete. Building carve lists...\n");

  // build carvelists before 2nd pass over image file
  
  for (needlenum=0; state->SearchSpec[needlenum].suffix != NULL; needlenum++) {

    currentneedle = &(state->SearchSpec[needlenum]);

    // handle each discovered header independently

    prevstopindex = 0;
    for (i=0; i < currentneedle->offsets.numheaders; i++) {
      start = currentneedle->offsets.headers[i];

      // block aligned test for "-q"

      if (state->blockAlignedOnly && start % state->alignedblocksize != 0) {
	continue;
      }

      stop=0;
      chopped=0;
    
      // case 1: no footer defined for this file type
      if (! currentneedle->endlength) {

	// this is the unfortunate case--if file type doesn't have a footer,
	// all we can done is carve a block between header position and
	// maximum carve size.
	stop = start + currentneedle->length - 1;
	// these are always considered chopped, because we don't really
	// know the actual size
	chopped=1;
      }
      else if (currentneedle->searchtype == SEARCHTYPE_FORWARD ||
	       currentneedle->searchtype == SEARCHTYPE_FORWARD_NEXT) {
	// footer defined: use FORWARD or FORWARD_NEXT semantics.
	// Stop at first occurrence of footer, but for FORWARD,
	// include the header in the carved file; for FORWARD_NEXT,
	// don't include footer in carved file.  For FORWARD_NEXT, if
	// no footer is found, then the maximum carve size for this
	// file type will be used and carving will proceed.  For
	// FORWARD, if no footer is found then no carving will be
	// performed unless -b was specified on the command line.

	halt=0;
	
	//	if (state->ignoreEmbedded) {
	//	  adjustForEmbedding(currentneedle, i, &prevstopindex);
	//	}

	for (j=prevstopindex; j < currentneedle->offsets.numfooters && 
	       ! halt; j++) {
	  if (currentneedle->offsets.footers[j] <= start) {
	    prevstopindex=j;
	  }
	  else {
	    halt=1;
	    stop = currentneedle->offsets.footers[j];

	    if (currentneedle->searchtype == SEARCHTYPE_FORWARD) {
	      // include footer in carved file
	      stop += currentneedle->endlength - 1;
	    }
	    else {
	      // FORWARD_NEXT--don't include footer in carved file
	      stop--;
	    }
	    // sanity check on size of potential file to carve--different
	    // actions depending on FORWARD or FORWARD_NEXT semantics
	    if (stop - start + 1 > currentneedle->length) {
	      if (currentneedle->searchtype == SEARCHTYPE_FORWARD) {
		// if the user specified -b, then foremost 0.69
		// compatibility is desired: carve this file even 
		// though the footer wasn't found and indicate
		// the file was chopped, in the log.  Otherwise, 
		// carve nothing and move on.
		if (state->carveWithMissingFooters) {
		  stop = start + currentneedle->length - 1;
		  chopped=1;
		}
		else {
		  stop=0;
		}
	      }
	      else {
		// footer found for FORWARD_NEXT, but distance exceeds
		// max carve size for this file type, so use max carve
		// size as stop
		stop = start + currentneedle->length - 1;
		chopped=1;
	      }
	    }
	  }
	}
	if (! halt && 
	    (currentneedle->searchtype == SEARCHTYPE_FORWARD_NEXT ||
	     (currentneedle->searchtype == SEARCHTYPE_FORWARD &&
	      state->carveWithMissingFooters))) {
	  // no footer found for SEARCHTYPE_FORWARD_NEXT, or no footer
	  // found for SEARCHTYPE_FORWARD and user specified -b, so just use
	  // max carve size for this file type as stop
	  stop = start + currentneedle->length - 1;
	}
      }
      else {
	// footer defined: use REVERSE semantics: want matching footer
	// as far away from header as possible, within maximum carving
	// size for this file type.  Don't bother to look at footers
	// that can't possibly match a header and remember this info
	// in prevstopindex, as the next headers will be even deeper
	// into the image file.  Footer is included in carved file for
	// this type of carve.
	halt=0;
	for (j=prevstopindex; j < currentneedle->offsets.numfooters && 
	       ! halt; j++) {
	  if (currentneedle->offsets.footers[j] <= start) {
	    prevstopindex=j;
	  }
	  else if (currentneedle->offsets.footers[j] - start <= 
		   currentneedle->length) {
	    stop = currentneedle->offsets.footers[j] 
	      + currentneedle->endlength - 1;
	  }
	  else {
	    halt=1;
	  }
	}
      }
      
      // if stop <> 0, then we have enough information to set up a
      // file carving operation
      if (stop) {
	// don't carve past end of image file...
	stop = stop > filesize ? filesize : stop;

	// find indices (in SIZE_OF_BUFFER units) of header and
	// footer, so the carveinfo can be placed into the right
	// queues.  The priority of each element in a queue allows the
	// appropriate thing to be done (e.g., STARTSTOPCARVE,
	// STARTCARVE, STOPCARVE, CONTINUECARVE).
	
	headerblockindex = start / SIZE_OF_BUFFER;
	footerblockindex = stop / SIZE_OF_BUFFER;

	// set up a struct CarveInfo for inclusion into the
	// appropriate carvelists

	// generate unique filename for file to carve

	if (state->organizeSubdirectories) {
	  snprintf(orgdir, MAX_STRING_LENGTH, "%s/%s-%d-%1lu", 
		   state->outputdirectory,
		   currentneedle->suffix,
		   needlenum,
		   currentneedle->organizeDirNum);
	  if (! state->previewMode) {
#ifdef __WIN32
	    mkdir(orgdir);
#else
	    mkdir(orgdir, 0777);
#endif
	  }
	}
	else {
	  snprintf(orgdir, MAX_STRING_LENGTH, "%s", state->outputdirectory);
	}

	if (state->modeNoSuffix || currentneedle->suffix[0] == 
	    SCALPEL_NOEXTENSION) {
#ifdef __WIN32
	  snprintf(fn,MAX_STRING_LENGTH,"%s/%08I64u",
		   orgdir,
		   state->fileswritten);
#else
	  snprintf(fn,MAX_STRING_LENGTH,"%s/%08llu",
		   orgdir,
		   state->fileswritten);
#endif

	}
	else {
#ifdef __WIN32
	  snprintf(fn,MAX_STRING_LENGTH,"%s/%08I64u.%s",
		   orgdir,
		   state->fileswritten,
		   currentneedle->suffix);
#else
	  snprintf(fn,MAX_STRING_LENGTH,"%s/%08llu.%s",
		   orgdir,
		   state->fileswritten,
		   currentneedle->suffix);
#endif
	}
	state->fileswritten++;     
	currentneedle->numfilestocarve++;
	if (currentneedle->numfilestocarve % state->organizeMaxFilesPerSub == 0) {
	  currentneedle->organizeDirNum++;
	}

	carveinfo = malloc(sizeof(struct CarveInfo));
	checkMemoryAllocation(state, carveinfo, __LINE__, __FILE__, "carveinfo");

	// remember filename
	carveinfo->filename=malloc(strlen(fn)+1);
	checkMemoryAllocation(state, carveinfo->filename, __LINE__, __FILE__, "carveinfo");
	strcpy(carveinfo->filename, fn);
	carveinfo->start = start;
	carveinfo->stop = stop;
	carveinfo->chopped = chopped;

	// fp will be allocated when the first byte of the file is
	// in the current buffer and cleaned up when we encounter the
	// last byte of the file.
	carveinfo->fp = 0;      

	if (headerblockindex == footerblockindex) {
	  // header and footer will both appear in the same buffer
	  add_to_queue(&carvelists[headerblockindex], 
		       &carveinfo, STARTSTOPCARVE);
	}
	else {
	  // header/footer will appear in different buffers, add carveinfo to 
	  // stop and start lists...
	  add_to_queue(&carvelists[headerblockindex], &carveinfo, STARTCARVE);
	  add_to_queue(&carvelists[footerblockindex], &carveinfo, STOPCARVE);
	  // .. and to all lists in between (these will result in a full
	  // SIZE_OF_BUFFER bytes being carved into the file).  
	  for (j=headerblockindex+1; j < footerblockindex; j++) {
	    add_to_queue(&carvelists[j], &carveinfo, CONTINUECARVE);
	  }
	}
      }
    }
  }
  
  fprintf(stdout, "Carve lists built.  Workload:\n");
  for (needlenum=0; state->SearchSpec[needlenum].suffix != NULL; needlenum++) {
    currentneedle = &(state->SearchSpec[needlenum]);
    fprintf(stdout, "%s with header \"",
	    currentneedle->suffix);
    printhex(currentneedle->begin, currentneedle->beginlength);
    fprintf(stdout,"\" and footer \"");
    if (currentneedle->end == 0) {
      fprintf(stdout,"NONE"); 
    }
    else {
      printhex(currentneedle->end, currentneedle->endlength);
    }
#ifdef __WIN32
    fprintf(stdout,"\" --> %I64u files\n", currentneedle->numfilestocarve);
#else
    fprintf(stdout,"\" --> %llu files\n", currentneedle->numfilestocarve);
#endif

  }
  
  if (state->previewMode) {
    fprintf(stdout, "** PREVIEW MODE: GENERATING AUDIT LOG ONLY **\n");
    fprintf(stdout, "** NO CARVED FILES WILL BE WRITTEN **\n");
  }

  fprintf(stdout, "Carving files from image.\n");
  fprintf(stdout, "Image file pass 2/2.\n");

  // now read image file in SIZE_OF_BUFFER-sized windows, writing
  // carved files to output directory

  success=1;
  while (success) {

    unsigned long long biglseek=0L;
    // goal: skip reading buffers for which there is no work to do by using one big
    // seek
    fileposition = ftello_use_coverage_map(state, infile);
    
    while (queue_length(&carvelists[fileposition / SIZE_OF_BUFFER]) == 0
	   && success) {
      biglseek += SIZE_OF_BUFFER;
      fileposition += SIZE_OF_BUFFER;
      success = fileposition <= filesize;
      
    }

    if (success && biglseek) {
      fseeko_use_coverage_map(state, infile, biglseek);
    }
    
    if (! success) { 
      // not an error--just means we've exhausted the image file--show
      // progress report then quit carving
      displayPosition(&displayUnits,filesize, 
		      filesize,state->imagefile);

      continue;
    }

    if (! state->previewMode) {
      bytesread = fread_use_coverage_map(state,readbuffer,1,SIZE_OF_BUFFER, infile);
      // Check for read errors
      if ((err = ferror(infile))) {
	return SCALPEL_ERROR_FILE_READ;      
      }
      else if (bytesread == 0) {   
	// no error, but image file exhausted
	success=0;
	continue;
      }
    }
    else {
      // in preview mode, seeks are used in the 2nd pass instead of
      // reads.  This isn't optimal, but it's fast enough and avoids
      // complicating the file carving code further.

      fileposition = ftello_use_coverage_map(state, infile);
      fseeko_use_coverage_map(state, infile, SIZE_OF_BUFFER);
      bytesread = ftello_use_coverage_map(state, infile) - fileposition;

      // Check for errors
      if ((err = ferror(infile))) {
	return SCALPEL_ERROR_FILE_READ;      
      }
      else if (bytesread == 0) {   
	// no error, but image file exhausted
	success=0;
	continue;
      }
    }

    success=1;

    // progress report needs real file position
    fileposition = ftello(infile);
    displayPosition(&displayUnits,fileposition-filebegin,
		    filesize,state->imagefile);

    // if using coverage map for carving, need adjusted file position
    fileposition = ftello_use_coverage_map(state, infile);
    
    // signal check
    if (signal_caught == SIGTERM || signal_caught == SIGINT) {
      clean_up(state,signal_caught);
    }

    // deal with work for this SIZE_OF_BUFFER-sized block by
    // examining the associated queue
    rewind_queue(&carvelists[(fileposition-bytesread) / SIZE_OF_BUFFER]);

    while (! end_of_queue(&carvelists[(fileposition-bytesread) / SIZE_OF_BUFFER])) {
      struct CarveInfo *carve;
      int operation;
      unsigned long long bytestowrite=0, byteswritten=0, offset=0;

      peek_at_current(&carvelists[(fileposition-bytesread) / SIZE_OF_BUFFER], 
		      &carve);
      operation = 
	current_priority(&carvelists[(fileposition-bytesread)/SIZE_OF_BUFFER]);

      // open file, if beginning of carve operation or file had to be closed
      // previously due to resource limitations
      if (operation == STARTSTOPCARVE || 
	  operation == STARTCARVE || carve->fp == 0) {

	if (! state->previewMode && state->modeVerbose) {
	  fprintf(stdout, "OPENING %s\n", carve->filename);
	}

	carve->fp=(FILE *)1;
	if (! state->previewMode) {
	  carve->fp = fopen(carve->filename,"ab");
	}

	if (! carve->fp) {
	  fprintf (stderr,           "Error opening file: %s -- %s\n", 
		   carve->filename, strerror(errno));
	  fprintf (state->auditFile, "Error opening file: %s -- %s\n", 
		   carve->filename, strerror(errno));
	  return SCALPEL_ERROR_FILE_WRITE;
	}
	else {
	  CURRENTFILESOPEN++;
	}
      }

      // write some portion of current readbuffer
      switch (operation) {
      case CONTINUECARVE:
	offset=0;
	bytestowrite=SIZE_OF_BUFFER;
	break;
      case STARTSTOPCARVE:
	offset=carve->start - (fileposition-bytesread);
	bytestowrite = carve->stop - carve->start + 1;
	break;
      case STARTCARVE:
	offset = carve->start - (fileposition-bytesread);
	bytestowrite = (carve->stop - carve->start + 1) >
	  (SIZE_OF_BUFFER - offset) ? (SIZE_OF_BUFFER - offset) :
	  (carve->stop - carve->start + 1);
	break;
      case STOPCARVE:
	offset = 0;
	bytestowrite=carve->stop - (fileposition-bytesread) + 1;
	break;
      }

      if (! state->previewMode) {
	if ((byteswritten = fwrite(readbuffer + offset,
				   sizeof(char),
				   bytestowrite,
				   carve->fp)) != bytestowrite) {
	  
	  fprintf(stderr,"Error writing to file: %s -- %s\n",
		  carve->filename, strerror(ferror(carve->fp)));
	  fprintf(state->auditFile,"Error writing to file: %s -- %s\n",
		  carve->filename, strerror(ferror(carve->fp)));
	  return SCALPEL_ERROR_FILE_WRITE;
	}
      }

      // close file, if necessary.  Always do it on STARTSTOPCARVE and
      // STOPCARVE, but also do it if we have a large number of files
      // open, otherwise we'll run out of available file handles.  Updating the
      // coverage blockmap and auditing is done here, when a file being carved
      // is closed for the last time.
      if (operation == STARTSTOPCARVE || 
	  operation == STOPCARVE || 
	  CURRENTFILESOPEN > MAX_FILES_TO_OPEN) {
	err=0;
	if (! state->previewMode) {
	  if (state->modeVerbose) {
	    fprintf(stdout, "CLOSING %s\n", carve->filename);
	  }
	  err = fclose(carve->fp);
	}

	if (err) {
	  fprintf(stderr,           "Error closing file: %s -- %s\n\n",
		  carve->filename,strerror(ferror(carve->fp)));
	  fprintf(state->auditFile, "Error closing file: %s -- %s\n\n",
		  carve->filename,strerror(ferror(carve->fp)));
	  return SCALPEL_ERROR_FILE_WRITE;
	}
	else {
	  CURRENTFILESOPEN--;
	  carve->fp=0;

	  // release filename buffer if it won't be needed again.  Don't release it
	  // if the file was closed only because a large number of files are currently
	  // open!
	  if (operation == STARTSTOPCARVE || operation == STOPCARVE) {
	    auditUpdateCoverageBlockmap(state, carve);
	    free(carve->filename);
	  }
	}
      }
      next_element(&carvelists[(fileposition-bytesread) / SIZE_OF_BUFFER]);
    }
  }
  
  closeFile(infile);

  // write header/footer database, if necessary, before 
  // cleanup for current image file.  

  if (state->generateHeaderFooterDatabase) {
    if ((err = writeHeaderFooterDatabase(state)) != SCALPEL_OK) {
      return err;
    }
  }

  // tear down coverage maps, if necessary
  destroyCoverageMaps(state);

  printf("Processing of image file complete. Cleaning up...\n");

  // tear down header/footer databases

  for (needlenum=0; 
       state->SearchSpec[needlenum].suffix != NULL; 
       needlenum++) {
    currentneedle = &(state->SearchSpec[needlenum]);
    if (currentneedle->offsets.headers) {
      free(currentneedle->offsets.headers);
    }
    if (currentneedle->offsets.footers) {
      free(currentneedle->offsets.footers);
    }
    currentneedle->offsets.headers=0;
    currentneedle->offsets.footers=0;
    currentneedle->offsets.numheaders=0;
    currentneedle->offsets.numfooters=0;
    currentneedle->offsets.headerstorage=0;
    currentneedle->offsets.footerstorage=0;
  }
  
  // tear down work queues--no memory deallocation for each queue
  // entry required, because memory associated with fp and the
  // filename was freed after the carved file was closed.

  // destroy queues
  for (i=0; i < 2 + (filesize / SIZE_OF_BUFFER); i++) {
    destroy_queue(&carvelists[i]);
  }
  // destroy array of queues
  free(carvelists);

  printf("Done.");
  return SCALPEL_OK;
}



// write header/footer database for current image file into the
// Scalpel output directory. No information is written into the
// database for file types without a suffix.  The filename used
// is the current image filename with ".hfd" appended.  The 
// format of the database file is straightforward:
//
// suffix_#1 (string)
// number_of_headers (unsigned long long)
// header_pos_#1 (unsigned long long)
// header_pos_#2 (unsigned long long) 
// ...
// number_of_footers (unsigned long long)
// footer_pos_#1 (unsigned long long)
// footer_pos_#2 (unsigned long long) 
// ...
// suffix_#2 (string)
// number_of_headers (unsigned long long)
// header_pos_#1 (unsigned long long)
// header_pos_#2 (unsigned long long) 
// ...
// number_of_footers (unsigned long long)
// footer_pos_#1 (unsigned long long)
// footer_pos_#2 (unsigned long long) 
// ...
// ...
//
// If state->useCoverageBlockmap, then translation is required to
// produce real disk image addresses for the generated header/footer
// database file, because the Scalpel carving engine isn't aware of
// gaps created by blocks that are covered by previously carved files.

static int writeHeaderFooterDatabase(struct scalpelState *state) {
  
  FILE *dbfile;
  char fn[MAX_STRING_LENGTH];  // filename for header/footer database
  int needlenum;
  struct SearchSpecLine *currentneedle;
  int i;
  
  // generate unique name for header/footer database
  snprintf(fn,MAX_STRING_LENGTH,"%s/%s.hfd",
	   state->outputdirectory,
	   base_name(state->imagefile));
  
  if ((dbfile = fopen(fn,"w")) == NULL) {
    fprintf(stderr,"Error writing to header/footer database file: %s\n",
	    fn);
    fprintf(state->auditFile, "Error writing to header/footer database file: %s\n",
	    fn);
    return SCALPEL_ERROR_FILE_WRITE;
  }

#ifdef __WIN32
  // set binary mode for Win32
  setmode(fileno(dbfile),O_BINARY);
#endif
#ifdef __LINUX
  fcntl(fileno(dbfile),F_SETFL, O_LARGEFILE);
#endif

  for (needlenum=0; 
       state->SearchSpec[needlenum].suffix != NULL; 
       needlenum++) {
    
    currentneedle = &(state->SearchSpec[needlenum]);
    
    if (currentneedle->suffix[0] != SCALPEL_NOEXTENSION) {
      // output current suffix
      if (fprintf(dbfile, "%s\n", currentneedle->suffix) <= 0) {
	fprintf(stderr,"Error writing to header/footer database file: %s\n",
		fn);
	fprintf(state->auditFile, "Error writing to header/footer database file: %s\n",
		fn);
	return SCALPEL_ERROR_FILE_WRITE;
      }
      
      // # of headers
#ifdef __WIN32
      if (fprintf(dbfile, "%I64u\n", currentneedle->offsets.numheaders) <= 0) {
#else
      if (fprintf(dbfile, "%llu\n", currentneedle->offsets.numheaders) <= 0) {
#endif
	fprintf(stderr,"Error writing to header/footer database file: %s\n",
		fn);
	fprintf(state->auditFile, "Error writing to header/footer database file: %s\n",
		fn);
	return SCALPEL_ERROR_FILE_WRITE;
      }

      // all header positions for current suffix
      for (i=0; i < currentneedle->offsets.numheaders; i++) {
#ifdef __WIN32
	if (fprintf(dbfile, "%I64u\n", positionUseCoverageBlockmap(state, currentneedle->offsets.headers[i])) <= 0) {
#else
	  if (fprintf(dbfile, "%llu\n", positionUseCoverageBlockmap(state, currentneedle->offsets.headers[i])) <= 0) {
#endif
	  fprintf(stderr,"Error writing to header/footer database file: %s\n",
		  fn);
	  fprintf(state->auditFile, "Error writing to header/footer database file: %s\n",
		  fn);
	  return SCALPEL_ERROR_FILE_WRITE;
	}
      }
	
      // # of footers
#ifdef __WIN32
      if (fprintf(dbfile, "%I64u\n", currentneedle->offsets.numfooters) <= 0) {
#else
      if (fprintf(dbfile, "%llu\n", currentneedle->offsets.numfooters) <= 0) {
#endif
	fprintf(stderr,"Error writing to header/footer database file: %s\n",
		fn);
	fprintf(state->auditFile, "Error writing to header/footer database file: %s\n",
		fn);
	return SCALPEL_ERROR_FILE_WRITE;
      }
      
      // all footer positions for current suffix
      for (i=0; i < currentneedle->offsets.numfooters; i++) {
#ifdef __WIN32
	if (fprintf(dbfile, "%I64u\n", positionUseCoverageBlockmap(state, currentneedle->offsets.footers[i])) <= 0) {
#else
	  if (fprintf(dbfile, "%llu\n", positionUseCoverageBlockmap(state, currentneedle->offsets.footers[i])) <= 0) {
#endif
	  fprintf(stderr,"Error writing to header/footer database file: %s\n",
		  fn);
	  fprintf(state->auditFile, "Error writing to header/footer database file: %s\n",
		  fn);
	  return SCALPEL_ERROR_FILE_WRITE;
	}
      }
   }
  }
 fclose(dbfile);

 return SCALPEL_OK;
}

      
// The coverage blockmap illustrates which blocks (of a
// user-specified size) have been "covered" by a carved file.
// The filename used for the coverage bitmap is the current
// image filename with ".map" appended, generated in a
// user-specified directory.  If the coverage blockmap is to be
// modified, check to see if it exists.  If it does, then open
// it and set the file handle in the Scalpel state.  If it
// doesn't, create a zeroed copy and set the file handle in the
// Scalpel state.  If the coverage blockmap is guiding carving,
// then create the coverage bitmap and initialize it using the
// coverage blockmap file.  The difference between the coverage
// blockmap (on disk) and the coverage bitmap (in memory) is
// that the blockmap counts carved files that cover a block.
// The coverage bitmap only indicates if ANY carved file covers
// a block.  'filesize' is the size of the image file being
// examined.

static int setupCoverageMaps(struct scalpelState *state, unsigned long long filesize) {
	
  char fn[MAX_STRING_LENGTH];  // filename for coverage blockmap
  unsigned long long i, k;
  int empty;
  unsigned int blocksize, entry;
  

  state->coveragebitmap=0;
  state->coverageblockmap=0;
  
  if (state->modeVerbose && (state->useCoverageBlockmap || state->updateCoverageBlockmap)) {
    fprintf(stdout, "Setting up coverage maps.\n");
  }
  
  if (state->updateCoverageBlockmap || state->useCoverageBlockmap) {
    // generate pathname for coverage blockmap
    snprintf(fn,MAX_STRING_LENGTH,"%s/%s.map",
	     state->coveragedirectory,
	     base_name(state->imagefile));

    if (state->modeVerbose) {
      fprintf(stdout, "Coverage blockmap is \"%s\".\n", 
	      fn);
    }
    
    empty = ((state->coverageblockmap = fopen(fn,"rb")) == NULL);

    if (state->modeVerbose) {
      fprintf(stdout, "Coverage blockmap file is %s.\n", 
	      (empty?"EMPTY":"NOT EMPTY"));
    }

    if (! empty) {
#ifdef __WIN32
      // set binary mode for Win32
      setmode(fileno(state->coverageblockmap),O_BINARY);
#endif
#ifdef __LINUX
      fcntl(fileno(state->coverageblockmap),F_SETFL, O_LARGEFILE);
#endif
      
      if (state->modeVerbose) {
	fprintf(stdout, "Reading blocksize from Coverage blockmap file.\n"); 
      }

      // read block size and make sure it matches user-specified block size
      if (fread(&blocksize, sizeof(unsigned int), 1, state->coverageblockmap) != 1) {
	fprintf(stderr,"Error reading coverage blockmap blocksize in\ncoverage blockmap file: %s\n",
		fn);
	fprintf(state->auditFile, "Error reading coverage blockmap blocksize in\ncoverage blockmap file: %s\n",
		fn);
	return SCALPEL_ERROR_FATAL_READ;
      }

      if (state->useCoverageBlockmap && ! state->updateCoverageBlockmap) {
	// just use blocksize in blockmap coverage file
	state->coverageblocksize = blocksize;

	if (state->modeVerbose) {
	  fprintf(stdout, "Blocksize for coverage blockmap is %u.\n", state->coverageblocksize);
	}
      }
      else if (blocksize != state->coverageblocksize) {
	fprintf(stderr,"User-specified blocksize does not match blocksize in\ncoverage blockmap file: %s\n",
		fn);
	fprintf(state->auditFile, "User-specified blocksize does not match blocksize in\ncoverage blockmap file: %s\n",
		fn);
	return SCALPEL_GENERAL_ABORT;
      }

      state->coveragenumblocks=ceil((double)filesize / (double)state->coverageblocksize);
      if (state->modeVerbose) {
#ifdef __WIN32
	fprintf(stdout, "# of blocks in coverage blockmap is %I64u.\n", state->coveragenumblocks);
#else
	fprintf(stdout, "# of blocks in coverage blockmap is %llu.\n", state->coveragenumblocks);
#endif
      }

      if (state->useCoverageBlockmap) {
	if (state->modeVerbose) {
	  fprintf(stdout, "Allocating and clearing coverage bitmap.\n");
	}
	// for bitmap, 8 bits per unsigned char, with each bit representing one
	// block
	state->coveragebitmap = malloc((state->coveragenumblocks / 8) 
				       * sizeof(unsigned char));
	checkMemoryAllocation(state, state->coveragebitmap, __LINE__, __FILE__, "coveragebitmap");

	// zap coverage bitmap 
	for (k=0; k < state->coveragenumblocks / 8; k++) {
	  state->coveragebitmap[k]=0;
	}
	
	fprintf(stdout, "Reading existing coverage blockmap...this may take a while.\n");
	
	for (i=0; i < state->coveragenumblocks; i++) {
	  fseeko(state->coverageblockmap, (i + 1) * sizeof(unsigned int), SEEK_SET);
	  if (fread(&entry, sizeof(unsigned int), 1, state->coverageblockmap) != 1) {
	    fprintf(stderr,"Error reading coverage blockmap entry (blockmap truncated?): %s\n", 
		    fn);
	    fprintf(state->auditFile, "Error reading coverage blockmap entry (blockmap truncated?): %s\n",
		    fn);
	    return SCALPEL_ERROR_FATAL_READ;
	  }
	  if (entry) {
	    state->coveragebitmap[i / 8] |= 1 << (i % 8);
	  }
	}
      }
    }
    else if (empty && state->useCoverageBlockmap) {
      fprintf(stderr,"-u option requires that the blockmap file %s exist.\n",
	      fn);
      fprintf(state->auditFile, "-u option requires that the blockmap file %s exist.\n",
	      fn);
      return SCALPEL_GENERAL_ABORT;
    }
    else {
      state->coveragenumblocks=ceil((double)filesize / (double)state->coverageblocksize);
      if (state->modeVerbose) {
#ifdef __WIN32
	fprintf(stdout, "# of blocks in coverage blockmap is %I64u.\n", state->coveragenumblocks);
#else
	fprintf(stdout, "# of blocks in coverage blockmap is %llu.\n", state->coveragenumblocks);
#endif
      }
    }
    
    // change mode to read/write for future updates if coverage blockmap will be updated
    if (state->updateCoverageBlockmap) {
      if (state->modeVerbose) {
	fprintf(stdout, "Changing mode of coverage blockmap file to R/W.\n");
      }
      
      if (! empty) {
	fclose(state->coverageblockmap);
      }
      if ((state->coverageblockmap = fopen(fn,(empty?"w+b":"r+b"))) == NULL) {
	fprintf(stderr,"Error writing to coverage blockmap file: %s\n",
		fn);
	fprintf(state->auditFile, "Error writing to coverage blockmap file: %s\n",
		fn);
	return SCALPEL_ERROR_FILE_WRITE;
      }

#ifdef __WIN32
      // set binary mode for Win32
      setmode(fileno(state->coverageblockmap),O_BINARY);
#endif
#ifdef __LINUX
      fcntl(fileno(state->coverageblockmap),F_SETFL, O_LARGEFILE);
#endif

      if (empty) {
	// create entries in empty coverage blockmap file
	fprintf(stdout, "Writing empty coverage blockmap...this may take a while.\n");
	entry=0;
	if (fwrite(&(state->coverageblocksize), sizeof(unsigned int), 1, state->coverageblockmap) != 1) {
	  fprintf(stderr,"Error writing initial entry in coverage blockmap file!\n");
	  fprintf(state->auditFile, "Error writing initial entry in coverage blockmap file!\n");
	  return SCALPEL_ERROR_FILE_WRITE;
	}
	for (k=0; k < state->coveragenumblocks; k++) {
	  if (fwrite(&entry, sizeof(unsigned int), 1, state->coverageblockmap) != 1) {
	    fprintf(stderr,"Error writing to coverage blockmap file!\n");
	    fprintf(state->auditFile, "Error writing to coverage blockmap file!\n");
	    return SCALPEL_ERROR_FILE_WRITE;
	  }
	}
      }
    }
  }
  
  if (state->modeVerbose && (state->useCoverageBlockmap || state->updateCoverageBlockmap)) {
    printf("Finished setting up coverage maps.\n");
  }

  return SCALPEL_OK;

 }

// map carve->start ... carve->stop into a queue of 'fragments' that
// define a carved file in the disk image.  
 static void generateFragments(struct scalpelState *state, Queue *fragments, CarveInfo *carve) {

  unsigned long long curblock, neededbytes=carve->stop - carve->start + 1, bytestoskip, 
    morebytes, totalbytes=0, curpos;

  Fragment frag;


  init_queue(fragments,
	     sizeof(struct Fragment),
	     TRUE, 0);
  
  if (! state->useCoverageBlockmap) {
    // no translation necessary
    frag.start = carve->start;
    frag.stop = carve->stop;
    add_to_queue(fragments, &frag, 0);
    return;
  }
  else {
    curpos = positionUseCoverageBlockmap(state, carve->start);
    curblock= curpos / state->coverageblocksize;
    
    while (totalbytes < neededbytes && curblock < state->coveragenumblocks) {

      morebytes=0;
      bytestoskip=0;
      
      // skip covered blocks
      while (curblock < state->coveragenumblocks && 
	     (state->coveragebitmap[curblock / 8] & (1 << (curblock % 8)))) {
	bytestoskip += state->coverageblocksize - 
			curpos % state->coverageblocksize;
	curblock++;
      }
      
      curpos += bytestoskip;
      
      // accumulate uncovered blocks in fragment
      while (curblock < state->coveragenumblocks && 
	     ((state->coveragebitmap[curblock / 8] & (1 << (curblock % 8))) == 0) &&
	     totalbytes + morebytes < neededbytes) {
	
	morebytes += state->coverageblocksize - 
		      curpos % state->coverageblocksize;
	
	curblock++;
      }
      
      // cap size
      if (totalbytes + morebytes > neededbytes) {
	morebytes = neededbytes - totalbytes;
      }
      
      frag.start=curpos;
      curpos += morebytes;
      frag.stop=curpos-1;
      totalbytes += morebytes;
      
      add_to_queue(fragments, &frag, 0);
    }
   }
 }


 // If the coverage blockmap is used to guide carving, then use the
 // coverage blockmap to map a logical index in the disk image (i.e.,
 // the index skips covered blocks) to an actual disk image index.  If
 // the coverage blockmap isn't being used, just returns the second
 // argument.  
 //
 // ***This function assumes that the 'position' does NOT lie
 // within a covered block! ***
static unsigned long long positionUseCoverageBlockmap(struct scalpelState *state, unsigned long long position) {
   

  unsigned long long totalbytes=0, neededbytes=position,
    morebytes, curblock=0, curpos=0, bytestoskip;
   
   if (! state->useCoverageBlockmap) {
     return position;
   }
   else {
     while (totalbytes < neededbytes && curblock < state->coveragenumblocks) {
       morebytes=0;
       bytestoskip=0;
      
       // skip covered blocks
       while (curblock < state->coveragenumblocks && 
	      (state->coveragebitmap[curblock / 8] & (1 << (curblock % 8)))) {
	 bytestoskip += state->coverageblocksize - 
	   curpos % state->coverageblocksize;
	 curblock++;
       }
      
      curpos += bytestoskip;
      
      // accumulate uncovered blocks
      while (curblock < state->coveragenumblocks && 
	     ((state->coveragebitmap[curblock / 8] & (1 << (curblock % 8))) == 0) &&
	     totalbytes + morebytes < neededbytes) {
	
	morebytes += state->coverageblocksize - 
		      curpos % state->coverageblocksize;
	curblock++;
      }
      
      // cap size
      if (totalbytes + morebytes > neededbytes) {
	morebytes = neededbytes - totalbytes;
      }
      
      curpos += morebytes;
      totalbytes += morebytes;
     }
     
     return curpos;
   }
 }

 
 
// update the coverage blockmap for a carved file (if appropriate) and write entries into
// the audit log describing the carved file.  If the file is fragmented, then multiple
// lines are written to indicate where the fragments occur. 
 static int auditUpdateCoverageBlockmap(struct scalpelState *state, struct CarveInfo *carve) {

   struct Queue fragments;  
   Fragment *frag;
   int k, err;

   // If the coverage blockmap used to guide carving, then carve->start and
   // carve->stop may not correspond to addresses in the disk image--the coverage blockmap
   // processing layer in Scalpel may have skipped "in use" blocks.  Transform carve->start
   // and carve->stop into a list of fragments that contain real disk image offsets.
   generateFragments(state, &fragments, carve);
   
   rewind_queue(&fragments);
   while (! end_of_queue(&fragments)) {
     frag = (Fragment *)pointer_to_current(&fragments);
     fprintf(state->auditFile,"%s",
	     base_name(carve->filename));
#ifdef __WIN32
     fprintf(state->auditFile,"%13I64u\t\t",
	     frag->start);
#else
     fprintf(state->auditFile,"%13llu\t\t",
	     frag->start);
#endif
     
     fprintf(state->auditFile,"%3s", 
	     carve->chopped ? "YES   " : "NO    ");
     
#ifdef __WIN32
     fprintf(state->auditFile,"%13I64u\t\t",
	     frag->stop - frag->start + 1);
#else
     fprintf(state->auditFile,"%13llu\t\t",
	     frag->stop - frag->start + 1);
#endif
     
     fprintf(state->auditFile,"%s\n",
	     base_name(state->imagefile));

     // update coverage blockmap, if appropriate
     if (state->updateCoverageBlockmap) {
       for (k=frag->start / state->coverageblocksize; 
	    k <= frag->stop / state->coverageblocksize; k++) {
	 if ((err = updateCoverageBlockmap(state, k)) != SCALPEL_OK) {
	   destroy_queue(&fragments);
	   return err;
	 }
       }
     }
     next_element(&fragments);
   }
   
   destroy_queue(&fragments);

   return SCALPEL_OK;
 }
   


 static int updateCoverageBlockmap(struct scalpelState *state, unsigned long long block) {
   
   unsigned int entry;

   if (state->updateCoverageBlockmap) {
     // first entry in file is block size, so seek one unsigned int further
     fseeko(state->coverageblockmap, (block+1) * sizeof(unsigned int), SEEK_SET);
     if (fread(&entry, sizeof(unsigned int), 1, state->coverageblockmap) != 1) {
       fprintf(stderr,"Error reading coverage blockmap entry!\n");
       fprintf(state->auditFile, "Error reading coverage blockmap entry!\n");
       return SCALPEL_ERROR_FATAL_READ;
     }
     entry++;
     // first entry in file is block size, so seek one unsigned int further 
     fseeko(state->coverageblockmap, (block+1) * sizeof(unsigned int), SEEK_SET);
     if (fwrite(&entry, sizeof(unsigned int), 1, state->coverageblockmap) != 1) {
       fprintf(stderr,"Error writing to coverage blockmap file!\n");
       fprintf(state->auditFile, "Error writing to coverage blockmap file!\n");
       return SCALPEL_ERROR_FILE_WRITE;
     }
   }
   
   return SCALPEL_OK;
 }
 
 
 
 static void destroyCoverageMaps(struct scalpelState *state) {
   
   // free memory associated with coverage bitmap, close coverage blockmap file
   
   if (state->coveragebitmap) {
     free(state->coveragebitmap);
   }
   
   if (state->useCoverageBlockmap || state->updateCoverageBlockmap) {
     fclose(state->coverageblockmap);
   }
 }
 

 // simple wrapper for fseeko with SEEK_CUR semantics that uses the
 // coverage bitmap to skip over covered blocks, IF the coverage
 // blockmap is being used.  The offset is adjusted so that covered
 // blocks are silently skipped when seeking if the coverage blockmap
 // is used, otherwise an fseeko() with an umodified offset is
 // performed.
static int fseeko_use_coverage_map(struct scalpelState *state, FILE *fp, off64_t offset) {

  off64_t currentpos;
  unsigned long long curblock, bytestoskip, bytestokeep, totalbytes=0;
  int sign;

  if (state->useCoverageBlockmap) {
    currentpos=ftello(fp);
    sign = (offset > 0 ? 1 : -1);

     curblock= currentpos / state->coverageblocksize;
     
     while (totalbytes < (offset > 0 ? offset : offset * -1) && 
	    curblock < state->coveragenumblocks && curblock >= 0) {
       bytestoskip=0;

       // covered blocks increase offset
       while (curblock < state->coveragenumblocks &&
	      curblock >= 0 &&
	      (state->coveragebitmap[curblock / 8] & (1 << (curblock % 8)))) {

	 bytestoskip += (state->coverageblocksize - 
			 currentpos % state->coverageblocksize);
	 curblock += sign;
       }
       
       offset += (bytestoskip * sign);
       currentpos += (bytestoskip * sign);

       bytestokeep=0;

       // uncovered blocks don't increase offset
       while (curblock < state->coveragenumblocks && 
	      curblock >= 0 &&
	      ((state->coveragebitmap[curblock / 8] & (1 << (curblock % 8))) == 0) &&
	      totalbytes < (offset > 0 ? offset : offset * -1)) {
	 
	 bytestokeep += (state->coverageblocksize - 
			currentpos % state->coverageblocksize);
	 
	 curblock += sign;
       }

       totalbytes += bytestokeep;
       currentpos += (bytestokeep * sign);
     }
  }

  return fseeko(fp, offset, SEEK_CUR);
 }

 

// simple wrapper for ftello() that uses the coverage bitmap to
// report the current file position *minus* the contribution of
// marked blocks, IF the coverage blockmap is being used.  If a
// coverage blockmap isn't in use, just performs a standard ftello()
// call.
//
// GGRIII:  *** This could use optimization, e.g., use of a pre-computed
// table to avoid walking the coverage bitmap on each call.
 
static off64_t ftello_use_coverage_map(struct scalpelState *state, FILE *fp) {
   
  off64_t currentpos, decrease=0;
  unsigned long long endblock, k;

  currentpos=ftello(fp);  

  if (state->useCoverageBlockmap) {
    endblock = currentpos / state->coverageblocksize;
    
    // covered blocks don't contribute to current file position
    for (k=0; k <= endblock; k++) {
      if (state->coveragebitmap[k / 8] & (1 << (k % 8))) {
	decrease += state->coverageblocksize;
      }
    }

    if (state->coveragebitmap[endblock / 8] & (1 << (endblock % 8))) {
      decrease += (state->coverageblocksize - 
		   currentpos % state->coverageblocksize);
    }
    
    if (state->modeVerbose && state->useCoverageBlockmap) {
#ifdef __WIN32
      fprintf(stdout, "Coverage map decreased current file position by %I64u bytes.\n", (unsigned long long)decrease);
#else
      fprintf(stdout, "Coverage map decreased current file position by %llu bytes.\n", (unsigned long long)decrease);
#endif
    }
  }
    
  return currentpos - decrease;
 }
 


 // simple wrapper for fread() that uses the coverage bitmap--the read silently
 // skips blocks that are marked covered (corresponding bit in coverage
 // bitmap is 1)
static size_t fread_use_coverage_map(struct scalpelState *state, void *ptr, 
			      size_t size, size_t nmemb, FILE *stream) {  

  unsigned long long curblock, neededbytes=nmemb * size, bytestoskip, 
    bytestoread, bytesread, totalbytesread=0, curpos;
  int shortread;


   if (state->useCoverageBlockmap) {
     if (state->modeVerbose) {
#ifdef __WIN32
       fprintf(stdout, "Issuing coverage map-based READ, wants %I64u bytes.\n", neededbytes);
#else
       fprintf(stdout, "Issuing coverage map-based READ, wants %llu bytes.\n", neededbytes);
#endif
     }
     
     curpos = ftello(stream);
     curblock= curpos / state->coverageblocksize;
     shortread=0;
     
     while (totalbytesread < neededbytes && curblock < state->coveragenumblocks && ! shortread) {
       bytestoread=0;
       bytestoskip=0;

       // skip covered blocks
       while (curblock < state->coveragenumblocks && 
	      (state->coveragebitmap[curblock / 8] & (1 << (curblock % 8)))) {
	 bytestoskip += (state->coverageblocksize - 
			 curpos % state->coverageblocksize);
	 curblock++;
       }
       
       curpos += bytestoskip;


       if (state->modeVerbose) {
#ifdef __WIN32
	 fprintf(stdout, "fread using coverage map to skip %I64u bytes.\n", bytestoskip);
#else
	 fprintf(stdout, "fread using coverage map to skip %llu bytes.\n", bytestoskip);
#endif
       }
       
       fseeko(stream, (off64_t)bytestoskip, SEEK_CUR);
       
       // accumulate uncovered blocks for read
       while (curblock < state->coveragenumblocks && 
	      ((state->coveragebitmap[curblock / 8] & (1 << (curblock % 8))) == 0) &&
	      totalbytesread + bytestoread <= neededbytes) {

	 bytestoread += (state->coverageblocksize - 
			 curpos % state->coverageblocksize);
	 
	 curblock++;
       }

       // cap read size
       if (totalbytesread + bytestoread > neededbytes) {
	 bytestoread = neededbytes - totalbytesread;
       }


       if (state->modeVerbose) {
#ifdef __WIN32
	 fprintf(stdout, "fread using coverage map found %I64u consecutive bytes.\n", bytestoread);
#else
	 fprintf(stdout, "fread using coverage map found %llu consecutive bytes.\n", bytestoread);
#endif
       }

       if ((bytesread=fread((char *)ptr+totalbytesread, 1, (size_t)bytestoread, stream)) < bytestoread) {
	 shortread=1;
       }

       totalbytesread += bytesread;
       curpos += bytestoread;

       if (state->modeVerbose) {
#ifdef __WIN32
	 fprintf(stdout, "fread using coverage map read %I64u bytes.\n", bytesread);
#else
	 fprintf(stdout, "fread using coverage map read %llu bytes.\n", bytesread);
#endif
       }
     }

     if (state->modeVerbose) {
       fprintf(stdout, "Coverage map-based READ complete.\n");
     }

     // conform with fread() semantics by returnign # of items read
     return totalbytesread / size;
   }
   else {
     return fread(ptr, size, nmemb, stream);
   }
 }

