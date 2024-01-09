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
// foremost.  Additional thanks to Vassil Roussev and Vico Marziale, of the
// Department of Computer Science at the University of New Orleans, for 
// ongoing support of Scalpel.
//
// Scalpel currently uses the SAME configuration file format as
// foremost 0.69.  This may change in the future, because of some
// planned new features.
//

#include "scalpel.h"

// GLOBALS
int signal_caught; 
char wildcard;
int ttywidth;
char *__progname;

void usage() {
  
  printf("Carves files from a disk image based on file headers and footers.\n");
  printf("\nUsage: scalpel [-b] [-c <config file>] [-d] [-h|V] [-i <file>]\n");
  printf("                 [-m blocksize] [-n] [-o <outputdir>] [-O num] [-q clustersize]\n");
  printf("                 [-r] [-s num] [-t <blockmap file>] [-u] [-v]\n"); 
  printf("                 <imgfile> [<imgfile>] ...\n\n");
  printf("-b  Carve files even if defined footers aren't discovered within\n");
  printf("    maximum carve size for file type [foremost 0.69 compat mode].\n");
  printf("-c  Choose configuration file.\n");
  printf("-d  Generate header/footer database; will bypass certain optimizations\n");
  printf("    and discover all footers, so performance suffers.  Doesn't affect\n");
  printf("    the set of files carved.  **EXPERIMENTAL**\n");
  printf("-h  Print this help message and exit.\n");
  printf("-i  Read names of disk images from specified file.\n");
  printf("-m  Generate/update carve coverage blockmap file.  The first 32bit\n");
  printf("    unsigned int in the file identifies the block size. Thereafter\n");
  printf("    each 32bit unsigned int entry in the blockmap file corresponds\n");
  printf("    to one block in the image file.  Each entry counts how many\n");
  printf("    carved files contain this block. Requires more memory and\n");
  printf("    disk.  **EXPERIMENTAL**\n");
  printf("-n  Don't add extensions to extracted files.\n");
  printf("-o  Set output directory for carved files.\n");
  printf("-O  Don't organize carved files by type. Default is to organize carved files\n");
  printf("    into subdirectories.\n");
  printf("-p  Perform image file preview; audit log indicates which files\n");
  printf("    would have been carved, but no files are actually carved.\n");
  printf("-q  Carve only when header is cluster-aligned.\n"); 
  printf("-r  Find only first of overlapping headers/footers [foremost 0.69 compat mode].\n");
  printf("-s  Skip n bytes in each disk image before carving.\n");
  printf("-t  Set directory for coverage blockmap.  **EXPERIMENTAL**\n");
  printf("-u  Use carve coverage blockmap when carving.  Carve only sections\n");
  printf("    of the image whose entries in the blockmap are 0.  These areas\n");
  printf("    are treated as contiguous regions.  **EXPERIMENTAL**\n");
  printf("-V  Print copyright information and exit.\n");
  printf("-v  Verbose mode.\n");
}


// signal handler, sets global variable 'signal_caught' which is
// checked periodically during carve operations.  Allows clean
// shutdown.
void catch_alarm (int signum) {
  signal_caught = signum;
  signal(signum,catch_alarm);

#ifdef __DEBUG
  fprintf(stderr,"\nCaught signal: %s.\n",(char*) strsignal(signum));
#endif

  fprintf (stderr, "\nKill signal detected. Cleaning up...\n");
}


int extractSearchSpecData(struct SearchSpecLine *s,char **tokenarray) {

  // process one line from config file:
  //     token[0] = suffix
  //     token[1] = case sensitive?
  //     token[2] = maximum carve size
  //     token[3] = begintag
  //     token[4] = endtag
  //     token[5] = search type (optional)

  s->suffix = malloc(MAX_SUFFIX_LENGTH*sizeof(char));
  s->begin  = malloc(MAX_STRING_LENGTH*sizeof(char));
  s->end    = malloc(MAX_STRING_LENGTH*sizeof(char));    
  
  if (!strncasecmp(tokenarray[0],
                   SCALPEL_NOEXTENSION_SUFFIX,
                   strlen(SCALPEL_NOEXTENSION_SUFFIX))) {
    s->suffix[0] = SCALPEL_NOEXTENSION;
    s->suffix[1] = 0;
  }
  else {
    memcpy(s->suffix,tokenarray[0],MAX_SUFFIX_LENGTH);
  }
  
  // case sensitivity check
  s->casesensitive = (!strncasecmp(tokenarray[1],"y",1) || 
		      !strncasecmp(tokenarray[1],"yes",3));
  
  //#ifdef __WIN32
  //    s->length = _atoi64(tokenarray[2]);
  //#else
  //  s->length = atoull(tokenarray[2]);
  //#endif

  s->length = strtoull(tokenarray[2], 0, 10);

  // determine search type for this needle
  s->searchtype = SEARCHTYPE_FORWARD;
  if (!strncasecmp(tokenarray[5],"REVERSE",strlen("REVERSE"))) {
    s->searchtype = SEARCHTYPE_REVERSE;
  }
  else if (!strncasecmp(tokenarray[5],"NEXT",strlen("NEXT"))) {
    s->searchtype = SEARCHTYPE_FORWARD_NEXT;
  }
  // FORWARD is the default, but OK if the user defines it explicitly
  else if (!strncasecmp(tokenarray[5],"FORWARD",strlen("FORWARD"))) {
    s->searchtype = SEARCHTYPE_FORWARD;
  }

  s->beginlength = translate(tokenarray[3]);
  memcpy(s->begin,tokenarray[3],s->beginlength);
  s->endlength = translate(tokenarray[4]);
  memcpy(s->end,tokenarray[4],s->endlength);  
 
  init_bm_table(s->begin,s->begin_bm_table,s->beginlength, 
		s->casesensitive);
  init_bm_table(s->end,s->end_bm_table,s->endlength,
		s->casesensitive);
  return SCALPEL_OK;
}


int processSearchSpecLine(struct scalpelState *state, char *buffer, 
			  int lineNumber) {
  
  char *buf = buffer;
  char *token;
  char **tokenarray = (char **) malloc(6*sizeof(char[MAX_STRING_LENGTH+1]));
  int i = 0, err = 0, len = strlen(buffer);

  // murder CTRL-M (0x0d) characters that terminate a line
  if (len >= 2 && buffer[len-2] == 0x0d && buffer[len-1] == 0x0a) {  
    buffer[len-2] = buffer[len-1];
    buffer[len-1] = buffer[len];
  }

  buf = (char*) skipWhiteSpace(buf);
  token = strtok(buf," \t\n");

  // lines beginning with # are comments
  if(token == NULL || token[0] == '#'){  
    return SCALPEL_OK;
  }
    
  // allow wildcard to be changed
  if (!strncasecmp(token,"wildcard",9)) {
    if ((token = strtok(NULL," \t\n")) != NULL) {
      translate(token);
    } 
    else {
      fprintf (stdout,"Warning: Empty wildcard in configuration file line %d. Ignoring.\n",
	       lineNumber);
      return SCALPEL_OK;
    }

    if (strlen(token) > 1) {
      fprintf(stderr,"Warning: Wildcard can only be one character,"
	      " but you specified %d characters.\n"
	      "         Using the first character, \"%c\", as the wildcard.\n",
	      (int)strlen(token),token[0]);
    }

    wildcard = token[0];
    return SCALPEL_OK;
  }
    
  while (token && (i < NUM_SEARCH_SPEC_ELEMENTS)){
    tokenarray[i] = token;
    i++;
    token = strtok(NULL," \t\n");
  }
  
  switch(NUM_SEARCH_SPEC_ELEMENTS-i){
    case 2:
      tokenarray[NUM_SEARCH_SPEC_ELEMENTS-1] = "";
      tokenarray[NUM_SEARCH_SPEC_ELEMENTS-2] = "";
      break;
    case 1:
      tokenarray[NUM_SEARCH_SPEC_ELEMENTS-1] = "";
      break;
    case 0:
      break;
    default:
      fprintf(stderr, 
	      "\nERROR: In line %d of the configuration file, expected %d tokens,\n"
	      "       but instead found only %d.\n", 
	      lineNumber,NUM_SEARCH_SPEC_ELEMENTS,i);
      return SCALPEL_ERROR_NO_SEARCH_SPEC;
      break;
      
  }

  if ((err = extractSearchSpecData(&(state->SearchSpec[state->specLines]),
				   tokenarray))) {
    switch(err) {
    default:
      fprintf(stderr,
	      "\nERROR: Unknown error on line %d of the configuration file.\n"
	      ,lineNumber);
    }
  }
  state->specLines++; 
  return SCALPEL_OK;
}


// process configuration file
int readSearchSpecFile(struct scalpelState *state) {

  int lineNumber = 0, status;
  FILE *f;
  
  char *buffer = malloc((NUM_SEARCH_SPEC_ELEMENTS * MAX_STRING_LENGTH + 1) * sizeof(char));

  f = fopen(state->conffile,"r");  
  if (f == NULL) {
    fprintf (stderr,
	     "ERROR: Couldn't open configuration file: %s -- %s\n", 
	     state->conffile,strerror(errno));
    free(buffer);
    return SCALPEL_ERROR_FILE_OPEN;
  }

  while (fgets(buffer,NUM_SEARCH_SPEC_ELEMENTS * MAX_STRING_LENGTH, f)) {
    lineNumber++;

    if (state->specLines > MAX_FILE_TYPES) {
      fprintf(stderr,"Your conf file contains too many file types.\n");
      fprintf(stderr,"This version was compiled with MAX_FILE_TYPES == %d.\n",
              MAX_FILE_TYPES);
      fprintf(stderr,"Increase MAX_FILE_TYPES, recompile, and try again.\n");
      free(buffer);
      return SCALPEL_ERROR_TOO_MANY_TYPES;
    }

    if ((status = processSearchSpecLine(state,buffer,lineNumber)) != SCALPEL_OK) {
      free(buffer);
      return status;
    }
  }

  // add an empty object to the end of the list as a marker

  state->SearchSpec[state->specLines].suffix = NULL;
  state->SearchSpec[state->specLines].casesensitive = 0;
  state->SearchSpec[state->specLines].length = 0;
  state->SearchSpec[state->specLines].begin = NULL;
  state->SearchSpec[state->specLines].beginlength = 0;
  state->SearchSpec[state->specLines].end = NULL;
  state->SearchSpec[state->specLines].endlength = 0;

  // GGRIII: offsets field is uninitialized--it doesn't
  // matter, since we won't use this entry.
  
  fclose(f);
  free(buffer);
  return SCALPEL_OK;
}

// Register the signal-handler that will write to the audit file and
// close it if we catch a SIGINT or SIGTERM 
void registerSignalHandlers() {
  if (signal (SIGINT, catch_alarm) == SIG_IGN) {
      signal (SIGINT, SIG_IGN);
  }
  if (signal (SIGTERM, catch_alarm) == SIG_IGN) {
    signal (SIGTERM, SIG_IGN);
  }

#ifndef __WIN32
    // *****GGRIII:  is this problematic?
    // From foremost 0.69:
    /* Note: I haven't found a way to get notified of 
       console resize events in Win32.  Right now the statusbar
       will be too long or too short if the user decides to resize 
       their console window while foremost runs.. */

    signal(SIGWINCH, setttywidth);
#endif 
}

// initialize state variable and copy command line arguments
void initializeState(char **argv, struct scalpelState *state) {
  
  char** argvcopy = argv;
  int sss;
  int i;
  
  // Allocate memory for the state 
  state->imagefile        = (char*) malloc(MAX_STRING_LENGTH * sizeof(char));
  state->inputFileList    = (char*) malloc(MAX_STRING_LENGTH * sizeof(char));
  state->conffile         = (char*) malloc(MAX_STRING_LENGTH * sizeof(char));
  state->outputdirectory  = (char*) malloc(MAX_STRING_LENGTH * sizeof(char));
  state->invocation       = (char*) malloc(MAX_STRING_LENGTH * sizeof(char));

  // GGRIII: memory allocation made more sane, because we're storing
  // more information in Scalpel than foremost had to, for each file
  // type.
  sss = (MAX_FILE_TYPES+1)*sizeof(struct SearchSpecLine);
  state->SearchSpec = (struct SearchSpecLine*) malloc(sss);
  state->specLines = 0;

  // GGRIII: initialize header/footer offset data, carved file count,
  // et al.  The header/footer database is re-initialized in "dig.c"
  // after each image file is processed (numfilestocarve and
  // organizeDirNum are not). Storage for the header/footer offsets
  // will be reallocated as needed.

  for (i=0; i < MAX_FILE_TYPES; i++) {
    state->SearchSpec[i].offsets.headers = 0;
    state->SearchSpec[i].offsets.footers = 0;
    state->SearchSpec[i].offsets.numheaders = 0;
    state->SearchSpec[i].offsets.numfooters = 0;
    state->SearchSpec[i].offsets.headerstorage = 0;
    state->SearchSpec[i].offsets.footerstorage = 0;
    state->SearchSpec[i].numfilestocarve = 0;
    state->SearchSpec[i].organizeDirNum = 0;
  }

  state->fileswritten = 0;
  state->skip = 0;
  state->organizeMaxFilesPerSub = MAX_FILES_PER_SUBDIRECTORY;
  state->modeVerbose = FALSE;
  state->modeNoSuffix = FALSE;
  state->useInputFileList = FALSE;
  state->carveWithMissingFooters = FALSE;
  state->noSearchOverlap = FALSE;
  state->generateHeaderFooterDatabase = FALSE;
  state->updateCoverageBlockmap = FALSE;
  state->useCoverageBlockmap = FALSE;
  state->blockAlignedOnly = FALSE; 
  state->organizeSubdirectories = TRUE;
  state->previewMode = FALSE;
  state->ignoreEmbedded = FALSE;
  state->auditFile = NULL;

  // default values for output directory, config file, wildcard character,
  // coverage blockmap directory
  strncpy(state->outputdirectory,SCALPEL_DEFAULT_OUTPUT_DIR,
	  MAX_STRING_LENGTH);
  strncpy(state->conffile,SCALPEL_DEFAULT_CONFIG_FILE,
	  MAX_STRING_LENGTH);
  state->coveragedirectory = state->outputdirectory;
  wildcard = SCALPEL_DEFAULT_WILDCARD;
  signal_caught = 0;
  state->invocation[0] = 0;
  
  // copy the invocation string into the state
  do {
    strncat(state->invocation,  
	    *argvcopy, 
	    MAX_STRING_LENGTH-strlen(state->invocation));
    strncat(state->invocation,
	    " ",
	    MAX_STRING_LENGTH-strlen(state->invocation));
    ++argvcopy;  
  } while (*argvcopy);  

  registerSignalHandlers();
}

// parse command line arguments
void processCommandLineArgs(int argc, char **argv, 
			    struct scalpelState *state) {
  int i;

  while ((i = getopt(argc, argv, "bhvVundpq:rt:c:o:s:i:m:O")) != -1) {
    switch (i) {
	
    case 'V':
      fprintf (stdout,SCALPEL_COPYRIGHT_STRING);
      exit(1);
      
    case 'h':
      usage();
      exit(1);
      
    case 's':
      state->skip = strtoull(optarg,NULL,10);
      fprintf (stdout,"Skipping the first %lld bytes of each image file.\n",
	       state->skip);
      break;
      
    case 'c':
      strncpy(state->conffile,optarg,MAX_STRING_LENGTH);
      break;

    case 'd':
      state->generateHeaderFooterDatabase = TRUE;
      break;

      // -e support is currently a work-in-progress and will be enabled in a future release
      //    case 'e':
      //      state->ignoreEmbedded=TRUE;
      //      break;

    case 'm':
      state->updateCoverageBlockmap = TRUE;
      state->coverageblocksize=strtoul(optarg,NULL,10);
      if (state->coverageblocksize <= 0) {
	fprintf(stderr, 
		"\nERROR: Invalid blocksize for -m command line option.\n");
	exit(1);
      }
      break;
      
    case 'o':
      strncpy(state->outputdirectory,optarg,MAX_STRING_LENGTH);
      break;

    case 't':
      state->coveragedirectory = (char *) malloc(MAX_STRING_LENGTH * sizeof(char));
      strncpy(state->coveragedirectory,optarg,MAX_STRING_LENGTH);
      break;

    case 'O':
      state->organizeSubdirectories = FALSE;
      break;

    case 'p':
      state->previewMode = TRUE;
      break;

    case 'b':
      state->carveWithMissingFooters = TRUE;
      break;
      
    case 'i':
      state->useInputFileList = TRUE;
      state->inputFileList = optarg;  
      break;	  
      
    case 'n':
      state->modeNoSuffix = TRUE;
      fprintf (stdout,"Extracting files without filename extensions.\n");
      break;

    case 'q':
      state->blockAlignedOnly = TRUE;
      state->alignedblocksize = strtoul(optarg,NULL,10);
      if (state->alignedblocksize <= 0) {
	fprintf(stderr, 
		"\nERROR: Invalid blocksize for -q command line option.\n");
	exit(1);
      }
      break;

    case 'r':
      state->noSearchOverlap = TRUE;
      break;

    case 'u':
      state->useCoverageBlockmap = TRUE;
      break;

    case 'v':
      state->modeVerbose = TRUE;
      break;

    default:
      exit(1);
    }
  }
}

// full pathnames for all files used
void convertFileNames(struct scalpelState *state) {

  char fn[MAX_STRING_LENGTH];  

  realpath(state->outputdirectory,fn);
  strncpy(state->outputdirectory,fn,MAX_STRING_LENGTH);

  realpath(state->conffile,fn);
  strncpy(state->conffile,fn,MAX_STRING_LENGTH);
}


// GGRIII: for each file, build header/footer offset database first,
// then carve files based on this database.  Need to clear the
// header/footer offset database after processing of each file.

void digAllFiles(int argc, char **argv, struct scalpelState *state) {

  int i = 0, j = 0; 
  FILE* listoffiles = NULL;

  if (state->useInputFileList) {
    fprintf(stdout, "Batch mode: reading list of images from %s.",
	    state->inputFileList);
    listoffiles = fopen(state->inputFileList,"r");
    if (listoffiles == NULL) {
      fprintf(stderr, "Couldn't open file: %s -- %s\n", 
	      (*(state->inputFileList) == '\0')?"<blank>":state->inputFileList,
	      strerror(errno));
      closeFile(state->auditFile);
      exit(-1);
    }
    j = 0;
    do {
      j++;
      
      if (fgets(state->imagefile,MAX_STRING_LENGTH,listoffiles) == NULL) {
	
	fprintf(stderr,
		"Error reading line %d of %s. Skipping line.\n", 
		j,state->inputFileList);
	continue;
      }
      if(state->imagefile[strlen(state->imagefile)-1] == '\n'){
	state->imagefile[strlen(state->imagefile)-1] = '\x00';
      }

      // GGRIII: this function now *only* builds the header/footer
      // database.  Carving is handled afterward, in carveImageFile().
      
      if ((i = digImageFile(state))) {
	handleError(state,i);
      }
      else {
	// GGRIII: "digging" is now complete and header/footer database
	// has been built.  The function carveImageFile() performs
	// extraction of files based on this database.
      
	if ((i = carveImageFile(state))) {
	  handleError(state,i);
	}		
      }
    } 
    while (!feof(listoffiles));
    closeFile(listoffiles);
  }
  else {
    do {
      state->imagefile = *argv;
      
      // GGRIII: this function now *only* builds the header/footer
      // database.  Carving is handled afterward, in carveImageFile().

      if ((i = digImageFile(state))) {
	handleError(state,i);
      }		
      else {
	// GGRIII: "digging" is now complete and header/footer database
	// has been built.  The function carveImageFile() performs extraction 
	// of files based on this database.
	
	if ((i = carveImageFile(state))) {
	  handleError(state,i);
	}		
      }
      ++argv;  
    } while (*argv);
  }
}


int main(int argc, char **argv) {

  time_t starttime = time(0);
  struct scalpelState state;

  if (ldiv(SIZE_OF_BUFFER,SCALPEL_BLOCK_SIZE).rem != 0) {
    fprintf (stderr, SCALPEL_SIZEOFBUFFER_PANIC_STRING);
    exit (-1);
  }
  
#ifndef __GLIBC__
  setProgramName(argv[0]);
#endif

  fprintf (stdout,SCALPEL_BANNER_STRING);

  initializeState(argv,&state);

  processCommandLineArgs(argc,argv,&state);

  convertFileNames(&state);

  if (state.modeVerbose) {
    fprintf (stdout,"Output directory: \"%s\"\n", state.outputdirectory);
    fprintf (stdout,"Configuration file: \"%s\"\n", state.conffile);
    fprintf (stdout,"Coverage maps directory: \"%s\"\n", state.coveragedirectory);
  }

  // read configuration file
  if (readSearchSpecFile(&state)) {
    // error in configuration file, msg has already been output
    exit(-1);
  }

  setttywidth();

  argv += optind;
  if (*argv != NULL || state.useInputFileList) {
    // prepare audit file and make sure output directory is empty.
    if(openAuditFile(&state)){
      fprintf(stderr, "Aborting.\n\n");
      exit(-1);
    }
    digAllFiles(argc,argv,&state);
    closeFile(state.auditFile);
  } else {      
    usage();
    fprintf(stdout,"\nERROR: No image files specified.\n\n");
  }

#ifdef __WIN32
  fprintf (stdout,"\nScalpel is done, files carved = %I64u, elapsed = %ld seconds.\n", 
	   state.fileswritten, 
	   (int)time(0) - starttime);
#else
  fprintf (stdout,"\nScalpel is done, files carved = %llu, elapsed = %ld seconds.\n", 
	   state.fileswritten, 
	   (int)time(0) - starttime);
#endif

  return 0;
}
