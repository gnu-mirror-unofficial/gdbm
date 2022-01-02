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

#include "autoconf.h"
#include "gdbmdefs.h"
#include "gdbm.h"
#include "gdbmapp.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#ifndef GDBM_ARG_UNUSED
# define GDBM_ARG_UNUSED __attribute__ ((__unused__))
#endif

#ifndef GDBM_PRINTFLIKE
# define GDBM_PRINTFLIKE(fmt,narg) \
  __attribute__ ((__format__ (__printf__, fmt, narg)))
#endif

/* Position in input file */
struct point
{
  char *file;             /* file name */
  unsigned line;          /* line number */  
  unsigned col;           /* column number */ 
};

/* Location in input file */
struct locus
{
  struct point beg, end;
};

typedef struct locus gdbm_yyltype_t;
 
#define YYLTYPE gdbm_yyltype_t 

#define YYLLOC_DEFAULT(Current, Rhs, N)			      \
  do							      \
    {							      \
      if (N)						      \
	{						      \
	  (Current).beg = YYRHSLOC(Rhs, 1).beg;		      \
	  (Current).end = YYRHSLOC(Rhs, N).end;		      \
	}						      \
      else						      \
	{						      \
	  (Current).beg = YYRHSLOC(Rhs, 0).end;		      \
	  (Current).end = (Current).beg;		      \
	}						      \
    }							      \
  while (0)

#define YY_LOCATION_PRINT(File, Loc) locus_print (File, &(Loc))

void locus_print (FILE *fp, struct locus const *loc);
void vlerror (struct locus *loc, const char *fmt, va_list ap);
void lerror (struct locus *loc, const char *fmt, ...)
	   GDBM_PRINTFLIKE (2, 3);

void terror (const char *fmt, ...)
	   GDBM_PRINTFLIKE (1, 2);
void dberror (char const *fmt, ...)
	   GDBM_PRINTFLIKE (1, 2);

char *make_prompt (void);

#define GDBMTOOLRC ".gdbmtoolrc"
#define GDBMTOOL_DEFFILE "junk.gdbm"

typedef struct instream *instream_t;

struct instream
{
  char *in_name;           /* Input stream name */
  int in_inter;            /* True if this is an interactive stream */
  ssize_t (*in_read) (instream_t, char*, size_t);
                           /* Read from stream */
  void (*in_close) (instream_t);
                           /* Close the stream */
  int (*in_eq) (instream_t, instream_t);
                           /* Return true if both streams refer to the
			      same input */
  int (*in_history_size) (instream_t);
                           /* Return size of the history buffer (entries) */
  const char *(*in_history_get) (instream_t, int);
                           /* Get Nth line from the history buffer */
};

static inline char const *
instream_name (instream_t in)
{
  return in->in_name;
}

static inline ssize_t
instream_read (instream_t in, char *buf, size_t size)
{
  return in->in_read (in, buf, size);
}

static inline void
instream_close (instream_t in)
{
  in->in_close (in);
}

static inline int
instream_interactive (instream_t in)
{
  return in->in_inter;
}

static inline int
instream_eq (instream_t a, instream_t b)
{
  return a->in_eq (a, b);
}

static inline int
instream_history_size (instream_t in)
{
  return in->in_history_size ? in->in_history_size (in) : -1;
}

static inline const char *
instream_history_get (instream_t in, int n)
{
  return in->in_history_get ? in->in_history_get (in, n) : NULL;
}

instream_t instream_stdin_create (void);
instream_t instream_argv_create (int argc, char **argv);
instream_t instream_file_create (char const *name);
instream_t instream_null_create (void);
#ifdef WITH_READLINE
instream_t instream_readline_create (void);
#endif

int interactive (void);
int input_context_push (instream_t);
int input_context_pop (void);
void input_context_drain (void);
int input_history_size (void);
const char *input_history_get (int n);
const char *input_stream_name (void);


void print_prompt_at_bol (void);
char *command_generator (const char *text, int state);


struct slist
{
  struct slist *next;
  char *str;
};

struct slist *slist_new (char const *s);
struct slist *slist_new_s (char *s);
struct slist *slist_new_l (char const *s, size_t l);
void slist_free (struct slist *);
void slist_insert (struct slist **where, struct slist *what);

#define KV_STRING 0
#define KV_LIST   1

struct kvpair
{
  struct kvpair *next;
  int type;
  struct locus loc;
  char *key;
  union
  {
    char *s;
    struct slist *l;
  } val;
};

struct kvpair *kvpair_string (struct locus *loc, char *val);
struct kvpair *kvpair_list (struct locus *loc, struct slist *s);
struct kvpair *kvlist_find (struct kvpair *kv, char const *tag);
void kvlist_free (struct kvpair *kvp);


#define GDBM_ARG_STRING 0
#define GDBM_ARG_DATUM  1
#define GDBM_ARG_KVPAIR 2
#define GDBM_ARG_MAX    3

/* Argument to a command handler */
struct gdbmarg
{
  struct gdbmarg *next;
  int type;
  int ref;
  struct locus loc;
  union
  {
    char *string;
    datum dat;
    struct kvpair *kvpair;
  } v;
};

/* List of arguments */
struct gdbmarglist
{
  struct gdbmarg *head, *tail;
};

struct command_param
{
  size_t argc;
  size_t argmax;
  struct gdbmarg **argv;
  struct gdbmarg *vararg;
};
  
#define HANDLER_PARAM_INITIALIZER { 0, 0, NULL, NULL }

#define PARAM_STRING(p,n) ((p)->argv[n]->v.string)
#define PARAM_DATUM(p,n)  ((p)->argv[n]->v.dat)
#define PARAM_KVPAIR(p,n) ((p)->argv[n]->v.kvpair)

void gdbmarglist_init (struct gdbmarglist *, struct gdbmarg *);
void gdbmarglist_add (struct gdbmarglist *, struct gdbmarg *);
void gdbmarglist_free (struct gdbmarglist *lst);

struct gdbmarg *gdbmarg_string (char *, struct locus *);
struct gdbmarg *gdbmarg_datum (datum *, struct locus *);
struct gdbmarg *gdbmarg_kvpair (struct kvpair *kvl, struct locus *);

int gdbmarg_free (struct gdbmarg *arg);
void gdbmarg_destroy (struct gdbmarg **parg);

struct command_environ
{
  FILE *fp;
  void *data;
};

#define COMMAND_ENVIRON_INITIALIZER { NULL, NULL }

struct command;
int command_lookup (const char *str, struct locus *loc, struct command **pcmd);

int run_command (struct command *cmd, struct gdbmarglist *arglist);
int run_last_command (void);

struct xdatum;
void xd_expand (struct xdatum *xd, size_t size);
void xd_store (struct xdatum *xd, void *val, size_t size);


struct datadef
{
  char *name;
  int size;
  int (*format) (FILE *, void *ptr, int size);
  int (*scan) (struct xdatum *xd, char *str);
};

struct datadef *datadef_lookup (const char *name);

struct field
{
  struct datadef *type;
  int dim;
  char *name;
};

#define FDEF_FLD 0
#define FDEF_OFF 1
#define FDEF_PAD 2

struct dsegm
{
  struct dsegm *next;
  int type;
  union
  {
    int n;
    struct field field;
  } v;
};

struct dsegm *dsegm_new (int type);
struct dsegm *dsegm_new_field (struct datadef *type, char *id, int dim);
void dsegm_list_free (struct dsegm *dp);
struct dsegm *dsegm_list_find (struct dsegm *dp, char const *name);

#define DS_KEY     0
#define DS_CONTENT 1
#define DS_MAX     2

extern struct dsegm *dsdef[];

#define VART_STRING 0
#define VART_BOOL   1
#define VART_INT    2

enum
  {
    VAR_OK,           /* operation succeeded */
    VAR_ERR_NOTSET,   /* Only for variable_get: variable is not set */ 
    VAR_ERR_NOTDEF,   /* no such variable */ 
    VAR_ERR_BADTYPE,  /* variable cannot be coerced to the requested type
		 	 (software error) */
    VAR_ERR_BADVALUE, /* Only for variable_set: the value is not valid for
			 this variable. */
    VAR_ERR_GDBM,     /* GDBM error */
  };


int variable_set (const char *name, int type, void *val);
int variable_get (const char *name, int type, void **val);
int variable_unset(const char *name);
int variable_is_set (const char *name);
int variable_is_true (const char *name);
void variable_print_all (FILE *fp);
static inline int
variable_has_errno (char *varname, int e)
{
  return variable_get (varname, VART_INT, (void**)&e) == VAR_OK && e == 1;
}
static inline int
gdbm_error_is_masked (int e)
{
  return variable_has_errno ("errormask", e);
}

int unescape (int c);
int escape (int c);
void begin_def (void);
void end_def (void);

int yylex (void);
int yylex_destroy (void);
void yyerror (char const *s);
#define YYERROR_IS_DECLARED 1
int yyparse (void);

void lex_trace (int n);
void gram_trace (int n);

void datum_format (FILE *fp, datum const *dat, struct dsegm *ds);
int datum_scan (datum *dat, struct dsegm *ds, struct kvpair *kv);
void dsprint (FILE *fp, int what, struct dsegm *ds);

char *mkfilename (const char *dir, const char *file, const char *suf);
char *tildexpand (char *s);
int vgetyn (const char *prompt, va_list ap);
int getyn (const char *prompt, ...) GDBM_PRINTFLIKE (1, 2);

int getnum (int *pnum, char *arg, char **endp);

int gdbmshell (instream_t input);
int gdbmshell_run (int (*init) (void *, instream_t *), void *data);

int gdbmshell_setopt (char *name, int opt, int val);

void variables_init (void);
void variables_free (void);

