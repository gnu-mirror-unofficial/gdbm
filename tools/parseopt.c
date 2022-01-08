/* This file is part of GDBM, the GNU data base manager.
   Copyright (C) 2011-2022 Free Software Foundation, Inc.

   GDBM is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GDBM is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GDBM. If not, see <http://www.gnu.org/licenses/>.   */

# include "autoconf.h"
# include "gdbm.h"
# include "gdbmapp.h"
# include "gdbmdefs.h"
# include <stdio.h>
# include <stdarg.h>
# include <errno.h>
# include <string.h>
# include <ctype.h>
# ifdef HAVE_GETOPT_H
#  include <getopt.h>
# endif

static int argc;
static char **argv;

static struct gdbm_option *option_tab;
static size_t option_count;
static size_t option_max;
static char *short_options;
static size_t short_option_count;
static size_t short_option_max;
#ifdef HAVE_GETOPT_LONG
static struct option *long_options;
static size_t long_option_count;
static size_t long_option_max;
#endif

#define OPT_USAGE -2

struct gdbm_option parseopt_default_options[] = {
  { 0, NULL, NULL, "" },
  { 'h', "help", NULL, N_("give this help list") },
  { 'V', "version", NULL, N_("print program version") },
  { OPT_USAGE, "usage", NULL, N_("give a short usage message") },
  { 0 }
};

#define OPT_END(opt) \
  ((opt)->opt_short == 0 && (opt)->opt_long == 0 && (opt)->opt_descr == NULL)
#define IS_OPTION(opt) \
  ((opt)->opt_short || (opt)->opt_long)
#define IS_GROUP_HEADER(opt)			\
  (!IS_OPTION(opt) && (opt)->opt_descr)
#define IS_VALID_SHORT_OPTION(opt) \
  ((opt)->opt_short > 0 && (opt)->opt_short < 127 && \
   isalnum ((opt)->opt_short))
#define IS_VALID_LONG_OPTION(opt) \
  ((opt)->opt_long != NULL)
  

static int
optcmp (const void *a, const void *b)
{
  struct gdbm_option const *ap = (struct gdbm_option const *)a;
  struct gdbm_option const *bp = (struct gdbm_option const *)b;

  while (ap->opt_flags & PARSEOPT_ALIAS)
    ap--;
  while (bp->opt_flags & PARSEOPT_ALIAS)
    bp--;
  
  if (IS_VALID_SHORT_OPTION(ap) && IS_VALID_SHORT_OPTION(bp))
    return ap->opt_short - bp->opt_short;
  if (IS_VALID_LONG_OPTION(ap) && IS_VALID_LONG_OPTION(bp))
    return strcmp (ap->opt_long, bp->opt_long);
  if (IS_VALID_LONG_OPTION(ap))
    return 1;
  return -1;
}

static void
sort_options (int start, int count)
{
  qsort (option_tab + start, count, sizeof (option_tab[0]), optcmp);
}

static size_t
sort_group (size_t start)
{
  size_t i;
  
  for (i = start; i < option_count && !IS_GROUP_HEADER (&option_tab[i]); i++)
    ;
  sort_options (start, i - start);
  return i + 1;
}

static void
sort_all_options (void)
{
  size_t start;

  /* Ensure sane start of options.  This is necessary because optcmp backs up
     until it finds an element with cleared PARSEOPT_ALIAS flag bit. */
  option_tab[0].opt_flags &= PARSEOPT_ALIAS;
  for (start = 0; start < option_count; )
    {
      if (IS_GROUP_HEADER (&option_tab[start]))
	start = sort_group (start + 1);
      else 
	start = sort_group (start);
    }
}

static void
add_options (struct gdbm_option *options)
{
  size_t optcnt = 0;
  size_t argcnt = 0;
  size_t count = 0;
  struct gdbm_option *opt;
  
  for (opt = options; !OPT_END(opt); opt++)
    {
      count++;
      if (IS_OPTION(opt))
	{
	  optcnt++;
	  if (opt->opt_arg)
	    argcnt++;
	}
    }

  if (option_count + count + 1 > option_max)
    {
      option_max = option_count + count + 1;
      option_tab = erealloc (option_tab,
			     sizeof (option_tab[0]) * option_max);
    }
  
#ifdef HAVE_GETOPT_LONG
  if (long_option_count + optcnt + 1 > long_option_max)
    {
      long_option_max = long_option_count + optcnt + 1;
      long_options = erealloc (long_options,
			       sizeof (long_options[0]) * long_option_max);
    }
#endif
  if (short_option_count + optcnt + argcnt + 1 > short_option_max)
    {
      short_option_max = short_option_count + optcnt + argcnt + 1;
      short_options = erealloc (short_options,
				sizeof (short_options[0]) * short_option_max);
    }

  for (opt = options; !OPT_END(opt); opt++)
    {
      option_tab[option_count++] = *opt;
      if (!IS_OPTION (opt))
	continue;
      if (IS_VALID_SHORT_OPTION (opt))
	{
	  short_options[short_option_count++] = opt->opt_short;
	  if (opt->opt_arg)
	    short_options[short_option_count++] = ':';
	}
#ifdef HAVE_GETOPT_LONG
      if (IS_VALID_LONG_OPTION (opt))
	{
	  long_options[long_option_count].name = opt->opt_long;
	  long_options[long_option_count].has_arg = opt->opt_arg != NULL;
	  long_options[long_option_count].flag = NULL;
	  long_options[long_option_count].val = opt->opt_short;
	  long_option_count++;
	}
#endif
    }
  short_options[short_option_count] = 0;
#ifdef HAVE_GETOPT_LONG
  memset (&long_options[long_option_count], 0,
	  sizeof long_options[long_option_count]);
#endif
}

void
parseopt_free (void)
{
  free (option_tab);
  option_tab = NULL;
  free (short_options);
  short_options = NULL;
  short_option_count = short_option_max = 0;
#ifdef HAVE_GETOPT_LONG
  free (long_options);
  long_options = NULL;
  long_option_count = long_option_max = 0;
#endif
}

int
parseopt_first (int pc, char **pv, struct gdbm_option *opts)
{
  parseopt_free ();
  add_options (opts);
  add_options (parseopt_default_options);
  opterr = 0;
  argc = pc;
  argv = pv;
  return parseopt_next ();
}

static unsigned short_opt_col = 2;
static unsigned long_opt_col = 6;
static unsigned doc_opt_col = 2;    /* FIXME: Not used: there are no doc
				       options in this implementation */
static unsigned header_col = 1;
static unsigned opt_doc_col = 29;
static unsigned usage_indent = 12;
static unsigned rmargin = 79;

static unsigned dup_args = 0;
static unsigned dup_args_note = 1;

enum usage_var_type
  {
    usage_var_column,
    usage_var_bool
  };

struct usage_var_def
{
  char *name;
  unsigned *valptr;
  enum usage_var_type type;
};

static struct usage_var_def usage_var[] = {
  { "short-opt-col", &short_opt_col, usage_var_column },
  { "header-col",    &header_col,    usage_var_column },
  { "opt-doc-col",   &opt_doc_col,   usage_var_column },
  { "usage-indent",  &usage_indent,  usage_var_column },
  { "rmargin",       &rmargin,       usage_var_column },
  { "dup-args",      &dup_args,      usage_var_bool },
  { "dup-args-note", &dup_args_note, usage_var_bool },
  { "long-opt-col",  &long_opt_col,  usage_var_column },
  { "doc-opt-col",   &doc_opt_col,   usage_var_column },
  { NULL }
};

static void
set_usage_var (char const *text, char **end)
{
  struct usage_var_def *p;
  int boolval = 1;
  char const *prog_name = parseopt_program_name ? parseopt_program_name : progname;
  size_t len = strcspn (text, ",=");
  char *endp;

  if (len > 3 && memcmp (text, "no-", 3) == 0)
    {
      text += 3;
      len -= 3;
      boolval = 0;
    }

  for (p = usage_var; p->name; p++)
    {
      if (strlen (p->name) == len && memcmp (p->name, text, len) == 0)
	break;
    }

  endp = (char*) text + len;
  if (p)
    {
      if (p->type == usage_var_bool)
	{
	  if (*endp == '=')
	    {
	      if (prog_name)
		fprintf (stderr, "%s: ", prog_name);
	      fprintf (stderr,
		       _("error in ARGP_HELP_FMT: improper usage of [no-]%s\n"),
		       p->name);
	      endp = strchr (text + len, ',');
	    }
	  else
	    *p->valptr = boolval;
	}
      else if (*endp == '=')
	{
	  unsigned long val;
	  
	  errno = 0;
	  val = strtoul (text + len + 1, &endp, 10);
	  if (errno || (*endp && *endp != ','))
	    {
	      if (prog_name)
		fprintf (stderr, "%s: ", prog_name);
	      fprintf (stderr,
		       _("error in ARGP_HELP_FMT: bad value for %s"),
		       p->name);
	      if (endp)
		{
		  fprintf (stderr, _(" (near %s)"), endp);
		}
	      fputc ('\n', stderr);
	    }
	  else if (val > UINT_MAX)
	    {
	      if (prog_name)
		fprintf (stderr, "%s: ", prog_name);
	      fprintf (stderr,
		       _("error in ARGP_HELP_FMT: %s value is out of range\n"),
		       p->name);
	    }
	  else
	    *p->valptr = val;
	}
      else
	{
	  if (prog_name)
	    fprintf (stderr, "%s: ", prog_name);
	  fprintf (stderr,
		   _("%s: ARGP_HELP_FMT parameter requires a value\n"),
		   p->name);
	}
    }
  else
    {
      if (prog_name)
	fprintf (stderr, "%s: ", prog_name);
      fprintf (stderr,
	       _("%s: Unknown ARGP_HELP_FMT parameter\n"),
	       text);
    }
  *end = endp;
}

static void
init_usage_vars (void)
{
  char *fmt, *p;
  
  fmt = getenv ("ARGP_HELP_FMT");
  if (!fmt || !*fmt)
    return;

  while (1)
    {
      set_usage_var (fmt, &p);
      if (*p == 0)
	break;
      else if (*p == ',')
	p++;
      else
	{
	  char const *prog_name = parseopt_program_name ? parseopt_program_name : progname;
	  if (prog_name)
	    fprintf (stderr, "%s: ", prog_name);
	  fprintf (stderr, _("ARGP_HELP_FMT: missing delimiter near %s\n"),
		   p);
	  break;
	}
      fmt = p;
    }
}

char *parseopt_program_name;
const char *program_bug_address = "<" PACKAGE_BUGREPORT ">";
void (*parseopt_help_hook) (FILE *stream);

static int argsused;

static int
print_arg (WORDWRAP_FILE wf, struct gdbm_option *opt, int delim)
{
  if (opt->opt_arg)
    {
      argsused = 1;
      return wordwrap_printf (wf, "%c%s", delim,
			      opt->opt_arg[0] ? gettext (opt->opt_arg) : "");
    }
  return 0;
}

size_t
print_option (WORDWRAP_FILE wf, size_t num)
{
  struct gdbm_option *opt = option_tab + num;
  size_t next, i;
  int delim;
  int w;
  
  if (IS_GROUP_HEADER (opt))
    {
      wordwrap_set_left_margin (wf, header_col);
      wordwrap_set_right_margin (wf,  rmargin);
      if (opt->opt_descr[0])
	{
	  wordwrap_putc (wf, '\n');
	  wordwrap_puts (wf, gettext (opt->opt_descr));
	  wordwrap_putc (wf, '\n');
	}
      wordwrap_putc (wf, '\n');
      return num + 1;
    }

  /* count aliases */
  for (next = num + 1;
       next < option_count && option_tab[next].opt_flags & PARSEOPT_ALIAS;
       next++);

  if (opt->opt_flags & PARSEOPT_HIDDEN)
    return next;

  wordwrap_set_left_margin (wf, short_opt_col);
  w = 0;
  for (i = num; i < next; i++)
    {
      if (IS_VALID_SHORT_OPTION (&option_tab[i]))
	{
	  if (w)
	    wordwrap_write (wf, ", ", 2);
	  wordwrap_printf (wf, "-%c", option_tab[i].opt_short);
	  delim = ' ';
	  if (dup_args)
	    print_arg (wf, opt, delim);
	  w = 1;
	}
    }
  
#ifdef HAVE_GETOPT_LONG
  for (i = num; i < next; i++)
    {
      if (IS_VALID_LONG_OPTION (&option_tab[i]))
	{
	  if (w)
	    wordwrap_write (wf, ", ", 2);
	  wordwrap_set_left_margin (wf, long_opt_col);
	  w = 0;
	  break;
	}
    }
  for (; i < next; i++)
    {
      if (IS_VALID_LONG_OPTION (&option_tab[i]))
	{
	  if (w)
	    wordwrap_write (wf, ", ", 2);
	  wordwrap_printf (wf, "--%s", option_tab[i].opt_long);
	  delim = '=';
	  if (dup_args)
	    print_arg (wf, opt, delim);
	  w = 1;
	}
    }
#endif
  if (!dup_args)
    print_arg (wf, opt, delim);
  
  wordwrap_set_left_margin (wf, opt_doc_col);
  if (opt->opt_descr[0])
    wordwrap_puts (wf, gettext (opt->opt_descr));
 
  return next;
}

void
parseopt_print_help (void)
{
  unsigned i;
  WORDWRAP_FILE wf;
  
  argsused = 0;

  init_usage_vars ();

  wf = wordwrap_fdopen (1);
  
  wordwrap_printf (wf, "%s %s [%s]... %s\n", _("Usage:"),
		   parseopt_program_name ? parseopt_program_name : progname,
		   _("OPTION"),
		   (parseopt_program_args && parseopt_program_args[0])
		     ? gettext (parseopt_program_args) : "");

  wordwrap_set_right_margin (wf, rmargin);
  if (parseopt_program_doc && parseopt_program_doc[0])
    wordwrap_puts (wf, gettext (parseopt_program_doc));
  wordwrap_para (wf);
  
  sort_all_options ();
  for (i = 0; i < option_count; )
    {
      i = print_option (wf, i);
    }
  wordwrap_para (wf);

#ifdef HAVE_GETOPT_LONG
  if (argsused && dup_args_note)
    {
      wordwrap_set_left_margin (wf, 0);
      wordwrap_set_right_margin (wf, rmargin);
      wordwrap_puts (wf, _("Mandatory or optional arguments to long options are also mandatory or optional for any corresponding short options."));
      wordwrap_para (wf);
    }
#endif
  if (parseopt_help_hook)
    parseopt_help_hook (stdout);//FIXME

  wordwrap_set_left_margin (wf, 0);
  wordwrap_set_right_margin (wf, rmargin);
 /* TRANSLATORS: The placeholder indicates the bug-reporting address
    for this package.  Please add _another line_ saying
    "Report translation bugs to <...>\n" with the address for translation
    bugs (typically your translation team's web or email address).  */
  wordwrap_printf (wf, _("Report bugs to %s.\n"), program_bug_address);
  
#ifdef PACKAGE_URL
  wordwrap_printf (wf, _("%s home page: <%s>\n"), PACKAGE_NAME, PACKAGE_URL);
#endif
}

static int
cmpidx_short (const void *a, const void *b)
{
  unsigned const *ai = (unsigned const *)a;
  unsigned const *bi = (unsigned const *)b;

  return option_tab[*ai].opt_short - option_tab[*bi].opt_short;
}
  
#ifdef HAVE_GETOPT_LONG
static int
cmpidx_long (const void *a, const void *b)
{
  unsigned const *ai = (unsigned const *)a;
  unsigned const *bi = (unsigned const *)b;
  struct gdbm_option const *ap = option_tab + *ai;
  struct gdbm_option const *bp = option_tab + *bi;
  return strcmp (ap->opt_long, bp->opt_long);
}
#endif

void
print_usage (void)
{
  WORDWRAP_FILE wf;
  unsigned i;
  unsigned *idxbuf;
  unsigned nidx;

  init_usage_vars ();
  
  idxbuf = ecalloc (option_count, sizeof (idxbuf[0]));

  wf = wordwrap_fdopen (1);
  wordwrap_set_right_margin (wf, rmargin);
  wordwrap_printf (wf, "%s %s ", _("Usage:"),
		   parseopt_program_name ? parseopt_program_name : progname);
  wordwrap_next_left_margin (wf, usage_indent);
  
  /* Print a list of short options without arguments. */
  for (i = nidx = 0; i < option_count; i++)
    if (IS_VALID_SHORT_OPTION (&option_tab[i]) && !option_tab[i].opt_arg)
      idxbuf[nidx++] = i;

  if (nidx)
    {
      qsort (idxbuf, nidx, sizeof (idxbuf[0]), cmpidx_short);

      wordwrap_puts (wf, "[-");
      for (i = 0; i < nidx; i++)
	{
	  wordwrap_putc (wf, option_tab[idxbuf[i]].opt_short);
	}
      wordwrap_putc (wf, ']');
    }

  /* Print a list of short options with arguments. */
  for (i = nidx = 0; i < option_count; i++)
    {
      if (IS_VALID_SHORT_OPTION (&option_tab[i]) && option_tab[i].opt_arg)
	idxbuf[nidx++] = i;
    }

  if (nidx)
    {
      qsort (idxbuf, nidx, sizeof (idxbuf[0]), cmpidx_short);
    
      for (i = 0; i < nidx; i++)
	{
	  struct gdbm_option *opt = option_tab + idxbuf[i];
	  const char *arg = gettext (opt->opt_arg);

	  wordwrap_word_start (wf);
	  wordwrap_puts (wf, " [-");
	  wordwrap_putc (wf, opt->opt_short);
	  wordwrap_putc (wf, ' ');
	  wordwrap_puts (wf, arg);
	  wordwrap_putc (wf, ']');
	  wordwrap_word_end (wf);
	}
    }
  
#ifdef HAVE_GETOPT_LONG
  /* Print a list of long options */
  for (i = nidx = 0; i < option_count; i++)
    {
      if (IS_VALID_LONG_OPTION (&option_tab[i]))
	idxbuf[nidx++] = i;
    }

  if (nidx)
    {
      qsort (idxbuf, nidx, sizeof (idxbuf[0]), cmpidx_long);
	
      for (i = 0; i < nidx; i++)
	{
	  struct gdbm_option *opt = option_tab + idxbuf[i];
	  const char *arg = opt->opt_arg ? gettext (opt->opt_arg) : NULL;

	  wordwrap_word_start (wf);
	  wordwrap_write (wf, " [--", 4);
	  wordwrap_puts (wf, opt->opt_long);
	  if (opt->opt_arg)
	    {
	      wordwrap_putc (wf, '=');
	      wordwrap_write (wf, arg, strlen (arg));
	    }
	  wordwrap_putc (wf, ']');
	  wordwrap_word_end (wf);
	}
    }
#endif
  wordwrap_close (wf);
  free (idxbuf);
}

const char version_etc_copyright[] =
  /* Do *not* mark this string for translation.  First %s is a copyright
     symbol suitable for this locale, and second %s are the copyright
     years.  */
  "Copyright %s %s Free Software Foundation, Inc";

const char license_text[] =
  "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>\n"
  "This is free software: you are free to change and redistribute it.\n"
  "There is NO WARRANTY, to the extent permitted by law.";

void
print_version_only (void)
{
  printf ("%s (%s) %s\n",
	   parseopt_program_name ? parseopt_program_name : progname,
	   PACKAGE_NAME,
	   PACKAGE_VERSION);
  /* TRANSLATORS: Translate "(C)" to the copyright symbol
     (C-in-a-circle), if this symbol is available in the user's
     locale.  Otherwise, do not translate "(C)"; leave it as-is.  */
  printf (version_etc_copyright, _("(C)"), "2011-2022");
  putchar ('\n');
  puts (license_text);
  putchar ('\n');
}


static int
handle_option (int c)
{
  switch (c)
    {
    case 'h':
      parseopt_print_help ();
      exit (0);
      
    case 'V':
      print_version_only ();
      exit (0);
      
    case OPT_USAGE:
      print_usage ();
      exit (0);
      
    default:
      break;
    }
  return 0;
}

int
parseopt_next (void)
{
  int rc;
  
  do
    {
#ifdef HAVE_GETOPT_LONG
      rc = getopt_long (argc, argv, short_options, long_options, NULL);
#else
      rc = getopt (argc, argv, short_options);
#endif
    }
  while (handle_option (rc));

  if (rc == EOF || rc == '?')
    parseopt_free ();
  
  return rc;
}
