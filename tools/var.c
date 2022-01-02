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

#define VARF_DFL    0x00   /* Default flags -- everything disabled */
#define VARF_SET    0x01   /* Variable is set */
#define VARF_INIT   0x02   /* Variable is initialized */
#define VARF_PROT   0x04   /* Variable is protected, i.e. cannot be unset */
#define VARF_OCTAL  0x08   /* For integer variables -- use octal base */

#define VAR_IS_SET(v) ((v)->flags & VARF_SET)

union value
{
  char *string;
  int bool;
  int num;
};

struct variable
{
  char *name;
  int type;
  int flags;
  union value init;
  union value v;
  void *data;
  int (*sethook) (struct variable *, union value *);
  int (*typeconv) (struct variable *, int, void **);
  void (*freehook) (void *);
};

static int open_sethook (struct variable *, union value *);
static int open_typeconv (struct variable *var, int type, void **retptr);
static int format_sethook (struct variable *, union value *);
static int format_typeconv (struct variable *var, int type, void **retptr);
static int fd_sethook (struct variable *, union value *);
static int centfree_sethook (struct variable *var, union value *v);
static int coalesce_sethook (struct variable *var, union value *v);
static int cachesize_sethook (struct variable *var, union value *v);
static int errormask_sethook (struct variable *var, union value *v);
static int errormask_typeconv (struct variable *var, int type, void **retptr);
static void errormask_freehook (void *);
static int errorexit_sethook (struct variable *var, union value *v);

static struct variable vartab[] = {
  /* Top-level prompt */
  {
    .name = "ps1",
    .type = VART_STRING,
    .flags = VARF_INIT,
    .init = { .string = "%p>%_" }
  },
  /* Second-level prompt (used within "def" block) */
  {
    .name = "ps2",
    .type = VART_STRING,
    .flags = VARF_INIT,
    .init = { .string = "%_>%_" }
  },
  /* This delimits array members */
  {
    .name = "delim1",
    .type = VART_STRING,
    .flags = VARF_INIT|VARF_PROT,
    .init = { .string = "," }
  },
  /* This delimits structure members */
  {
    .name = "delim2",
    .type = VART_STRING,
    .flags = VARF_INIT|VARF_PROT,
    .init = { .string = "," }
  },
  {
    .name = "confirm",
    .type = VART_BOOL,
    .flags = VARF_INIT,
    .init = { .bool = 1 }
  },
  {
    .name = "cachesize",
    .type = VART_INT,
    .flags = VARF_DFL,
    .sethook = cachesize_sethook
  },
  {
    .name = "blocksize",
    .type = VART_INT,
    .flags = VARF_DFL
  },
  {
    .name = "open",
    .type = VART_STRING,
    .flags = VARF_DFL,
    .sethook = open_sethook,
    .typeconv = open_typeconv
  },
  {
    .name = "lock",
    .type = VART_BOOL,
    .flags = VARF_INIT,
    .init = { .bool = 1 }
  },
  {
    .name = "mmap",
    .type = VART_BOOL,
    .flags = VARF_INIT,
    .init = { .bool = 1 }
  },
  {
    .name = "sync",
    .type = VART_BOOL,
    .flags = VARF_INIT,
    .init = { .bool = 0 }
  },
  {
    .name = "coalesce",
    .type = VART_BOOL,
    .flags = VARF_INIT,
    .init = { .bool = 0 },
    .sethook = coalesce_sethook
  },
  {
    .name = "centfree",
    .type = VART_BOOL,
    .flags = VARF_INIT,
    .init = { .bool = 0 },
    .sethook = centfree_sethook
  },
  {
    .name = "filemode",
    .type = VART_INT,
    .flags = VARF_INIT|VARF_OCTAL|VARF_PROT,
    .init = { .num = 0644 }
  },
  {
    .name = "format",
    .type = VART_STRING,
    .flags = VARF_INIT,
    .init = { .string = "standard" },
    .sethook = format_sethook,
    .typeconv = format_typeconv
  },
  {
    .name = "pager",
    .type = VART_STRING,
    .flags = VARF_DFL
  },
  {
    .name = "quiet",
    .type = VART_BOOL,
    .flags = VARF_DFL
  },
  {
    .name = "filename",
    .type = VART_STRING,
    .flags = VARF_INIT|VARF_PROT,
    { .string = GDBMTOOL_DEFFILE }
  },
  {
    .name = "fd",
    .type = VART_INT,
    .flags = VARF_DFL,
    .sethook = fd_sethook
  },
  {
    .name = "errorexit",
    .type = VART_STRING,
    .sethook = errorexit_sethook,
    .typeconv = errormask_typeconv,
    .freehook = errormask_freehook    
  },
  {
    .name = "errormask",
    .type = VART_STRING,
    .sethook = errormask_sethook,
    .typeconv = errormask_typeconv,
    .freehook = errormask_freehook
  },
  {
    .name = "timing",
    .type = VART_BOOL
  },
  {
    .name = "trace",
    .type = VART_BOOL
  },
  { NULL }
};

static struct variable *
varfind (const char *name)
{
  struct variable *vp;

  for (vp = vartab; vp->name; vp++)
    if (strcmp (vp->name, name) == 0)
      return vp;
  
  return NULL;
}

typedef int (*setvar_t) (union value *, void *, int);

static int
s2s (union value *vp, void *val, int flags)
{
  vp->string = estrdup (val);
  return VAR_OK;
}

static int
b2s (union value *vp, void *val, int flags)
{
  vp->string = estrdup (*(int*)val ? "true" : "false");
  return VAR_OK;
}

static int
i2s (union value *vp, void *val, int flags)
{
  char buf[128];
  snprintf (buf, sizeof buf, "%d", *(int*)val);
  vp->string = estrdup (buf);
  return VAR_OK;
}

static int
s2b (union value *vp, void *val, int flags)
{
  static char *trueval[] = { "on", "true", "yes", NULL };
  static char *falseval[] = { "off", "false", "no", NULL };
  int i;
  unsigned long n;
  char *p;
  
  for (i = 0; trueval[i]; i++)
    if (strcasecmp (trueval[i], val) == 0)
      {
	vp->bool = 1;
	return VAR_OK;
      }
  
  for (i = 0; falseval[i]; i++)
    if (strcasecmp (falseval[i], val) == 0)
      {
	vp->bool = 0;
	return VAR_OK;
      }
  
  n = strtoul (val, &p, 0);
  if (*p)
    return VAR_ERR_BADTYPE;
  vp->bool = !!n;
  return VAR_OK;
}
  
static int
s2i (union value *vp, void *val, int flags)
{
  char *p;
  int n = strtoul (val, &p, (flags & VARF_OCTAL) ? 8 : 10);

  if (*p)
    return VAR_ERR_BADTYPE;

  vp->num = n;
  return VAR_OK;
}

static int
b2b (union value *vp, void *val, int flags)
{
  vp->bool = !!*(int*)val;
  return VAR_OK;
}

static int
b2i (union value *vp, void *val, int flags)
{
  vp->num = *(int*)val;
  return VAR_OK;
}

static int
i2i (union value *vp, void *val, int flags)
{
  vp->num = *(int*)val;
  return VAR_OK;
}

static int
i2b (union value *vp, void *val, int flags)
{
  vp->bool = *(int*)val;
  return VAR_OK;
}

static setvar_t setvar[3][3] = {
            /*    s     b    i */
  /* s */    {   s2s,  b2s, i2s },
  /* b */    {   s2b,  b2b, i2b },
  /* i */    {   s2i,  b2i, i2i }
};

int
variable_set (const char *name, int type, void *val)
{
  struct variable *vp = varfind (name);
  int rc;
  union value v, *valp;
  
  if (!vp)
    return VAR_ERR_NOTDEF;

  if (val)
    {
      memset (&v, 0, sizeof (v));
      rc = setvar[vp->type][type] (&v, val, vp->flags);
      if (rc)
	return rc;
      valp = &v; 
    }
  else
    {
      if (vp->flags & VARF_PROT)
	return VAR_ERR_BADVALUE;
      valp = NULL;
    }
  
  if (vp->sethook && (rc = vp->sethook (vp, valp)) != VAR_OK)
    return rc;

  if (vp->type == VART_STRING && (vp->flags & VARF_SET))
    free (vp->v.string);

  if (!val)
    {
      vp->flags &= VARF_SET;
    }
  else
    {
      vp->v = v;
      vp->flags |= VARF_SET;
    }
  
  return VAR_OK;
}

int
variable_unset (const char *name)
{
  struct variable *vp = varfind (name);
  int rc;
    
  if (!vp)
    return VAR_ERR_NOTDEF;
  if (vp->flags & VARF_PROT)
    return VAR_ERR_BADVALUE;

  if (vp->sethook && (rc = vp->sethook (vp, NULL)) != VAR_OK)
    return rc;

  if (vp->type == VART_STRING)
    {
      free (vp->v.string);
      vp->v.string = NULL;
    }
  vp->flags &= ~VARF_SET;

  return VAR_OK;
}

int
variable_get (const char *name, int type, void **val)
{
  struct variable *vp = varfind (name);

  if (!vp)
    return VAR_ERR_NOTDEF;
  
  if (!VAR_IS_SET (vp))
    return VAR_ERR_NOTSET;

  if (type != vp->type)
    {
      if (vp->typeconv)
	{
	  return vp->typeconv (vp, type, val);
	}
      else
	return VAR_ERR_BADTYPE;
    }
      
  switch (vp->type)
    {
    case VART_STRING:
      *val = vp->v.string;
      break;

    case VART_BOOL:
      *(int*)val = vp->v.bool;
      break;
      
    case VART_INT:
      *(int*)val = vp->v.num;
      break;
    }

  return VAR_OK;
}

static int
varcmp (const void *a, const void *b)
{
  return strcmp (((struct variable const *)a)->name,
		 ((struct variable const *)b)->name);
}

void
variable_print_all (FILE *fp)
{
  struct variable *vp;
  char *s;
  static int sorted;
  
  if (!sorted)
    {
      qsort (vartab, ARRAY_SIZE (vartab) - 1, sizeof (vartab[0]), varcmp);
      sorted = 1;
    }
  
  for (vp = vartab; vp->name; vp++)
    {
      if (!VAR_IS_SET (vp))
	{
	  fprintf (fp, "# %s is unset", vp->name);
	}
      else
	{
	  switch (vp->type)
	    {
	    case VART_INT:
	      fprintf (fp, (vp->flags & VARF_OCTAL) ? "%s=%03o" : "%s=%d",
		       vp->name, vp->v.num);
	      break;
	      
	    case VART_BOOL:
	      fprintf (fp, "%s%s", vp->v.bool ? "" : "no", vp->name);
	      break;
	      
	    case VART_STRING:
	      fprintf (fp, "%s=\"", vp->name);
	      for (s = vp->v.string; *s; s++)
		{
		  int c;
		  
		  if (isprint (*s))
		    fputc (*s, fp);
		  else if ((c = escape (*s)))
		    fprintf (fp, "\\%c", c);
		  else
		    fprintf (fp, "\\%03o", *s);
		}
	      fprintf (fp, "\"");
	    }
	}
      fputc ('\n', fp);
    }
}

int
variable_is_set (const char *name)
{
  struct variable *vp = varfind (name);

  if (!vp)
    return 0;
  return VAR_IS_SET (vp);
}

int
variable_is_true (const char *name)
{
  int n;

  if (variable_get (name, VART_BOOL, (void **) &n) == VAR_OK)
    return n;
  return 0;
}

void
variables_free (void)
{
  struct variable *vp;

  for (vp = vartab; vp->name; vp++)
    {
      if (vp->type == VART_STRING && (vp->flags & VARF_SET))
	free (vp->v.string);
      vp->v.string = NULL;
      if (vp->freehook && vp->data)
	{
	  vp->freehook (vp->data);
	  vp->data = NULL;
	}
      vp->flags &= ~VARF_SET;
    }
}

void
variables_init (void)
{
  struct variable *vp;

  for (vp = vartab; vp->name; vp++)
    {
      if (!(vp->flags & VARF_SET) && (vp->flags & VARF_INIT))
	{
	  if (vp->type == VART_STRING)
	    variable_set (vp->name, vp->type, vp->init.string);
	  else
	    variable_set (vp->name, vp->type, &vp->init);
	}
    }
}

struct kwtrans
{
  char *s;
  int t;
};

static int
string_to_int (char const *s, struct kwtrans *t)
{
  int i;

  for (i = 0; t[i].s; i++)
    if (strcmp (t[i].s, s) == 0)
      return t[i].t;
  return -1;
}

#if 0
static char const *
int_to_string (int n, struct kwtrans *t)
{
  int i;

  for (i = 0; t[i].s; i++)
    if (t[i].t == n)
      return t[i].s;
  return NULL;
}
#endif

static struct kwtrans db_open_flags[] = {
    { "newdb", GDBM_NEWDB },
    { "wrcreat", GDBM_WRCREAT },
    { "rw", GDBM_WRCREAT },
    { "reader", GDBM_READER },
    { "readonly", GDBM_READER },
    { NULL }
};

static int
open_sethook (struct variable *var, union value *v)
{
  int n;
  if (!v)
    return VAR_ERR_BADVALUE;
  n = string_to_int (v->string, db_open_flags);
  if (n == -1)
    return VAR_ERR_BADVALUE;
  return VAR_OK;
}

static int
open_typeconv (struct variable *var, int type, void **retptr)
{
  if (type == VART_INT)
    {
      *(int*) retptr = string_to_int (var->v.string, db_open_flags);
      return VAR_OK;
    }
  return VAR_ERR_BADTYPE;
}

static int
format_sethook (struct variable *var, union value *v)
{
  if (!v)
    return VAR_OK;
  return _gdbm_str2fmt (v->string) == -1 ? VAR_ERR_BADVALUE : VAR_OK;
}

static int
format_typeconv (struct variable *var, int type, void **retptr)
{
  if (type == VART_INT)
    {
      *(int*) retptr = _gdbm_str2fmt (var->v.string);
      return VAR_OK;
    }
  return VAR_ERR_BADTYPE;
}

static int
fd_sethook (struct variable *var, union value *v)
{
  if (!v)
    return VAR_OK;
  if (v->num < 0)
    return VAR_ERR_BADVALUE;
  return VAR_OK;
}

static int
cachesize_sethook (struct variable *var, union value *v)
{
  if (!v)
    return VAR_OK;
  if (v->num < 0)
    return VAR_ERR_BADVALUE;
  return gdbmshell_setopt ("GDBM_SETCACHESIZE", GDBM_SETCACHESIZE, v->num) == 0
         ? VAR_OK : VAR_ERR_GDBM;
}

static int
centfree_sethook (struct variable *var, union value *v)
{
  if (!v)
    return VAR_OK;
  return gdbmshell_setopt ("GDBM_SETCENTFREE", GDBM_SETCENTFREE, v->bool) == 0
         ? VAR_OK : VAR_ERR_GDBM;
}

static int
coalesce_sethook (struct variable *var, union value *v)
{
  if (!v)
    return VAR_OK;
  return gdbmshell_setopt ("GDBM_SETCOALESCEBLKS", GDBM_SETCOALESCEBLKS, v->bool) == 0
         ? VAR_OK : VAR_ERR_GDBM;
}

const char * const errname[_GDBM_MAX_ERRNO+1] = {
  [GDBM_NO_ERROR]               = "GDBM_NO_ERROR",
  [GDBM_MALLOC_ERROR]           = "GDBM_MALLOC_ERROR",
  [GDBM_BLOCK_SIZE_ERROR]       = "GDBM_BLOCK_SIZE_ERROR",
  [GDBM_FILE_OPEN_ERROR]        = "GDBM_FILE_OPEN_ERROR",
  [GDBM_FILE_WRITE_ERROR]       = "GDBM_FILE_WRITE_ERROR",
  [GDBM_FILE_SEEK_ERROR]        = "GDBM_FILE_SEEK_ERROR",
  [GDBM_FILE_READ_ERROR]        = "GDBM_FILE_READ_ERROR",
  [GDBM_BAD_MAGIC_NUMBER]       = "GDBM_BAD_MAGIC_NUMBER",
  [GDBM_EMPTY_DATABASE]         = "GDBM_EMPTY_DATABASE",
  [GDBM_CANT_BE_READER]         = "GDBM_CANT_BE_READER",
  [GDBM_CANT_BE_WRITER]         = "GDBM_CANT_BE_WRITER",
  [GDBM_READER_CANT_DELETE]     = "GDBM_READER_CANT_DELETE",
  [GDBM_READER_CANT_STORE]      = "GDBM_READER_CANT_STORE",
  [GDBM_READER_CANT_REORGANIZE] = "GDBM_READER_CANT_REORGANIZE",
  [GDBM_UNKNOWN_ERROR]          = "GDBM_UNKNOWN_ERROR",
  [GDBM_ITEM_NOT_FOUND]         = "GDBM_ITEM_NOT_FOUND",
  [GDBM_REORGANIZE_FAILED]      = "GDBM_REORGANIZE_FAILED",
  [GDBM_CANNOT_REPLACE]         = "GDBM_CANNOT_REPLACE",
  [GDBM_MALFORMED_DATA]         = "GDBM_MALFORMED_DATA",
  [GDBM_OPT_ALREADY_SET]        = "GDBM_OPT_ALREADY_SET",
  [GDBM_OPT_BADVAL]             = "GDBM_OPT_BADVAL",
  [GDBM_BYTE_SWAPPED]           = "GDBM_BYTE_SWAPPED",
  [GDBM_BAD_FILE_OFFSET]        = "GDBM_BAD_FILE_OFFSET",
  [GDBM_BAD_OPEN_FLAGS]         = "GDBM_BAD_OPEN_FLAGS",
  [GDBM_FILE_STAT_ERROR]        = "GDBM_FILE_STAT_ERROR",
  [GDBM_FILE_EOF]               = "GDBM_FILE_EOF",
  [GDBM_NO_DBNAME]              = "GDBM_NO_DBNAME",
  [GDBM_ERR_FILE_OWNER]         = "GDBM_ERR_FILE_OWNER",
  [GDBM_ERR_FILE_MODE]          = "GDBM_ERR_FILE_MODE",
  [GDBM_NEED_RECOVERY]          = "GDBM_NEED_RECOVERY",
  [GDBM_BACKUP_FAILED]          = "GDBM_BACKUP_FAILED",
  [GDBM_DIR_OVERFLOW]           = "GDBM_DIR_OVERFLOW",
  [GDBM_BAD_BUCKET]             = "GDBM_BAD_BUCKET",
  [GDBM_BAD_HEADER]             = "GDBM_BAD_HEADER",
  [GDBM_BAD_AVAIL]              = "GDBM_BAD_AVAIL",
  [GDBM_BAD_HASH_TABLE]         = "GDBM_BAD_HASH_TABLE",
  [GDBM_BAD_DIR_ENTRY]          = "GDBM_BAD_DIR_ENTRY",
  [GDBM_FILE_CLOSE_ERROR]       = "GDBM_FILE_CLOSE_ERROR",
  [GDBM_FILE_SYNC_ERROR]        = "GDBM_FILE_SYNC_ERROR",
  [GDBM_FILE_TRUNCATE_ERROR]    = "GDBM_FILE_TRUNCATE_ERROR",
  [GDBM_BUCKET_CACHE_CORRUPTED] = "GDBM_BUCKET_CACHE_CORRUPTED",
  [GDBM_BAD_HASH_ENTRY]         = "GDBM_BAD_HASH_ENTRY",
  [GDBM_ERR_SNAPSHOT_CLONE]     = "GDBM_ERR_SNAPSHOT_CLONE",
  [GDBM_ERR_REALPATH]           = "GDBM_ERR_REALPATH",
  [GDBM_ERR_USAGE]              = "GDBM_ERR_USAGE",
};

static int
str2errcode (char const *str)
{
  int i;
#define GDBM_PREFIX "GDBM_"
#define GDBM_PREFIX_LEN (sizeof (GDBM_PREFIX) - 1)
  
  if (strncasecmp (str, GDBM_PREFIX, GDBM_PREFIX_LEN) == 0)
    str += GDBM_PREFIX_LEN;
  
  for (i = 0; i < ARRAY_SIZE (errname); i++)
    if (strcasecmp (errname[i] + GDBM_PREFIX_LEN, str) == 0)
      return i;

  return -1;
}

#define ERROR_MASK_SIZE (_GDBM_MAX_ERRNO+1)

static int
errormask_sethook (struct variable *var, union value *v)
{
  char *errmask = var->data;
  
  if (!v || strcmp (v->string, "false") == 0)
    {
      if (var->data)
	memset (errmask, 0, ERROR_MASK_SIZE);
    }
  else
    {
      char *t;

      if (!errmask)
	{
	  errmask = calloc (ERROR_MASK_SIZE, sizeof (char));
	  var->data = errmask;
	}
      
      if (strcmp (v->string, "true") == 0)
	{
	  memset (errmask, 1, ERROR_MASK_SIZE);
	  free (v->string);
	  v->string = estrdup ("all");
	}
      else
	{
	  for (t = strtok (v->string, ","); t; t = strtok (NULL, ","))
	    {
	      int len, val, e;
	      
	      while (t[0] == ' ' || t[0] == '\t')
		t++;
	      len = strlen (t);
	      while (len > 0 && (t[len-1] == ' ' || t[len-1] == '\t'))
		len--;
	      t[len] = 0;
	      
	      if (t[0] == '-')
		{
		  val = 0;
		  t++;
		}
	      else if (t[0] == '+')
		{
		  val = 1;
		  t++;
		}
	      else
		{
		  val = 1;
		}
	      if (strcmp (t, "all") == 0)
		{
		  for (e = 1; e < ERROR_MASK_SIZE; e++)
		    errmask[e] = val;
		}
	      else
		{
		  e = str2errcode (t);
		  if (e == -1)
		    terror (_("unrecognized error code: %s"), t);
		  else
		    errmask[e] = val;
		}
	    }
	}
    }
  return VAR_OK;
}

static int
errormask_typeconv (struct variable *var, int type, void **retptr)
{
  char *errmask = var->data;

  if (type == VART_INT)
    {
      int n = *(int*) retptr;
      if (n >= 0 && n < ERROR_MASK_SIZE)
	{
	  *(int*) retptr = errmask ? errmask[n] : 0;
	  return VAR_OK;
	}
      else
	return VAR_ERR_BADVALUE;
    }
  return VAR_ERR_BADTYPE;
}

static void
errormask_freehook (void *data)
{
  free (data);
}

static int
errorexit_sethook (struct variable *var, union value *v)
{
  if (interactive ())
    {
      return VAR_ERR_BADVALUE;
    }
  return errormask_sethook (var, v);
}
