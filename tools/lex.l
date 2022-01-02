%{
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
#include "gram.h"

struct context                /* Input context */
{
  struct context *parent;     /* Pointer to the parent context */
  struct locus locus;         /* Locus */
  struct point point;
  YY_BUFFER_STATE buf;        /* Buffer */
  instream_t input;
};

static struct context *context_tos;

/* Advance locus to the next line */
void
advance_line (void)
{
  ++context_tos->point.line;
  context_tos->point.col = 0;
}

#define YY_USER_ACTION					    \
  do							    \
    {							    \
      if (YYSTATE == 0)					    \
	{						    \
	  yylloc.beg = context_tos->point;		    \
	  yylloc.beg.col++;				    \
	}						    \
      context_tos->point.col += yyleng;			    \
      yylloc.end = context_tos->point;			    \
    }							    \
  while (0);

#undef YY_INPUT
#define YY_INPUT(buf,result,max_size)                                   \
  do									\
    {									\
      if (context_tos)							\
        result = instream_read (context_tos->input, buf, max_size);	\
      else								\
	result = 0;							\
    }									\
  while (0);
 
void string_begin (void);
void string_add (const char *s, int l);
void string_addc (int c);
char *string_end (int empty_null);
int unescape (int c);

struct file_name
{
  struct file_name *next;
  char str[1];
};

static struct file_name *file_head;

static void
file_names_free (void)
{
  while (file_head)
    {
      struct file_name *next = file_head->next;
      free (file_head);
      file_head = next;
    }
}
 
char *
file_name_alloc (char const *s)
{
  struct file_name *f = emalloc (sizeof (*f) + strlen (s));
  strcpy (f->str, s);
  f->next = file_head;
  if (!file_head)
    atexit (file_names_free);
  file_head = f;
  return f->str;
}

int
interactive (void)
{
  return context_tos && instream_interactive (context_tos->input);
}

int
input_history_size (void)
{
  return context_tos ? instream_history_size (context_tos->input) : 0;
}

const char *
input_history_get (int n)  
{
  return context_tos ? instream_history_get (context_tos->input, n) : NULL;
}

const char *
input_stream_name (void)
{
  return context_tos ? context_tos->input->in_name : "NULL";
}
 
static struct context *
input_context_lookup (instream_t istr)
{
  struct context *cp;

  for (cp = context_tos; cp; cp = cp->parent)
    if (instream_eq (cp->input, istr))
      break;
  return cp;
}
 
int
input_context_push (instream_t input)
{
  struct context *cp;

  cp = input_context_lookup (input);
  if (cp)
    {
      terror (_("recursive sourcing"));
      if (cp->parent)
	lerror (&cp->locus, _("%s already sourced here"),
		instream_name (input));
      return 1;
    }

  yy_switch_to_buffer (yy_create_buffer (NULL, YY_BUF_SIZE));

  /* Create new context */

  cp = ecalloc (1, sizeof (*cp));
  cp->locus = yylloc;
  cp->point.file = file_name_alloc (instream_name (input));
  cp->point.line = 1;
  cp->point.col = 0;
  
  cp->input = input;
  cp->buf = YY_CURRENT_BUFFER;
  cp->parent = context_tos;
  context_tos = cp;

  return 0;
}

void
lex_trace (int n)
{
  yy_flex_debug = n;
}
    
int
input_context_pop (void)
{
  struct context *cp;

  if (!context_tos)
    return 1;
  instream_close (context_tos->input);
  cp = context_tos->parent;
  free (context_tos);
  context_tos = cp;
  yy_delete_buffer (YY_CURRENT_BUFFER);
  if (!cp)
    return 1;
  
  yylloc = cp->locus;
  yy_switch_to_buffer (cp->buf);

  return 0;
}

void
input_context_drain (void)
{
   while (!input_context_pop ())
    ;
}

static int
t_num (int base)
{
  long n;
  errno = 0;
  n = strtol (yytext, NULL, base);
  if (errno)
    {
      lerror (&yylloc, "%s", strerror (errno));
      return T_BOGUS;
    }
  if (n < INT_MIN || n > INT_MAX)
    {
      lerror (&yylloc, "value out of range");
      return T_BOGUS;
    }
  yylval.num = n;
  return T_NUM;  
}
 
%}

%option noinput
%option nounput
%option nodefault

%x CMD STR MLSTR DEF SHELLWS SHELL SHSTR SHQ

WS [ \t][ \t]*
IDENT [a-zA-Z_][a-zA-Z_0-9-]*
N [0-9][0-9]*
P [1-9][0-9]*
X [0-9a-fA-F]
O [0-7]

%%
^[ \t]*#[ \t]*line[ \t].*\n {
  char *p;
  char *file = NULL;
  int line, len;
  
  for (p = strchr (yytext, '#') + 1; *p == ' ' || *p == '\t'; p++);
  p += 4;
  for (; *p == ' ' || *p == '\t'; p++);

  line = strtol (p, &p, 10);
  for (; *p == ' ' || *p == '\t'; p++);

  if (*p == '"')
    {
      p++;
      len = strcspn (p, "\"");
      if (p[len] == 0)
	{
	  yyerror (_("invalid #line statement"));
	  REJECT;
	}
      file = emalloc (len + 1);
      memcpy (file, p, len);
      file[len] = 0;
      for (p += len + 1; *p == ' ' || *p == '\t'; p++);
    }
  if (*p != '\n' )
    {
      yyerror (_("invalid #line statement"));
      free (file);
      REJECT;
    }
  if (file)
    context_tos->point.file = file;
  context_tos->point.line = line;
  context_tos->point.col = 0;
}
#.*\n              advance_line ();
#.*     /* end-of-file comment */;

<INITIAL>{
\?        { BEGIN (CMD);
            return command_lookup ("help", &yylloc, &yylval.cmd); }
\! {
  BEGIN (SHELLWS);
  string_begin ();
  return command_lookup ("shell", &yylloc, &yylval.cmd); 
 }
{IDENT}   {
  int t;
  t = command_lookup (yytext, &yylloc, &yylval.cmd);
  if (t == T_SHELL)
    BEGIN (SHELLWS);
  else
    BEGIN (CMD);
  return t;
 }
{WS}      ; 
}
 
<DEF>{
off           { return T_OFF; }
pad           { return T_PAD; }
0[xX]{X}{X}*  { return t_num (8); };
0{O}{O}*      { return t_num (16); };
0|{P}         { return t_num (10); };
{IDENT}       { if ((yylval.type = datadef_lookup (yytext)))
                  return T_TYPE;
                else
		  {
		    yylval.string = estrdup (yytext);
		    return T_IDENT;
		  }
              }
[^ \"\t\n;\[\]{},=]+  { yylval.string = estrdup (yytext); return T_WORD; }
\n            { advance_line (); }
{WS}          ;
.             return yytext[0]; 
}

<CMD>{
{IDENT}       { yylval.string = estrdup (yytext); return T_IDENT; }
[^ \"\t\n;\[\]{},=]+  { yylval.string = estrdup (yytext); return T_WORD; }
\"[^\\\"\n]*\"     {
                     yylval.string = emalloc (yyleng - 1);
                     memcpy (yylval.string, yytext+1, yyleng-2);
		     yylval.string[yyleng-2] = 0;
		     return T_WORD; }
\"[^\\\"\n]*\\\n   {
                     advance_line ();
                     string_begin ();
                     string_add (yytext + 1, yyleng - 2);
                     BEGIN (MLSTR); }
\"[^\\\"\n]*\\.    {
                     string_begin ();
                     string_add (yytext + 1, yyleng - 3);
		     string_addc (unescape (yytext[yyleng-1]));
                     BEGIN (STR); }
;                  { BEGIN (INITIAL); return ';'; }
{WS}               ; 
}

<STR,MLSTR>{
[^\\\"\n]*\"       { if (yyleng > 1)
		       string_add (yytext, yyleng - 1);
		     yylval.string = string_end (FALSE);
		     BEGIN (CMD);
		     return T_WORD; }
[^\\\"\n]*\\\n     { advance_line ();
                     string_add (yytext, yyleng - 1); }
[^\\\"\n]*\\.      { string_add (yytext, yyleng - 2);
                     string_addc (unescape (yytext[yyleng-1])); }
}

<SHELLWS>{
{WS}  { BEGIN(SHELL); }
"\n"  {
    BEGIN (INITIAL);
    advance_line ();
    return '\n';
 }
}

<SHELL>{
\\\n        { advance_line (); string_add (yytext, yyleng); }
\\.         { string_add (yytext, yyleng); }
\'[^\'\n]*\' { string_add (yytext, yyleng); }
\'[^\'\n]*\n { advance_line ();
               string_add (yytext, yyleng);
	       BEGIN (SHQ); }
\"[^\\\"\n]*\" { string_add (yytext, yyleng); }
\"[^\\\"\n]*\n { advance_line ();
                 string_add (yytext, yyleng);
	         BEGIN (SHSTR); }
\"[^\\\"\n]*\\. { string_add (yytext, yyleng);
                  BEGIN (SHSTR); }
"\n"  {
    BEGIN (INITIAL);
    advance_line ();
    yyless (0);
    if ((yylval.string = string_end (TRUE)) != NULL)
      return T_WORD;    
 }
.   string_addc (yytext[0]);
}

<SHSTR>{
[^\\\"]*\\. { string_add (yytext, yyleng); }
[^\\\"]*\"  { string_add (yytext, yyleng);
              BEGIN (SHELL); }
}

<SHQ>{
[^\'\n]*\n  { advance_line ();
              string_add (yytext, yyleng); }
[^\'\n]*\'  { string_add (yytext, yyleng);
              BEGIN (SHELL); } 
}

<*>\n              { BEGIN (INITIAL); advance_line (); return '\n'; }

<INITIAL,CMD,DEF>. return yytext[0];
%%

int
yywrap (void)
{
  return input_context_pop ();
}

void
begin_def (void)
{
  BEGIN (DEF);
}

void
end_def (void)
{
  BEGIN (CMD);
} 

void
print_prompt_at_bol (void)
{
  if (YY_AT_BOL ())
    {
      char *s = make_prompt ();
      fputs (s, stdout);
      fflush (stdout);
      free (s);
    }
}
  

struct strseg
{
  struct strseg *next;
  int len;
  char ptr[1];
};

static struct strseg *strseg_head, *strseg_tail;

void
string_begin (void)
{
  strseg_head = strseg_tail = NULL;
}

void
strseg_attach (struct strseg *seg)
{
  seg->next = NULL;
  if (strseg_tail)
    strseg_tail->next = seg;
  else
    strseg_head = seg;
  strseg_tail = seg;
}  

void
string_add (const char *s, int l)
{
  struct strseg *seg = emalloc (sizeof (*seg) + l);
  memcpy (seg->ptr, s, l);
  seg->len = l;
  strseg_attach (seg);
}

void
string_addc (int c)
{
  struct strseg *seg = emalloc (sizeof (*seg));
  seg->ptr[0] = c;
  seg->len = 1;
  strseg_attach (seg);
}

/*
 * Compose the collected string segments into a nul-terminated string.
 * Return the allocated string.
 * If EMPTY_NULL is TRUE and the resulting string length is 0, return
 * NULL instead.
 */
char *
string_end (int empty_null)
{
  int len = 0;
  struct strseg *seg;
  char *ret, *p;
  
  for (seg = strseg_head; seg; seg = seg->next)
    len += seg->len;

  if (len == 0 && empty_null)
    ret = NULL;
  else
    {
      ret = emalloc (len + 1);
      p = ret;
      for (seg = strseg_head; seg; )
	{
	  struct strseg *next = seg->next;
	  memcpy (p, seg->ptr, seg->len);
	  p += seg->len;
	  free (seg);
	  seg = next;
	}
      *p = 0;
    }
  strseg_head = strseg_tail = NULL;

  return ret;
}

static char transtab[] = "\\\\\"\"a\ab\bf\fn\nr\rt\tv\v";

int
unescape (int c)
{
  char *p;

  for (p = transtab; *p; p += 2)
    {
      if (*p == c)
	return p[1];
    }
  return c;
}

int
escape (int c)
{
  char *p;
  for (p = transtab + sizeof (transtab) - 2; p > transtab; p -= 2)
    {
      if (*p == c)
	return p[-1];
    }
  return 0;
}

void
locus_print (FILE *fp, struct locus const *loc)
{							  
  if (loc->beg.file)
    {
      if (loc->beg.col == 0)
	fprintf (fp, "%s:%u",
		 loc->beg.file,
		 loc->beg.line);
      else if (strcmp (loc->beg.file, loc->end.file))
	fprintf (fp, "%s:%u.%u-%s:%u.%u",
		 loc->beg.file,
		 loc->beg.line, loc->beg.col,
		 loc->end.file,
		 loc->end.line, loc->end.col);
      else if (loc->beg.line != loc->end.line)
	fprintf (fp, "%s:%u.%u-%u.%u",
		 loc->beg.file,
		 loc->beg.line, loc->beg.col,
		 loc->end.line, loc->end.col);
      else if (loc->beg.col != loc->end.col)
	fprintf (fp, "%s:%u.%u-%u",
		 loc->beg.file,
		 loc->beg.line, loc->beg.col,
		 loc->end.col);
      else
	fprintf (fp, "%s:%u.%u",
		 loc->beg.file,
		 loc->beg.line,
		 loc->beg.col);
    }
}

void
vlerror (struct locus *loc, const char *fmt, va_list ap)
{
  if (!interactive ())
    fprintf (stderr, "%s: ", progname);
  if (loc && loc->beg.file)
    {
      YY_LOCATION_PRINT (stderr, *loc);
      fprintf (stderr, ": ");
    }
  vfprintf (stderr, fmt, ap);
  fputc ('\n', stderr);
}

void
lerror (struct locus *loc, const char *fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);
  vlerror (loc, fmt, ap);
  va_end (ap);
}


static struct slist *
pe_file_name (void)
{
  char *name = NULL;
  variable_get ("filename", VART_STRING, (void**) &name);
  return name ? slist_new (name) : NULL;
}

static struct slist *
pe_program_name (void)
{
  return slist_new (progname);
}

static struct slist *
pe_package_name (void)
{
  return slist_new (PACKAGE_NAME);
}

static struct slist *
pe_program_version (void)
{
  return slist_new (PACKAGE_VERSION);
}

static struct slist *
pe_space (void)
{
  return slist_new (" ");
}

struct prompt_exp
{
  int ch;
  struct slist *(*fun) (void);
};

struct prompt_exp prompt_exp[] = {
  { 'f', pe_file_name },
  { 'p', pe_program_name },
  { 'P', pe_package_name },
  { 'v', pe_program_version },
  { '_', pe_space },
  { 0 }
};

static int
expand_char (int c, struct slist **tailp)
{
  struct prompt_exp *p;

  if (c && c != '%')
    {
      for (p = prompt_exp; p->ch; p++)
	{
	  if (c == p->ch)
	    {
	      struct slist *s = p->fun ();
	      if (s)
		slist_insert (tailp, s);
	      return 0;
	    }
	}
    }
  return 1;
}

char const *
psname (void)
{
  switch (YYSTATE)
    {
    case DEF:
    case MLSTR:
    case SHSTR:
    case SHQ:
      return "ps2";

    default:
      return "ps1";
    }
}

char *
make_prompt (void)
{
  const char *s;
  const char *prompt;
  struct slist *head = NULL, *tail = NULL, *p;
  char *ret, *end;
  size_t len;
  
  switch (variable_get (psname (), VART_STRING, (void *) &prompt))
    {
    case VAR_OK:
      break;

    case VAR_ERR_NOTSET:
      return NULL;
      
    default:
      abort ();
    }

  for (s = prompt; *s; )
    {
      if (*s == '%' && s[1])
	{
	  if (s > prompt)
	    {
	      slist_insert (&tail, slist_new_l (prompt, s - prompt));
	      if (!head)
		head = tail;
	    }
	  if (expand_char (s[1], &tail) == 0)
	    {
	      if (!head)
		head = tail;
	      prompt = s + 2;
	    }
	  else
	    prompt = s;
	  s += 2;
	}
      else
	++s;
    }

  if (s > prompt)
    {
      slist_insert (&tail, slist_new_l (prompt, s - prompt));
      if (!head)
	head = tail;
    }

  len = 0;
  for (p = head; p; p = p->next)
    len += strlen (p->str);

  ret = emalloc (len + 1);
  end = ret;
  for (p = head; p; p = p->next)
    {
      s = p->str;
      while (*s)
	*end++ = *s++;
    }
  *end = 0;

  slist_free (head);
  
  return ret;
}
  
