/* This file is part of GDBM test suite.
   Copyright (C) 2021-2022 Free Software Foundation, Inc.

   GDBM is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GDBM is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GDBM. If not, see <http://www.gnu.org/licenses/>.
*/
#include "autoconf.h"
#include "gdbmapp.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

static void
h_left_margin (WORDWRAP_FILE wf, char *arg)
{
  wordwrap_set_left_margin (wf, atoi (arg));
}
  
static void
h_right_margin (WORDWRAP_FILE wf, char *arg)
{
  wordwrap_set_right_margin (wf, atoi (arg));
}

static void
h_flush (WORDWRAP_FILE wf, char *arg)
{
  wordwrap_flush (wf);
}

static void
h_file (WORDWRAP_FILE wf, char *arg)
{
  FILE *fp;
  char buf[256];
  
  fp = fopen (arg, "r");
  if (!fp)
    {
      perror (arg);
      exit (1);
    }

  while (fgets (buf, sizeof buf, fp))
    {
      wordwrap_write (wf, buf, strlen (buf));
    }
  fclose (fp);
}

static void
h_newline (WORDWRAP_FILE wf, char *arg)
{
  wordwrap_putc (wf, '\n');
}

static void
h_para (WORDWRAP_FILE wf, char *arg)
{
  wordwrap_para (wf);
}


struct wwt_option
{
  char *name;
  int arg;
  void (*handler) (WORDWRAP_FILE, char *);
};

struct wwt_option wwt_options[] = {
  { "left", 1, h_left_margin },
  { "right", 1, h_right_margin },
  { "flush", 0, h_flush },
  { "file", 1, h_file },
  { "newline", 0, h_newline },
  { "para", 0, h_para },
  { NULL }
};

enum
  {
    WWT_ARG,
    WWT_OPT,
    WWT_ERR
  };

int
wwt_getopt (WORDWRAP_FILE wf, char *arg)
{
  if (arg[0] == '-')
    {
      struct wwt_option *opt;
      size_t len;
      
      if (arg[1] == '-' && arg[2] == 0)
	return WWT_ARG;

      arg++;
      len = strcspn (arg, "=");

      for (opt = wwt_options; opt->name; opt++)
	{
	  if (strlen (opt->name) == len && memcmp (opt->name, arg, len) == 0)
	    {
	      if (opt->arg && arg[len])
		arg += len + 1;
	      else if (!opt->arg && !arg[len])
		arg = NULL;
	      else
		continue;
	      opt->handler (wf, arg);
	      return WWT_OPT;
	    }
	}
      return WWT_ERR;
    }
  return WWT_ARG;
}

int
main (int argc, char **argv)
{
  WORDWRAP_FILE wf;
  int escape = 0;

  setlocale (LC_ALL, "");
  if ((wf = wordwrap_fdopen (1)) == NULL)
    {
      perror ("wordwrap_open");
      exit (1);
    }

  while (--argc)
    {
      char *arg = *++argv;

      if (escape)
	{
	  wordwrap_write (wf, arg, strlen (arg));
	  if (argc > 1)
	    wordwrap_write (wf, " ", 1);
	  escape = 0;
	  continue;
	}
      
      switch (wwt_getopt (wf, arg))
	{
	case WWT_ARG:
	  if (strcmp (arg, "--") == 0)
	    escape = 1;
	  else
	    {
	      wordwrap_write (wf, arg, strlen (arg));
	      if (argc > 1)
		wordwrap_write (wf, " ", 1);
	    }
	  break;

	case WWT_OPT:
	  break;

	case WWT_ERR:
	  fprintf (stderr, "unrecognized option: %s\n", arg);
	}
    }

  wordwrap_close (wf);
}
