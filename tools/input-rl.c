/* This file is part of GDBM, the GNU data base manager.
   Copyright (C) 2016-2022 Free Software Foundation, Inc.

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
#include <readline/readline.h>
#include <readline/history.h>

static char *pre_input_line;

static int
pre_input (void)
{
  if (pre_input_line)
    {
      rl_insert_text (pre_input_line);
      free (pre_input_line);
      pre_input_line = NULL;
      rl_redisplay ();
    }
  return 0;
}

static int
retrieve_history (char *str)
{
  char *out;
  int rc;

  rc = history_expand (str, &out);
  switch (rc)
    {
    case -1:
      yyerror (out);
      free (out);
      return 1;

    case 0:
      free (out);
      break;

    case 1:
      pre_input_line = out;
      return 1;

    case 2:
      printf ("%s\n", out);
      free (out);
      return 1;
    }
  return 0;
}

#define HISTFILE_PREFIX "~/."
#define HISTFILE_SUFFIX "_history"

static char *history_file_name;

static char *
get_history_file_name (void)
{
  if (!history_file_name)
    {
      char *hname;

      hname = emalloc (sizeof HISTFILE_PREFIX + strlen (rl_readline_name) +
		       sizeof HISTFILE_SUFFIX - 1);
      strcpy (hname, HISTFILE_PREFIX);
      strcat (hname, rl_readline_name);
      strcat (hname, HISTFILE_SUFFIX);
      history_file_name = tildexpand (hname);
      free (hname);
    }
  return history_file_name;
}
 
static char **
shell_completion (const char *text, int start, int end)
{
  char **matches;

  matches = (char **) NULL;

  /* If this word is at the start of the line, then it is a command
     to complete.  Otherwise it is the name of a file in the current
     directory. */
  if (start == 0)
    matches = rl_completion_matches (text, command_generator);

  return (matches);
}

static void
instream_readline_close (instream_t istr)
{
  if (history_file_name)
    {
      write_history (history_file_name);
      free (history_file_name);
      history_file_name = NULL;
    }
  free (istr);
}

static ssize_t
stdin_read_readline (instream_t istr, char *buf, size_t size)
{
  static char *input_line;
  static size_t input_length;
  static size_t input_off;
#define input_ptr() (input_line + input_off)
#define input_size() (input_length - input_off)
  size_t len = input_size ();
  if (!len)
    {
      if (input_line)
	{
	newline:
	  free (input_line);
	  input_line = NULL;
	  buf[0] = '\n';
	  return 1;
	}
      else
	{
	  char *prompt;
	again:
	  prompt = make_prompt ();
	  input_line = readline (prompt);
	  free (prompt);
	  if (!input_line)
	    return 0;
	  input_length = strlen (input_line);
	  input_off = 0;
	  if (input_length)
	    {
	      if (retrieve_history (input_line))
		{
		  free (input_line);
		  goto again;
		}
	    }
	  else
	    goto newline;
	  len = input_size ();
	  add_history (input_line);
	}
    }
  
  if (len > size)
    len = size;
  memcpy (buf, input_ptr (), len);
  input_off += len;

  return len;
} 

static ssize_t
instream_readline_read (instream_t istr, char *buf, size_t size)
{
  if (istr->in_inter)
    return stdin_read_readline (istr, buf, size);
  return fread (buf, 1, size, stdin);
}

static int
instream_readline_eq (instream_t a, instream_t b)
{
  return 0;
}

static int
instream_readline_history_size (instream_t istr)
{
  return history_length;
}

static const char *
instream_readline_history_get (instream_t instr, int n)
{
  if (n < history_length)
    return history_list ()[n]->line;
  return NULL;
}

instream_t
instream_readline_create (void)
{  
  struct instream *istr;

  if (isatty (fileno (stdin)))
    {
      istr = emalloc (sizeof *istr);
      istr->in_name = "stdin";
      istr->in_inter = 1;
      istr->in_read = instream_readline_read;
      istr->in_close = instream_readline_close;
      istr->in_eq = instream_readline_eq;
      istr->in_history_size = instream_readline_history_size;
      istr->in_history_get = instream_readline_history_get;
  
      /* Allow conditional parsing of the ~/.inputrc file. */
      rl_readline_name = (char *) progname;
      rl_attempted_completion_function = shell_completion;
      rl_pre_input_hook = pre_input;
      read_history (get_history_file_name ());
    }
  else
    istr = instream_stdin_create ();
  return istr;
}
