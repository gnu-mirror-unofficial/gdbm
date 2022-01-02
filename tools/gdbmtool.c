/* This file is part of GDBM, the GNU data base manager.
   Copyright (C) 1990-2022 Free Software Foundation, Inc.

   GDBM is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GDBM is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GDBM. If not, see <http://www.gnu.org/licenses/>.    */

#include "gdbmtool.h"
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <stdarg.h>
#ifdef HAVE_LOCALE_H
# include <locale.h>
#endif

static void
source_rcfile (void)
{
  instream_t istr = NULL;
  
  if (access (GDBMTOOLRC, R_OK) == 0)
    {
      istr = instream_file_create (GDBMTOOLRC);
    }
  else
    {
      char *fname;
      char *p = getenv ("HOME");
      if (!p)
	{
	  struct passwd *pw = getpwuid (getuid ());
	  if (!pw)
	    {
	      terror (_("cannot find home directory"));
	      return;
	    }
	  p = pw->pw_dir;
	}
      fname = mkfilename (p, GDBMTOOLRC, NULL);
      if (access (fname, R_OK) == 0)
	{
	  istr = instream_file_create (GDBMTOOLRC);
	}
      free (fname);
    }

  if (istr)
    {
      if (input_context_push (istr))
	exit (EXIT_FATAL);
      yyparse ();
    }
}

#if GDBM_DEBUG_ENABLE
void
debug_printer (char const *fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
}
#endif


char *parseopt_program_doc = N_("examine and/or modify a GDBM database");
char *parseopt_program_args = N_("DBFILE [COMMAND [ARG ...]]");

enum {
  OPT_LEX_TRACE = 256,
  OPT_GRAM_TRACE
};

struct gdbm_option optab[] = {
  { 'b', "block-size", N_("SIZE"), N_("set block size") },
  { 'c', "cache-size", N_("SIZE"), N_("set cache size") },
  { 'f', "file",       N_("FILE"), N_("read commands from FILE") },
  { 'g', NULL, "FILE", NULL, PARSEOPT_HIDDEN },
  { 'l', "no-lock",    NULL,       N_("disable file locking") },
  { 'm', "no-mmap",    NULL,       N_("do not use mmap") },
  { 'n', "newdb",      NULL,       N_("create database") },
  { 'N', "norc",       NULL,       N_("do not read .gdbmtoolrc file") },
  { 'r', "read-only",  NULL,       N_("open database in read-only mode") },
  { 's', "synchronize", NULL,      N_("synchronize to disk after each write") },
  { 'q', "quiet",      NULL,       N_("don't print initial banner") },
  { 'd', "db-descriptor",
    /* TRANSLATORS: File Descriptor. */
    N_("FD"),
    N_("open database at the given file descriptor") },
  { 'x', "extended",   NULL,       N_("extended format (numsync)") },
  {   0, "numsync",    NULL,       NULL, PARSEOPT_ALIAS },
  { 't', "trace",      NULL,       N_("enable trace mode") },
  { 'T', "timing",     NULL,       N_("print timing after each command") },
#if GDBMTOOL_DEBUG    
  { OPT_LEX_TRACE, "lex-trace", NULL, N_("enable lexical analyzer traces") },
  { OPT_GRAM_TRACE, "gram-trace", NULL, N_("enable grammar traces") },
#endif  
  { 0 }
};

#ifdef WITH_READLINE
# define instream_default_create   instream_readline_create
#else
# define instream_default_create   instream_stdin_create
#endif

struct gdbmtool_closure
{
  int argc;
  char **argv;
};

static int
gdbmtool_init (void *data, instream_t *pinstr)
{
  struct gdbmtool_closure *clos = data;
  int argc = clos->argc;
  char **argv = clos->argv;
  int opt;
  int bv;
  int norc = 0;
  char *source = NULL;
  instream_t input = NULL;
  
  for (opt = parseopt_first (argc, argv, optab);
       opt != EOF;
       opt = parseopt_next ())
    switch (opt)
      {
      case 'd':
	if (variable_set ("fd", VART_STRING, optarg) != VAR_OK)
	  {
	    terror (_("invalid file descriptor: %s"), optarg);
	    exit (EXIT_USAGE);
	  }
	break;
	
      case 'f':
	source = optarg;
	break;
	
      case 'l':
	bv = 0;
	variable_set ("lock", VART_BOOL, &bv);
	break;

      case 'm':
	bv = 0;
	variable_set ("mmap", VART_BOOL, &bv);
	break;

      case 's':
	bv = 1;
	variable_set ("sync", VART_BOOL, &bv);
	break;
	
      case 'r':
	variable_set ("open", VART_STRING, "readonly");
	break;
	
      case 'n':
	variable_set ("open", VART_STRING, "newdb");
	break;

      case 'N':
	norc = 1;
	break;
	
      case 'c':
	variable_set ("cachesize", VART_STRING, optarg);
	break;
	
      case 'b':
	variable_set ("blocksize", VART_STRING, optarg);
	break;

      case 'g':
	variable_set ("filename", VART_STRING, optarg);
	break;

      case 't':
	bv = 1;
	variable_set ("trace", VART_BOOL, &bv);
	break;

      case 'T':
	bv = 1;
	variable_set ("timing", VART_BOOL, &bv);
	break;
	
      case 'q':
	bv = 1;
	variable_set ("quiet", VART_BOOL, &bv);
	break;

      case 'x':
	variable_set ("format", VART_STRING, "numsync");
	break;
	
      case OPT_LEX_TRACE:
	lex_trace (1);
	break;

      case OPT_GRAM_TRACE:
	gram_trace (1);
	break;
	
      default:
	if (optopt == 0)
	  terror (_("unknown option %s; try `%s -h' for more info"),
		  argv[optind-1], progname);
	else
	  terror (_("unknown option %c; try `%s -h' for more info"),
		  optopt, progname);
	exit (EXIT_USAGE);
      }
  
  argc -= optind;
  argv += optind;
  
  if (source && strcmp (source, "-"))
    {
      input = instream_file_create (source);
      if (!input)
	exit (1);
    }
  
  if (argc >= 1)
    {
      variable_set ("filename", VART_STRING, argv[0]);
      argc--;
      argv++;
      if (argc)
	{
	  if (input)
	    {
	      terror (_("--file and command cannot be used together"));
	      exit (EXIT_USAGE);
	    }
	  input = instream_argv_create (argc, argv);
	  if (!input)
	    exit (1);
	}
    }

  if (!norc)
    source_rcfile ();

  if (!input)
    input = instream_default_create ();

  *pinstr = input;

  return 0;
}
  
int
main (int argc, char *argv[])
{
  struct gdbmtool_closure closure;
  
  set_progname (argv[0]);
#if GDBM_DEBUG_ENABLE
  gdbm_debug_printer = debug_printer;
#endif

#ifdef HAVE_SETLOCALE
  setlocale (LC_ALL, "");
#endif
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  closure.argc = argc;
  closure.argv = argv;

  return gdbmshell_run (gdbmtool_init, &closure);
}
