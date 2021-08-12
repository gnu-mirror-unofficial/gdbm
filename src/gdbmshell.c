/* This file is part of GDBM, the GNU data base manager.
   Copyright (C) 1990-2021 Free Software Foundation, Inc.

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
#include "gdbm.h"
#include "gram.h"

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

static GDBM_FILE gdbm_file = NULL;   /* Database to operate upon */
static datum key_data;               /* Current key */
static datum return_data;            /* Current data */

static void
datum_free (datum *dp)
{
  free (dp->dptr);
  dp->dptr = NULL;
}


int
gdbmshell_setopt (char *name, int opt, int val)
{
  if (gdbm_file)
    {
      if (gdbm_setopt (gdbm_file, opt, &val, sizeof (val)) == -1)
	{
	  terror (_("%s failed: %s"), name, gdbm_strerror (gdbm_errno));
	  return 1;
	}
    }
  return 0;
}

static void
closedb (void)
{
  if (gdbm_file)
    {
      gdbm_close (gdbm_file);
      gdbm_file = NULL;
    }

  datum_free (&key_data);
  datum_free (&return_data);
}

static int
opendb (char *dbname, int fd)
{
  int cache_size = 0;
  int block_size = 0;
  int flags;
  int filemode;
  GDBM_FILE db;
  int n;
  
  switch (variable_get ("cachesize", VART_INT, (void**) &cache_size))
    {
    case VAR_OK:
    case VAR_ERR_NOTSET:
      break;
    default:
      abort ();
    }
  switch (variable_get ("blocksize", VART_INT, (void**) &block_size))
    {
    case VAR_OK:
    case VAR_ERR_NOTSET:
      break;
    default:
      abort ();
    }
  
  if (variable_get ("open", VART_INT, (void**) &flags) != VAR_OK)
    abort ();
  
  if (flags == GDBM_NEWDB)
    {
      if (interactive () && variable_is_true ("confirm") &&
	  access (dbname, F_OK) == 0)
	{
	  if (!getyn (_("database %s already exists; overwrite"), dbname))
	    return 1;
	}
    }

  if (variable_get ("format", VART_INT, (void**) &n) != VAR_OK)
    abort ();

  flags |= n;
  
  if (!variable_is_true ("lock"))
    flags |= GDBM_NOLOCK;
  if (!variable_is_true ("mmap"))
    flags |= GDBM_NOMMAP;
  if (variable_is_true ("sync"))
    flags |= GDBM_SYNC;
  
  if (variable_get ("filemode", VART_INT, (void**) &filemode))
    abort ();

  if (fd > 0)
    db = gdbm_fd_open (fd, dbname, block_size, flags | GDBM_CLOERROR, NULL);
  else
    {
      char *name = tildexpand (dbname);
      db = gdbm_open (name, block_size, flags, filemode, NULL);
      free (name);
    }

  if (db == NULL)
    {
      terror (_("cannot open database %s: %s"), dbname,
	      gdbm_strerror (gdbm_errno));
      return 1;
    }

  if (cache_size &&
      gdbm_setopt (db, GDBM_CACHESIZE, &cache_size, sizeof (int)) == -1)
    terror (_("gdbm_setopt failed: %s"), gdbm_strerror (gdbm_errno));

  if (variable_is_true ("coalesce"))
    {
      gdbmshell_setopt ("GDBM_SETCOALESCEBLKS", GDBM_SETCOALESCEBLKS, 1);
    }
  if (variable_is_true ("centfree"))
    {
      gdbmshell_setopt ("GDBM_SETCENTFREE", GDBM_SETCENTFREE, 1);
    }
  
  if (gdbm_file)
    gdbm_close (gdbm_file);
  
  gdbm_file = db;
  return 0;
}

static int
checkdb (void)
{
  if (!gdbm_file)
    {
      char *filename;
      int fd = -1;
      variable_get ("filename", VART_STRING, (void**) &filename);
      variable_get ("fd", VART_INT, (void**) &fd);
      return opendb (filename, fd);
    }
  return 0;
}

static int
checkdb_begin (struct command_param *param GDBM_ARG_UNUSED,
	       struct command_environ *cenv GDBM_ARG_UNUSED,
	       size_t *exp_count GDBM_ARG_UNUSED)
{
  return checkdb ();
}

static size_t
bucket_print_lines (hash_bucket *bucket)
{
  return 6 + gdbm_file->header->bucket_elems + 3 + bucket->av_count;
}

static void
format_key_start (FILE *fp, bucket_element *elt)
{
  int size = SMALL < elt->key_size ? SMALL : elt->key_size;
  int i;

  for (i = 0; i < size; i++)
    {
      if (isprint (elt->key_start[i]))
	fprintf (fp, "   %c", elt->key_start[i]);
      else
	fprintf (fp, " %03o", elt->key_start[i]);
    }
}

/* Debug procedure to print the contents of the current hash bucket. */
static void
print_bucket (FILE *fp, hash_bucket *bucket, const char *mesg, ...)
{
  int index;
  va_list ap;

  fprintf (fp, "******* ");
  va_start(ap, mesg);
  vfprintf (fp, mesg, ap);
  va_end (ap);
  fprintf (fp, " **********\n\n");
  fprintf (fp,
	   _("bits = %d\ncount= %d\nHash Table:\n"),
	   bucket->bucket_bits, bucket->count);
  fprintf (fp,
	   _("    #    hash value     key size    data size     data adr home  key start\n"));
  for (index = 0; index < gdbm_file->header->bucket_elems; index++)
    {
      fprintf (fp, " %4d  %12x  %11d  %11d  %11lu %4d", index,
	       bucket->h_table[index].hash_value,
	       bucket->h_table[index].key_size,
	       bucket->h_table[index].data_size,
	       (unsigned long) bucket->h_table[index].data_pointer,
	       bucket->h_table[index].hash_value %
	       gdbm_file->header->bucket_elems);
      if (bucket->h_table[index].key_size)
	{
	  fprintf (fp, " ");
	  format_key_start (fp, &bucket->h_table[index]);
	}
      fprintf (fp, "\n");
    }

  fprintf (fp, _("\nAvail count = %1d\n"), bucket->av_count);
  fprintf (fp, _("Address           size\n"));
  for (index = 0; index < bucket->av_count; index++)
    fprintf (fp, "%11lu%9d\n",
	     (unsigned long) bucket->bucket_avail[index].av_adr,
	     bucket->bucket_avail[index].av_size);
}

struct avail_list_counter
{
  size_t min_size;
  size_t lines;
};

static int
avail_list_count (avail_block *avblk, off_t off, void *data)
{
  struct avail_list_counter *ctr = data;

  ctr->lines += avblk->count;
  return ctr->lines > ctr->min_size;
} 
  
static size_t
_gdbm_avail_list_size (GDBM_FILE dbf, size_t min_size)
{
  struct avail_list_counter ctr;
  ctr.min_size = 0;
  ctr.lines = 0;
  gdbm_avail_traverse (dbf, avail_list_count, &ctr);
  return ctr.lines;
}

static void
av_table_display (avail_elem *av_table, int count, FILE *fp)
{
  int i;
  
  for (i = 0; i < count; i++)
    {
      fprintf (fp, "  %15d   %10lu \n",
	       av_table[i].av_size, (unsigned long) av_table[i].av_adr);
    }
}

static int
avail_list_print (avail_block *avblk, off_t n, void *data)
{
  FILE *fp = data;
  
  fputc ('\n', fp);
  if (n == 0)//FIXME
    fprintf (fp, "%s", _("header block"));
  else
    fprintf (fp, _("block = %lu"), (unsigned long) n);
  fprintf (fp, _("\nsize  = %d\ncount = %d\n"),
	   avblk->size, avblk->count);
  av_table_display (avblk->av_table, avblk->count, fp);
  return 0;
}

static void
_gdbm_print_avail_list (FILE *fp, GDBM_FILE dbf)
{
  if (gdbm_avail_traverse (dbf, avail_list_print, fp))
    terror ("%s", gdbm_strerror (gdbm_errno));
}

static void
_gdbm_print_bucket_cache (FILE *fp, GDBM_FILE dbf)
{
  if (dbf->cache_num)
    {
      int i;
      cache_elem *elem;
  
      fprintf (fp,
	_("Bucket Cache (size %zu/%zu):\n  Index:         Address  Changed  Data_Hash \n"),
	       dbf->cache_num, dbf->cache_size);
      for (elem = dbf->cache_entry, i = 0; elem; elem = elem->ca_next, i++)
	{
	  fprintf (fp, "  %5d:  %15lu %7s  %x\n",
		   i,
		   (unsigned long) elem->ca_adr,
		   (elem->ca_changed ? _("True") : _("False")),
		   elem->ca_data.hash_val);
	}
    }
  else
    fprintf (fp, _("Bucket cache is empty.\n"));
}

static int
trimnl (char *str)
{
  int len = strlen (str);

  if (str[len - 1] == '\n')
    {
      str[--len] = 0;
      return 1;
    }
  return 0;
}

static int
get_screen_lines (void)
{
#ifdef TIOCGWINSZ
  if (isatty (1))
    {
      struct winsize ws;

      ws.ws_col = ws.ws_row = 0;
      if ((ioctl(1, TIOCGWINSZ, (char *) &ws) < 0) || ws.ws_row == 0)
	{
	  const char *lines = getenv ("LINES");
	  if (lines)
	    ws.ws_row = strtol (lines, NULL, 10);
	}
      return ws.ws_row;
    }
#else
  const char *lines = getenv ("LINES");
  if (lines)
    return strtol (lines, NULL, 10);
#endif
  return -1;
}

/* Open database */
static void
open_handler (struct command_param *param,
	      struct command_environ *cenv GDBM_ARG_UNUSED)
{
  char *filename;
  int fd = -1;

  closedb ();

  if (param->argc == 1)
    filename = PARAM_STRING (param, 0);
  else
    {
      variable_get ("filename", VART_STRING, (void**) &filename);
      variable_get ("fd", VART_INT, (void**) &fd);
    }
  
  if (opendb (filename, fd) == 0)
    {
      variable_set ("filename", VART_STRING, filename);
      if (fd >= 0)
	variable_set ("fd", VART_INT, &fd);
      else
	variable_unset ("fd");
    }
}

/* Close database */
static void
close_handler (struct command_param *param GDBM_ARG_UNUSED,
	       struct command_environ *cenv GDBM_ARG_UNUSED)
{
  if (!gdbm_file)
    terror (_("nothing to close"));
  else
    closedb ();
}

static char *
count_to_str (gdbm_count_t count, char *buf, size_t bufsize)
{
  char *p = buf + bufsize;

  *--p = 0;
  if (count == 0)
    *--p = '0';
  else
    while (count)
      {
	if (p == buf)
	  return NULL;
	*--p = '0' + count % 10;
	count /= 10;
      }
  return p;
}
  
/* count - count items in the database */
static void
count_handler (struct command_param *param GDBM_ARG_UNUSED,
	       struct command_environ *cenv)
{
  gdbm_count_t count;

  if (gdbm_count (gdbm_file, &count))
    terror ("gdbm_count: %s", gdbm_strerror (gdbm_errno));
  else
    {
      char buf[128];
      char *p = count_to_str (count, buf, sizeof buf);

      if (!p)
	terror (_("count buffer overflow"));
      else
	fprintf (cenv->fp, 
		 ngettext ("There is %s item in the database.\n",
			   "There are %s items in the database.\n",
			   count),
		 p);
    }
}

/* delete KEY - delete a key*/
static void
delete_handler (struct command_param *param, struct command_environ *cenv)
{
  if (gdbm_delete (gdbm_file, PARAM_DATUM (param, 0)) != 0)
    {
      if (gdbm_errno == GDBM_ITEM_NOT_FOUND)
	terror (_("Item not found"));
      else
	terror (_("Can't delete: %s"), gdbm_strerror (gdbm_errno));
    }
}

/* fetch KEY - fetch a record by its key */
static void
fetch_handler (struct command_param *param, struct command_environ *cenv)
{
  return_data = gdbm_fetch (gdbm_file, PARAM_DATUM (param, 0));
  if (return_data.dptr != NULL)
    {
      datum_format (cenv->fp, &return_data, dsdef[DS_CONTENT]);
      fputc ('\n', cenv->fp);
      datum_free (&return_data);
    }
  else if (gdbm_errno == GDBM_ITEM_NOT_FOUND)
    terror ("%s", _("No such item found."));
  else
    terror (_("Can't fetch data: %s"), gdbm_strerror (gdbm_errno));
}

/* store KEY DATA - store data */
static void
store_handler (struct command_param *param,
	       struct command_environ *cenv GDBM_ARG_UNUSED)
{
  if (gdbm_store (gdbm_file,
		  PARAM_DATUM (param, 0), PARAM_DATUM (param, 1),
		  GDBM_REPLACE) != 0)
    terror (_("Item not inserted: %s."), gdbm_db_strerror (gdbm_file));
}

/* first - begin iteration */

static void
firstkey_handler (struct command_param *param, struct command_environ *cenv)
{
  datum_free (&key_data);
  key_data = gdbm_firstkey (gdbm_file);
  if (key_data.dptr != NULL)
    {
      datum_format (cenv->fp, &key_data, dsdef[DS_KEY]);
      fputc ('\n', cenv->fp);

      return_data = gdbm_fetch (gdbm_file, key_data);
      datum_format (cenv->fp, &return_data, dsdef[DS_CONTENT]);
      fputc ('\n', cenv->fp);

      datum_free (&return_data);
    }
  else if (gdbm_errno == GDBM_ITEM_NOT_FOUND)
    fprintf (cenv->fp, _("No such item found.\n"));
  else
    terror (_("Can't find key: %s"), gdbm_strerror (gdbm_errno));
}

/* next [KEY] - next key */
static void
nextkey_handler (struct command_param *param, struct command_environ *cenv)
{
  if (param->argc == 1)
    {
      datum_free (&key_data);
      key_data.dptr = emalloc (PARAM_DATUM (param, 0).dsize);
      key_data.dsize = PARAM_DATUM (param, 0).dsize;
      memcpy (key_data.dptr, PARAM_DATUM (param, 0).dptr, key_data.dsize);
    }
  return_data = gdbm_nextkey (gdbm_file, key_data);
  if (return_data.dptr != NULL)
    {
      datum_free (&key_data);
      key_data = return_data;
      datum_format (cenv->fp, &key_data, dsdef[DS_KEY]);
      fputc ('\n', cenv->fp);

      return_data = gdbm_fetch (gdbm_file, key_data);
      datum_format (cenv->fp, &return_data, dsdef[DS_CONTENT]);
      fputc ('\n', cenv->fp);

      datum_free (&return_data);
    }
  else if (gdbm_errno == GDBM_ITEM_NOT_FOUND)
    {
      terror ("%s", _("No such item found."));
      datum_free (&key_data);
    }
  else
    terror (_("Can't find key: %s"), gdbm_strerror (gdbm_errno));
}

/* reorganize */
static void
reorganize_handler (struct command_param *param GDBM_ARG_UNUSED,
		    struct command_environ *cenv)
{
  if (gdbm_reorganize (gdbm_file))
    terror ("%s", _("Reorganization failed."));
  else
    fprintf (cenv->fp, "%s\n", _("Reorganization succeeded."));
}

static void
err_printer (void *data GDBM_ARG_UNUSED, char const *fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fprintf (stderr, "\n");
}

/* recover sumamry verbose backup max-failed-keys=N max-failed-buckets=N max-failures=N */
static void
recover_handler (struct command_param *param, struct command_environ *cenv)
{
  gdbm_recovery rcvr;
  int flags = 0;
  int rc;
  int i;
  char *p;
  int summary = 0;
  
  for (i = 0; i < param->argc; i++)
    {
      char *arg = PARAM_STRING (param, i);
      if (strcmp (arg, "verbose") == 0)
	{
	  rcvr.errfun = err_printer;
	  flags |= GDBM_RCVR_ERRFUN;
	}
      else if (strcmp (arg, "force") == 0)
	{
	  flags |= GDBM_RCVR_FORCE;
	}
      else if (strcmp (arg, "summary") == 0)
	{
	  summary = 1;
	}
      else if (strcmp (arg, "backup") == 0)
	{
	  flags |= GDBM_RCVR_BACKUP;
	}
      else if (strncmp (arg, "max-failures=", 13) == 0)
	{
	  rcvr.max_failures = strtoul (arg + 13, &p, 10);
	  if (*p)
	    {
	      printf (_("not a number (stopped near %s)\n"), p);
	      return;
	    }
	  flags |= GDBM_RCVR_MAX_FAILURES;
	}
      else if (strncmp (arg, "max-failed-keys=", 16) == 0)
	{
	  rcvr.max_failed_keys = strtoul (arg + 16, &p, 10);
	  if (*p)
	    {
	      printf (_("not a number (stopped near %s)\n"), p);
	      return;
	    }
	  flags |= GDBM_RCVR_MAX_FAILED_KEYS;
	}
      else if (strncmp (arg, "max-failed-buckets=", 19) == 0)
	{
	  rcvr.max_failures = strtoul (arg + 19, &p, 10);
	  if (*p)
	    {
	      printf (_("not a number (stopped near %s)\n"), p);
	      return;
	    }
	  flags |= GDBM_RCVR_MAX_FAILED_BUCKETS;
	}
      else
	{
	  terror (_("unrecognized argument: %s"), arg);
	  return;
	}
    }

  rc = gdbm_recover (gdbm_file, &rcvr, flags);

  if (rc == 0)
    {
      fprintf (cenv->fp, _("Recovery succeeded.\n"));
      if (summary)
	{
	  fprintf (cenv->fp,
		   _("Keys recovered: %lu, failed: %lu, duplicate: %lu\n"),
		   (unsigned long) rcvr.recovered_keys,
		   (unsigned long) rcvr.failed_keys,
		   (unsigned long) rcvr.duplicate_keys);
	  fprintf (cenv->fp,
		   _("Buckets recovered: %lu, failed: %lu\n"),
		   (unsigned long) rcvr.recovered_buckets,
		   (unsigned long) rcvr.failed_buckets);
	}
      
      if (rcvr.backup_name)
	{
	  fprintf (cenv->fp,
		   _("Original database preserved in file %s"),
		   rcvr.backup_name);
	  free (rcvr.backup_name);
	}
      fputc ('\n', cenv->fp);
    }
  else
    {
      fprintf (stderr, _("Recovery failed: %s"), gdbm_strerror (gdbm_errno));
      if (gdbm_syserr[gdbm_errno])
	fprintf (stderr, ": %s", strerror (errno));
      fputc ('\n', stderr);
    }
}  

/* avail - print available list */
static int
avail_begin (struct command_param *param GDBM_ARG_UNUSED,
	     struct command_environ *cenv GDBM_ARG_UNUSED,
	     size_t *exp_count)
{
  if (checkdb ())
    return 1;
  if (exp_count)
    *exp_count = _gdbm_avail_list_size (gdbm_file, SIZE_T_MAX);
  return 0;
}

static void
avail_handler (struct command_param *param GDBM_ARG_UNUSED,
	       struct command_environ *cenv)
{
  _gdbm_print_avail_list (cenv->fp, gdbm_file);
}

/* print current bucket */
static int
print_current_bucket_begin (struct command_param *param GDBM_ARG_UNUSED,
			    struct command_environ *cenv GDBM_ARG_UNUSED,
			    size_t *exp_count)
{
  if (checkdb ())
    return 1;
  if (!gdbm_file->bucket)
    return 0;
  if (exp_count)
    *exp_count = gdbm_file->bucket
                       ? bucket_print_lines (gdbm_file->bucket) + 3
                       : 1;
  return 0;
}

static void
print_current_bucket_handler (struct command_param *param,
			      struct command_environ *cenv)
{
  if (!gdbm_file->bucket)
    fprintf (cenv->fp, _("no current bucket\n"));
  else
    {
      if (param->argc)
	print_bucket (cenv->fp, gdbm_file->bucket, _("Bucket #%s"),
		      PARAM_STRING (param, 0));
      else
	print_bucket (cenv->fp, gdbm_file->bucket, "%s", _("Current bucket"));
      fprintf (cenv->fp, _("\n current directory entry = %d.\n"),
	       gdbm_file->bucket_dir);
      fprintf (cenv->fp, _(" current bucket address  = %lu.\n"),
	       (unsigned long) gdbm_file->cache_entry->ca_adr);
    }
}

int
getnum (int *pnum, char *arg, char **endp)
{
  char *p;
  unsigned long x = strtoul (arg, &p, 10);
  if (*p && !isspace (*p))
    {
      printf (_("not a number (stopped near %s)\n"), p);
      return 1;
    }
  while (*p && isspace (*p))
    p++;
  if (endp)
    *endp = p;
  else if (*p)
    {
      printf (_("not a number (stopped near %s)\n"), p);
      return 1;
    }
  *pnum = x;
  return 0;
}
  
/* bucket NUM - print a bucket and set it as a current one.
   Uses print_current_bucket_handler */
static int
print_bucket_begin (struct command_param *param,
		    struct command_environ *cenv GDBM_ARG_UNUSED,
		    size_t *exp_count)
{
  int temp;

  if (checkdb ())
    return 1;
  
  if (getnum (&temp, PARAM_STRING (param, 0), NULL))
    return 1;

  if (temp >= GDBM_DIR_COUNT (gdbm_file))
    {
      terror (_("Not a bucket."));
      return 1;
    }
  if (_gdbm_get_bucket (gdbm_file, temp))
    {
      terror ("%s", gdbm_db_strerror (gdbm_file));
      return 1;
    }
  if (exp_count)
    *exp_count = bucket_print_lines (gdbm_file->bucket) + 3;
  return 0;
}

/* dir - print hash directory */
static int
print_dir_begin (struct command_param *param GDBM_ARG_UNUSED,
		 struct command_environ *cenv GDBM_ARG_UNUSED,
		 size_t *exp_count)
{
  if (checkdb ())
    return 1;
  if (exp_count)
    *exp_count = GDBM_DIR_COUNT (gdbm_file) + 3;
  return 0;
}

static size_t
bucket_count (void)
{
  size_t count = 0;

  if (gdbm_bucket_count (gdbm_file, &count))
    {
      terror ("gdbm_bucket_count: %s", gdbm_strerror (gdbm_errno));
    }
  return count;
}

static void
print_dir_handler (struct command_param *param GDBM_ARG_UNUSED,
		   struct command_environ *cenv)
{
  int i;
  
  fprintf (cenv->fp, _("Hash table directory.\n"));
  fprintf (cenv->fp, _("  Size =  %d.  Bits = %d,  Buckets = %zu.\n\n"),
	   gdbm_file->header->dir_size, gdbm_file->header->dir_bits,
	   bucket_count ());
  
  for (i = 0; i < GDBM_DIR_COUNT (gdbm_file); i++)
    fprintf (cenv->fp, "  %10d:  %12lu\n",
	     i, (unsigned long) gdbm_file->dir[i]);
}

/* header - print file handler */
static int
print_header_begin (struct command_param *param GDBM_ARG_UNUSED,
		    struct command_environ *cenv GDBM_ARG_UNUSED,
		    size_t *exp_count)
{
  int n;
  
  if (checkdb ())
    return 1;

  switch (gdbm_file->header->header_magic)
    {
    case GDBM_OMAGIC:
    case GDBM_MAGIC:
      n = 14;
      break;

    case GDBM_NUMSYNC_MAGIC:
      n = 19;
      break;

    default:
      abort ();
    }

  if (exp_count)
    *exp_count = n;
  
  return 0;
}

static void
print_header_handler (struct command_param *param GDBM_ARG_UNUSED,
		      struct command_environ *cenv)
{
  FILE *fp = cenv->fp;
  char const *type;

  switch (gdbm_file->header->header_magic)
    {
    case GDBM_OMAGIC:
      type = "GDBM (old)";
      break;

    case GDBM_MAGIC:
      type = "GDBM (standard)";
      break;

    case GDBM_NUMSYNC_MAGIC:
      type = "GDBM (numsync)";
      break;

    default:
      abort ();
    }
  
  fprintf (fp, _("\nFile Header: \n\n"));
  fprintf (fp, _("  type         = %s\n"), type);
  fprintf (fp, _("  table        = %lu\n"),
	   (unsigned long) gdbm_file->header->dir);
  fprintf (fp, _("  table size   = %d\n"), gdbm_file->header->dir_size);
  fprintf (fp, _("  table bits   = %d\n"), gdbm_file->header->dir_bits);
  fprintf (fp, _("  block size   = %d\n"), gdbm_file->header->block_size);
  fprintf (fp, _("  bucket elems = %d\n"), gdbm_file->header->bucket_elems);
  fprintf (fp, _("  bucket size  = %d\n"), gdbm_file->header->bucket_size);
  fprintf (fp, _("  header magic = %x\n"), gdbm_file->header->header_magic);
  fprintf (fp, _("  next block   = %lu\n"),
	   (unsigned long) gdbm_file->header->next_block);

  fprintf (fp, _("  avail size   = %d\n"), gdbm_file->avail->size);
  fprintf (fp, _("  avail count  = %d\n"), gdbm_file->avail->count);
  fprintf (fp, _("  avail nx blk = %lu\n"),
	   (unsigned long) gdbm_file->avail->next_block);

  if (gdbm_file->xheader)
    {
      fprintf (fp, _("\nExtended Header: \n\n"));
      fprintf (fp, _("       version = %d\n"), gdbm_file->xheader->version);  
      fprintf (fp, _("       numsync = %u\n"), gdbm_file->xheader->numsync);
    }
}

static void
sync_handler (struct command_param *param GDBM_ARG_UNUSED,
	      struct command_environ *cenv GDBM_ARG_UNUSED)
{
  if (gdbm_sync (gdbm_file))
    terror ("%s", gdbm_db_strerror (gdbm_file));
}

static void
upgrade_handler (struct command_param *param GDBM_ARG_UNUSED,
		 struct command_environ *cenv GDBM_ARG_UNUSED)
{
  if (gdbm_convert (gdbm_file, GDBM_NUMSYNC))
    terror ("%s", gdbm_db_strerror (gdbm_file));
}

static void
downgrade_handler (struct command_param *param GDBM_ARG_UNUSED,
		   struct command_environ *cenv GDBM_ARG_UNUSED)
{
  if (gdbm_convert (gdbm_file, 0))
    terror ("%s", gdbm_db_strerror (gdbm_file));
}

struct snapshot_status_info
{
  char const *code;
  char const *descr;
  void (*fn) (FILE *, char const *, char const *);
};

#define MODBUFSIZE 10

static char *
decode_mode (mode_t mode, char *buf)
{
  char *s = buf;
  *s++ = mode & S_IRUSR ? 'r' : '-';
  *s++ = mode & S_IWUSR ? 'w' : '-';
  *s++ = (mode & S_ISUID
	       ? (mode & S_IXUSR ? 's' : 'S')
	       : (mode & S_IXUSR ? 'x' : '-'));
  *s++ = mode & S_IRGRP ? 'r' : '-';
  *s++ = mode & S_IWGRP ? 'w' : '-';
  *s++ = (mode & S_ISGID
	       ? (mode & S_IXGRP ? 's' : 'S')
	       : (mode & S_IXGRP ? 'x' : '-'));
  *s++ = mode & S_IROTH ? 'r' : '-';
  *s++ = mode & S_IWOTH ? 'w' : '-';
  *s++ = (mode & S_ISVTX
	       ? (mode & S_IXOTH ? 't' : 'T')
	       : (mode & S_IXOTH ? 'x' : '-'));
  *s = '\0';
  return buf;
}

struct error_entry
{
  const char *msg;
  int gdbm_err;
  int sys_err;
};

static void
error_push (struct error_entry *stk, int *tos, int maxstk, char const *text,
	    int gdbm_err, int sys_err)
{
  if (*tos == maxstk)
    abort ();
  stk += *tos;
  ++ *tos;
  stk->msg = text;
  stk->gdbm_err = gdbm_err;
  stk->sys_err = sys_err;
}

static void
print_snapshot (char const *snapname, FILE *fp)
{
  struct stat st;
  char buf[MODBUFSIZE];

  if (stat (snapname, &st) == 0)
    {
# define MAXERRS 4
      struct error_entry errs[MAXERRS];
      int errn = 0;
      int i;
      
      switch (st.st_mode & ~S_IFREG)
	{
	case S_IRUSR:
	case S_IWUSR:
	  break;
	  
	default:
	  error_push (errs, &errn, ARRAY_SIZE (errs), N_("bad file mode"), 
                      0, 0);
	}
      
      fprintf (fp, "%s: ", snapname);
      fprintf (fp, "%03o %s ", st.st_mode & 0777,
	       decode_mode (st.st_mode, buf));
      fprintf (fp, "%ld.%09ld", st.st_mtim.tv_sec, st.st_mtim.tv_nsec);
      if (S_ISREG (st.st_mode))
	{
	  GDBM_FILE dbf;

	  dbf = gdbm_open (snapname, 0, GDBM_READER, 0, NULL);
	  if (dbf)
	    {
	      if (dbf->xheader)
		fprintf (fp, " %u", dbf->xheader->numsync);
	      else
		/* TRANSLATORS: Stands for "Not Available". */
		fprintf (fp, " %s", _("N/A"));
	    }
	  else if (gdbm_check_syserr (gdbm_errno))
	    {
	      if (errno == EACCES)
		fprintf (fp, " ?");
	      else
		error_push (errs, &errn, ARRAY_SIZE (errs),
			    N_("can't open database"),
			    gdbm_errno, errno);
	    }
	  else
	    error_push (errs, &errn, ARRAY_SIZE (errs),
			N_("can't open database"),
			gdbm_errno, 0);
	}
      else
	error_push (errs, &errn, ARRAY_SIZE (errs),
		    N_("not a regular file"),
		    0, 0);
      fputc ('\n', fp);
      for (i = 0; i < errn; i++)
	{
	  fprintf (fp, "%s: %s: %s", snapname, _("ERROR"), gettext (errs[i].msg));
	  if (errs[i].gdbm_err)
	    fprintf (fp, ": %s", gdbm_strerror (errs[i].gdbm_err));
	  if (errs[i].sys_err)
	    fprintf (fp, ": %s", strerror (errs[i].sys_err));
	  fputc ('\n', fp);
	}	  
    }
  else
    {
      fprintf (fp, _("%s: ERROR: can't stat: %s"), snapname, strerror (errno));
      return;
    }
}

static void
snapshot_print_fn (FILE *fp, char const *sa, char const *sb)
{
  print_snapshot (sa, fp);
  print_snapshot (sb, fp);
}

static void
snapshot_err_fn (FILE *fp, char const *sa, char const *sb)
{
  switch (errno)
    {
    default:
      print_snapshot (sa, fp);
      print_snapshot (sb, fp);
      break;
      
    case EINVAL:
      fprintf (fp, "%s.\n",
	       _("Invalid arguments in call to gdbm_latest_snapshot"));
      break;
      
    case ENOSYS:
      fprintf (fp, "%s.\n",
	       _("Function is not implemented: GDBM is built without crash-tolerance support"));
      break;
    }      
}

static struct snapshot_status_info snapshot_status_info[] = {
  [GDBM_SNAPSHOT_OK] = {
    "GDBM_SNAPSHOT_OK",
    N_("Selected the most recent snapshot")
  },
  [GDBM_SNAPSHOT_BAD] = {
    "GDBM_SNAPSHOT_BAD",
    N_("Neither snapshot is readable"),
    snapshot_print_fn
  },
  [GDBM_SNAPSHOT_ERR] = {
    "GDBM_SNAPSHOT_ERR",
    N_("Error selecting snapshot"),
    snapshot_err_fn
  },
  [GDBM_SNAPSHOT_SAME] = {
    "GDBM_SNAPSHOT_SAME",
    N_("Snapshot modes and dates are the same"),
    snapshot_print_fn
  },
  [GDBM_SNAPSHOT_SUSPICIOUS] = {
    "GDBM_SNAPSHOT_SUSPICIOUS",
    N_("Snapshot sync counters differ by more than 1"),
    snapshot_print_fn
  }
};
    
void
snapshot_handler (struct command_param *param, struct command_environ *cenv)
{
  char *sa = tildexpand (PARAM_STRING (param, 0));
  char *sb = tildexpand (PARAM_STRING (param, 1));
  char const *sel;
  int rc = gdbm_latest_snapshot (sa, sb, &sel); 

  if (rc >= 0 && rc < ARRAY_SIZE (snapshot_status_info))
    {
      fprintf (cenv->fp,
	       "%s: %s.\n", 
	       snapshot_status_info[rc].code,
	       gettext (snapshot_status_info[rc].descr));
      if (snapshot_status_info[rc].fn)
	snapshot_status_info[rc].fn (cenv->fp, sa, sb);
      if (rc == GDBM_SNAPSHOT_OK)
	print_snapshot (sel, cenv->fp);
    }
  else
    terror (_("unexpected error code: %d"), rc);
}


/* hash KEY - hash the key */
static void
hash_handler (struct command_param *param GDBM_ARG_UNUSED,
	      struct command_environ *cenv)
{
  if (gdbm_file)
    {
      int hashval, bucket, off;
      _gdbm_hash_key (gdbm_file, PARAM_DATUM (param, 0),
		       &hashval, &bucket, &off);
      fprintf (cenv->fp, _("hash value = %x, bucket #%u, slot %u"),
	       hashval,
	       hashval >> (GDBM_HASH_BITS - gdbm_file->header->dir_bits),
	       hashval % gdbm_file->header->bucket_elems);
    }
  else
    fprintf (cenv->fp, _("hash value = %x"),
	     _gdbm_hash (PARAM_DATUM (param, 0)));
  fprintf (cenv->fp, ".\n");
}

/* cache - print the bucket cache */
static int
print_cache_begin (struct command_param *param GDBM_ARG_UNUSED,
		   struct command_environ *cenv GDBM_ARG_UNUSED,
		   size_t *exp_count)
{
  if (checkdb ())
    return 1;
  if (exp_count)
    *exp_count = gdbm_file->cache_num + 1;
  return 0;
}

static void
print_cache_handler (struct command_param *param GDBM_ARG_UNUSED,
		     struct command_environ *cenv)
{
  _gdbm_print_bucket_cache (cenv->fp, gdbm_file);
}

/* version - print GDBM version */
static void
print_version_handler (struct command_param *param GDBM_ARG_UNUSED,
		       struct command_environ *cenv)
{
  fprintf (cenv->fp, "%s\n", gdbm_version);
}

/* list - List all entries */
static int
list_begin (struct command_param *param GDBM_ARG_UNUSED,
	    struct command_environ *cenv GDBM_ARG_UNUSED,
	    size_t *exp_count)
{
  if (checkdb ())
    return 1;
  if (exp_count)
    {
      gdbm_count_t count;

      if (gdbm_count (gdbm_file, &count))
	*exp_count = 0;
      else if (count > SIZE_T_MAX)
	*exp_count = SIZE_T_MAX;
      else
	*exp_count = count;
    }

  return 0;
}

static void
list_handler (struct command_param *param GDBM_ARG_UNUSED,
	      struct command_environ *cenv)
{
  datum key;
  datum data;

  key = gdbm_firstkey (gdbm_file);
  while (key.dptr)
    {
      datum nextkey = gdbm_nextkey (gdbm_file, key);

      data = gdbm_fetch (gdbm_file, key);
      if (!data.dptr)
	 {
	   terror (_("%s; the key was:"), gdbm_db_strerror (gdbm_file));
	   datum_format (stderr, &key, dsdef[DS_KEY]);
	 }
      else
	 {
	   datum_format (cenv->fp, &key, dsdef[DS_KEY]);
	   fputc (' ', cenv->fp);
	   datum_format (cenv->fp, &data, dsdef[DS_CONTENT]);
	   fputc ('\n', cenv->fp);
	   free (data.dptr);
	 }
      free (key.dptr);
      key = nextkey;
    }
}

/* quit - quit the program */
static void
quit_handler (struct command_param *param GDBM_ARG_UNUSED,
	      struct command_environ *cenv GDBM_ARG_UNUSED)
{
  while (!input_context_pop ())
    ;
  if (input_context_push (instream_null_create ()))
    exit (EXIT_FATAL);
}

/* export FILE [truncate] - export to a flat file format */
static void
export_handler (struct command_param *param,
		struct command_environ *cenv GDBM_ARG_UNUSED)
{
  int format = GDBM_DUMP_FMT_ASCII;
  int flags = GDBM_WRCREAT;
  int i;
  int filemode;

  for (i = 1; i < param->argc; i++)
    {
      if (strcmp (PARAM_STRING (param, i), "truncate") == 0)
	 flags = GDBM_NEWDB;
      else if (strcmp (PARAM_STRING (param, i), "binary") == 0)
	 format = GDBM_DUMP_FMT_BINARY;
      else if (strcmp (PARAM_STRING (param, i), "ascii") == 0)
	 format = GDBM_DUMP_FMT_ASCII;
      else
	 {
	   terror (_("unrecognized argument: %s"), PARAM_STRING (param, i));
	   return;
	 }
    }

  if (variable_get ("filemode", VART_INT, (void**) &filemode))
    abort ();
  if (gdbm_dump (gdbm_file, PARAM_STRING (param, 0), format, flags, filemode))
    {
      terror (_("error dumping database: %s"),
	      gdbm_strerror (gdbm_errno));
    }
}

/* import FILE [replace] [nometa] - import from a flat file */
static void
import_handler (struct command_param *param,
		struct command_environ *cenv GDBM_ARG_UNUSED)
{
  int flag = GDBM_INSERT;
  unsigned long err_line;
  int meta_mask = 0;
  int i;
  int rc;
  char *file_name;
  
  for (i = 0; i < param->argc; i++)
    {
      if (strcmp (PARAM_STRING (param, i), "replace") == 0)
	 flag = GDBM_REPLACE;
      else if (strcmp (PARAM_STRING (param, i), "nometa") == 0)
	 meta_mask = GDBM_META_MASK_MODE | GDBM_META_MASK_OWNER;
      else
	 {
	   terror (_("unrecognized argument: %s"),
		   PARAM_STRING (param, i));
	   return;
	 }
    }

  rc = gdbm_load (&gdbm_file, PARAM_STRING (param, 0), flag,
		   meta_mask, &err_line);
  if (rc && gdbm_errno == GDBM_NO_DBNAME)
    {
      char *save_mode;

      variable_get ("open", VART_STRING, (void**) &save_mode);
      save_mode = estrdup (save_mode);
      variable_set ("open", VART_STRING, "newdb");

      rc = checkdb ();
      variable_set ("open", VART_STRING, save_mode);
      free (save_mode);
      
      if (rc)
	 return;

      rc = gdbm_load (&gdbm_file, PARAM_STRING (param, 0), flag,
		       meta_mask, &err_line);
    }
  if (rc)
    {
      switch (gdbm_errno)
	 {
	 case GDBM_ERR_FILE_OWNER:
	 case GDBM_ERR_FILE_MODE:
	   terror (_("error restoring metadata: %s (%s)"),
			 gdbm_strerror (gdbm_errno), strerror (errno));
	   break;

	 default:
	   if (err_line)
	     terror ("%s:%lu: %s", PARAM_STRING (param, 0), err_line,
		     gdbm_strerror (gdbm_errno));
	   else
	     terror (_("cannot load from %s: %s"), PARAM_STRING (param, 0),
		     gdbm_strerror (gdbm_errno));
	 }
      return;
    }

  free (file_name);
  if (gdbm_setopt (gdbm_file, GDBM_GETDBNAME, &file_name, sizeof (file_name)))
    terror (_("gdbm_setopt failed: %s"), gdbm_strerror (gdbm_errno));
  else
    {
      variable_set ("filename", VART_STRING, file_name);
      variable_unset ("fd");
    }
}

/* status - print current program status */
static void
status_handler (struct command_param *param GDBM_ARG_UNUSED,
		struct command_environ *cenv)
{
  char *file_name;

  variable_get ("filename", VART_STRING, (void**) &file_name);
  fprintf (cenv->fp, _("Database file: %s\n"), file_name);
  if (gdbm_file)
    fprintf (cenv->fp, "%s\n", _("Database is open"));
  else
    fprintf (cenv->fp, "%s\n", _("Database is not open"));
  dsprint (cenv->fp, DS_KEY, dsdef[DS_KEY]);
  dsprint (cenv->fp, DS_CONTENT, dsdef[DS_CONTENT]);
}

#if GDBM_DEBUG_ENABLE
static int
debug_flag_printer (void *data, int flag, char const *tok)
{
  FILE *fp = data;
  fprintf (fp, " %s", tok);
  return 0;
}
#endif

static void
debug_handler (struct command_param *param, struct command_environ *cenv)
{
#if GDBM_DEBUG_ENABLE
  if (param->vararg)
    {
      struct gdbmarg *arg;
      int i;
      
      for (arg = param->vararg, i = 0; arg; arg = arg->next, i++)
	{
	  if (arg->type == GDBM_ARG_STRING)
	    {
	      int flag;
	      int negate;
	      char const *tok = arg->v.string;
	      
	      if (tok[0] == '-')
		{
		  ++tok;
		  negate = 1;
		}
	      else if (tok[0] == '+')
		{
		  ++tok;
		  negate = 0;
		}
	      else
		negate = 0;
	      
              flag = gdbm_debug_token (tok);
	      if (flag)
		{
		  if (negate)
		    gdbm_debug_flags &= ~flag;
		  else
		    gdbm_debug_flags |= flag;
		}
	      else
		terror (_("unknown debug flag: %s"), tok);
	    }
	  else
	    terror (_("invalid type of argument %d"), i);
	}
    }
  else
    {
      fprintf (cenv->fp, _("Debug flags:"));
      if (gdbm_debug_flags)
	{
	  gdbm_debug_parse_state (debug_flag_printer, cenv->fp);
	}
      else
	fprintf (cenv->fp, " %s", _("none"));
      fputc ('\n', cenv->fp);
    }
#else
  terror ("%s", _("compiled without debug support"));
#endif
}

static void
shell_handler (struct command_param *param,
	       struct command_environ *cenv GDBM_ARG_UNUSED)
{
  char *argv[4];
  pid_t pid, rc;
  int status;
  
  argv[0] = getenv ("$SHELL");
  if (!argv[0])
    argv[0] = "/bin/sh";
  if (param->vararg)
    {
      argv[1] = "-c";
      argv[2] = param->vararg->v.string;
      argv[3] = NULL;
    }
  else
    {
      argv[1] = NULL;
    }

  pid = fork ();
  if (pid == -1)
    {
      terror ("fork: %s", strerror (errno));
      return;
    }
  if (pid == 0)
    {
      execv (argv[0], argv);
      _exit (127);
    }

  rc = waitpid (pid, &status, 0);
  if (rc == -1)
    terror ("waitpid: %s\n", strerror (errno));
  else if (!interactive ())
    {
      if (WIFEXITED (status))
	{
	  if (WEXITSTATUS (status) != 0)
	    terror ("command failed with status %d", WEXITSTATUS (status));
	}
      else if (WIFSIGNALED (status))
	terror ("command terminated on signal %d", WTERMSIG (status));
    }
}

static void
source_handler (struct command_param *param,
		struct command_environ *cenv GDBM_ARG_UNUSED)
{
  char *fname = tildexpand (PARAM_STRING (param, 0));
  instream_t istr = instream_file_create (fname);
  free (fname);
  if (istr && input_context_push (istr) == 0)
    yyparse ();
}


static void help_handler (struct command_param *, struct command_environ *);
static int help_begin (struct command_param *, struct command_environ *,
		       size_t *);

struct argdef
{
  char *name;
  int type;
  int ds;
};

#define NARGS 10

enum command_repeat_type
  {
    REPEAT_NEVER,
    REPEAT_ALWAYS,
    REPEAT_NOARG
  };

struct command
{
  char *name;           /* Command name */
  size_t len;           /* Name length */
  int tok;
  int  (*begin) (struct command_param *param, struct command_environ *cenv, size_t *);
  void (*handler) (struct command_param *param, struct command_environ *cenv);
  void (*end) (void *data);
  struct argdef args[NARGS];
  int variadic;
  enum command_repeat_type repeat;
  char *doc;
};

static struct command command_tab[] = {
#define S(s) #s, sizeof (#s) - 1
  { S(count), T_CMD,
    checkdb_begin, count_handler, NULL,
    { { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("count (number of entries)") },
  { S(delete), T_CMD,
    checkdb_begin, delete_handler, NULL,
    { { N_("KEY"), GDBM_ARG_DATUM, DS_KEY }, { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("delete a record") },
  { S(export), T_CMD,
    checkdb_begin, export_handler, NULL,
    { { N_("FILE"), GDBM_ARG_STRING },
      { "[truncate]", GDBM_ARG_STRING },
      { "[binary|ascii]", GDBM_ARG_STRING },
      { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("export") },
  { S(fetch), T_CMD,
    checkdb_begin, fetch_handler, NULL,
    { { N_("KEY"), GDBM_ARG_DATUM, DS_KEY }, { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("fetch record") },
  { S(import), T_CMD,
    NULL, import_handler, NULL,
    { { N_("FILE"), GDBM_ARG_STRING },
      { "[replace]", GDBM_ARG_STRING },
      { "[nometa]" , GDBM_ARG_STRING },
      { NULL } },
    FALSE,
    FALSE,
    N_("import") },
  { S(list), T_CMD,
    list_begin, list_handler, NULL,
    { { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("list") },
  { S(next), T_CMD,
    checkdb_begin, nextkey_handler, NULL,
    { { N_("[KEY]"), GDBM_ARG_DATUM, DS_KEY },
      { NULL } },
    FALSE,
    REPEAT_NOARG,
    N_("continue iteration: get next key and datum") },
  { S(store), T_CMD,
    checkdb_begin, store_handler, NULL,
    { { N_("KEY"), GDBM_ARG_DATUM, DS_KEY },
      { N_("DATA"), GDBM_ARG_DATUM, DS_CONTENT },
      { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("store") },
  { S(first), T_CMD,
    checkdb_begin, firstkey_handler, NULL,
    { { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("begin iteration: get first key and datum") },
  { S(reorganize), T_CMD,
    checkdb_begin, reorganize_handler, NULL,
    { { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("reorganize") },
  { S(recover), T_CMD,
    checkdb_begin, recover_handler, NULL,
    { { "[verbose]", GDBM_ARG_STRING },
      { "[summary]", GDBM_ARG_STRING },
      { "[backup]",  GDBM_ARG_STRING },
      { "[force]",   GDBM_ARG_STRING },
      { "[max-failed-keys=N]", GDBM_ARG_STRING },
      { "[max-failed-buckets=N]", GDBM_ARG_STRING },
      { "[max-failures=N]", GDBM_ARG_STRING },
      { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("recover the database") },
  { S(avail), T_CMD,
    avail_begin, avail_handler, NULL,
    { { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("print avail list") }, 
  { S(bucket), T_CMD,
    print_bucket_begin, print_current_bucket_handler, NULL,
    { { N_("NUMBER"), GDBM_ARG_STRING },
      { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("print a bucket") },
  { S(current), T_CMD,
    print_current_bucket_begin, print_current_bucket_handler, NULL,
    { { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("print current bucket") },
  { S(dir), T_CMD,
    print_dir_begin, print_dir_handler, NULL,
    { { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("print hash directory") },
  { S(header), T_CMD,
    print_header_begin , print_header_handler, NULL,
    { { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("print database file header") },
  { S(hash), T_CMD,
    NULL, hash_handler, NULL,
    { { N_("KEY"), GDBM_ARG_DATUM, DS_KEY },
      { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("hash value of key") },
  { S(cache), T_CMD,
    print_cache_begin, print_cache_handler, NULL,
    { { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("print the bucket cache") },
  { S(status), T_CMD,
    NULL, status_handler, NULL,
    { { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("print current program status") },
  { S(sync), T_CMD,
    checkdb_begin, sync_handler, NULL,
    { { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("Synchronize the database with disk copy") },  
  { S(upgrade), T_CMD,
    checkdb_begin, upgrade_handler, NULL,
    { { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("Upgrade the database to extended format") },
  { S(downgrade), T_CMD,
    checkdb_begin, downgrade_handler, NULL,
    { { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("Downgrade the database to standard format") },    
  { S(snapshot), T_CMD,
    NULL, snapshot_handler, NULL,
    { { "FILE", GDBM_ARG_STRING },
      { "FILE", GDBM_ARG_STRING },
      { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("analyze two database snapshots") },
  { S(version), T_CMD,
    NULL, print_version_handler, NULL,
    { { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("print version of gdbm") },
  { S(help), T_CMD,
    help_begin, help_handler, NULL,
    { { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("print this help list") },
  { S(quit), T_CMD,
    NULL, quit_handler, NULL,
    { { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("quit the program") },
  { S(set), T_SET,
    NULL, NULL, NULL,
    { { "[VAR=VALUE...]" }, { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("set or list variables") },
  { S(unset), T_UNSET,
    NULL, NULL, NULL,
    { { "VAR..." }, { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("unset variables") },
  { S(define), T_DEF,
    NULL, NULL, NULL,
    { { "key|content", GDBM_ARG_STRING },
      { "{ FIELD-LIST }", GDBM_ARG_STRING },
      { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("define datum structure") },
  { S(source), T_CMD,
    NULL, source_handler, NULL,
    { { "FILE", GDBM_ARG_STRING },
      { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("source command script") },
  { S(close), T_CMD,
    NULL, close_handler, NULL,
    { { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("close the database") },
  { S(open), T_CMD,
    NULL, open_handler, NULL,
    { { "[FILE]", GDBM_ARG_STRING }, { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("open new database") },
#ifdef WITH_READLINE
  { S(history), T_CMD,
    input_history_begin, input_history_handler, NULL,
    { { N_("[FROM]"), GDBM_ARG_STRING },
      { N_("[COUNT]"), GDBM_ARG_STRING },
      { NULL } },
    FALSE,
    REPEAT_NEVER,
    N_("show input history") },
#endif
  { S(debug), T_CMD,
    NULL, debug_handler, NULL,
    { { NULL } },
    TRUE,
    REPEAT_NEVER,
    N_("query/set debug level") },
  { S(shell), T_SHELL,
    NULL, shell_handler, NULL,
    { { NULL } },
    TRUE,
    REPEAT_NEVER,
    N_("invoke the shell") },          
#undef S
  { NULL }
};

static int commands_sorted;

static int
cmdcmp (const void *a, const void *b)
{
  struct command const *ac = a;
  struct command const *bc = b;
  return strcmp (ac->name, bc->name);
}

/* Generator function for command completion.  STATE lets us know whether
   to start from scratch; without any state (i.e. STATE == 0), then we
   start at the top of the list. */
char *
command_generator (const char *text, int state)
{
  const char *name;
  static int len;
  static struct command *cmd;

  /* If this is a new word to complete, initialize now.  This includes
     saving the length of TEXT for efficiency, and initializing the index
     variable to 0. */
  if (!state)
    {
      cmd = command_tab;
      len = strlen (text);
    }

  if (!cmd || !cmd->name)
    return NULL;

  /* Return the next name which partially matches from the command list. */
  while ((name = cmd->name))
    {
      cmd++;
      if (strncmp (name, text, len) == 0)
        return strdup (name);
    }

  /* If no names matched, then return NULL. */
  return NULL;
}

/* ? - help handler */
#define CMDCOLS 30

static int
help_begin (struct command_param *param GDBM_ARG_UNUSED,
	    struct command_environ *cenv GDBM_ARG_UNUSED,
	    size_t *exp_count)
{
  if (exp_count)
    *exp_count = ARRAY_SIZE (command_tab) + 1;
  return 0;
}

static void
help_handler (struct command_param *param GDBM_ARG_UNUSED,
	      struct command_environ *cenv)
{
  struct command *cmd;
  FILE *fp = cenv->fp;
  
  for (cmd = command_tab; cmd->name; cmd++)
    {
      int i;
      int n;
      int optoff;
      
      n = fprintf (fp, " %s", cmd->name);
      optoff = n;
      
      for (i = 0; i < NARGS && cmd->args[i].name; i++)
	{
	  if (n >= CMDCOLS)
	    {
	      fputc ('\n', fp);
	      n = fprintf (fp, "%*.*s", optoff, optoff, "");
	    }
	  n += fprintf (fp, " %s", gettext (cmd->args[i].name));
	} 

      if (n < CMDCOLS)
	fprintf (fp, "%*.s", CMDCOLS-n, "");
      else
	fprintf (fp, "\n%*.*s", CMDCOLS, CMDCOLS, "");
      fprintf (fp, " %s", gettext (cmd->doc));
      fputc ('\n', fp);
    }
}

int
command_lookup (const char *str, struct locus *loc, struct command **pcmd)
{
  enum { fcom_init, fcom_found, fcom_ambig, fcom_abort } state = fcom_init;
  struct command *cmd, *found = NULL;
  size_t len = strlen (str);
  
  for (cmd = command_tab; state != fcom_abort && cmd->name; cmd++)
    {
      size_t n = len < cmd->len ? len : cmd->len;
      if (memcmp (cmd->name, str, n) == 0 && str[n] == 0)
	{
	  switch (state)
	    {
	    case fcom_init:
	      found = cmd;
	      state = fcom_found;
	      break;

	    case fcom_found:
	      if (!interactive ())
		{
		  state = fcom_abort;
		  found = NULL;
		  continue;
		}
	      fprintf (stderr, "ambiguous command: %s\n", str);
	      fprintf (stderr, "    %s\n", found->name);
	      found = NULL;
	      state = fcom_ambig;
	      /* fall through */
	    case fcom_ambig:
	      fprintf (stderr, "    %s\n", cmd->name);
	      break;
	      
	    case fcom_abort:
	      /* should not happen */
	      abort ();
	    }
	}
    }

  if (state == fcom_init)
    lerror (loc, interactive () ? _("Invalid command. Try ? for help.") :
	                          _("Unknown command"));
  if (!found)
    return T_BOGUS;

  *pcmd = found;
  return found->tok;
}

struct gdbmarg *
gdbmarg_string (char *string, struct locus *loc)
{
  struct gdbmarg *arg = ecalloc (1, sizeof (*arg));
  arg->next = NULL;
  arg->type = GDBM_ARG_STRING;
  arg->ref = 1;
  if (loc)
    arg->loc = *loc;
  arg->v.string = string;
  return arg;
}

struct gdbmarg *
gdbmarg_datum (datum *dat, struct locus *loc)
{
  struct gdbmarg *arg = ecalloc (1, sizeof (*arg));
  arg->next = NULL;
  arg->type = GDBM_ARG_DATUM;
  arg->ref = 1;
  if (loc)
    arg->loc = *loc;
  arg->v.dat = *dat;
  return arg;
}

struct gdbmarg *
gdbmarg_kvpair (struct kvpair *kvp, struct locus *loc)
{
  struct gdbmarg *arg = ecalloc (1, sizeof (*arg));
  arg->next = NULL;
  arg->type = GDBM_ARG_KVPAIR;
  arg->ref = 1;
  if (loc)
    arg->loc = *loc;
  arg->v.kvpair = kvp;
  return arg;
}

struct slist *
slist_new_s (char *s)
{
  struct slist *lp = emalloc (sizeof (*lp));
  lp->next = NULL;
  lp->str = s;
  return lp;
}

struct slist *
slist_new (char const *s)
{
  return slist_new_s (estrdup (s));
}

struct slist *
slist_new_l (char const *s, size_t l)
{
  char *copy = emalloc (l + 1);
  memcpy (copy, s, l);
  copy[l] = 0;
  return slist_new_s (copy);
}

void
slist_free (struct slist *lp)
{
  while (lp)
    {
      struct slist *next = lp->next;
      free (lp->str);
      free (lp);
      lp = next;
    }
}

void
slist_insert (struct slist **where, struct slist *what)
{
  if (*where)
    {
      while (what->next)
	what = what->next;
      what->next = (*where)->next;
      (*where)->next = what;
    }
  else
    what->next = NULL;
  *where = what;
}

struct kvpair *
kvpair_string (struct locus *loc, char *val)
{
  struct kvpair *p = ecalloc (1, sizeof (*p));
  p->type = KV_STRING;
  if (loc)
    p->loc = *loc;
  p->val.s = val;
  return p;
}

struct kvpair *
kvpair_list (struct locus *loc, struct slist *s)
{
  struct kvpair *p = ecalloc (1, sizeof (*p));
  p->type = KV_LIST;
  if (loc)
    p->loc = *loc;
  p->val.l = s;
  return p;
}  

void
kvlist_free (struct kvpair *kvp)
{
  while (kvp)
    {
      struct kvpair *next = kvp->next;
      free (kvp->key);
      switch (kvp->type)
	{
	case KV_STRING:
	  free (kvp->val.s);
	  break;

	case KV_LIST:
	  slist_free (kvp->val.l);
	  break;
	}
      free (kvp);
      kvp = next;
    }
}

struct kvpair *
kvlist_find (struct kvpair *kv, char const *tag)
{
  for (; kv; kv = kv->next)
    if (kv->key && strcmp (kv->key, tag) == 0)
      break;
  return kv;
}

int
gdbmarg_free (struct gdbmarg *arg)
{
  if (arg && --arg->ref == 0)
    {
      switch (arg->type)
	{
	case GDBM_ARG_STRING:
	  free (arg->v.string);
	  break;

	case GDBM_ARG_KVPAIR:
	  kvlist_free (arg->v.kvpair);
	  break;

	case GDBM_ARG_DATUM:
	  free (arg->v.dat.dptr);
	  break;
	}
      free (arg);
      return 0;
    }
  return 1;
}

void
gdbmarg_destroy (struct gdbmarg **parg)
{
  if (parg && gdbmarg_free (*parg))
    *parg = NULL;
}

void
gdbmarglist_init (struct gdbmarglist *lst, struct gdbmarg *arg)
{
  if (arg)
    arg->next = NULL;
  lst->head = lst->tail = arg;
}

void
gdbmarglist_add (struct gdbmarglist *lst, struct gdbmarg *arg)
{
  arg->next = NULL;
  if (lst->tail)
    lst->tail->next = arg;
  else
    lst->head = arg;
  lst->tail = arg;
}

void
gdbmarglist_free (struct gdbmarglist *lst)
{
  struct gdbmarg *arg;

  for (arg = lst->head; arg; )
    {
      struct gdbmarg *next = arg->next;
      gdbmarg_free (arg);
      arg = next;
    }
  lst->head = lst->tail = NULL;
}

static void
param_expand (struct command_param *p)
{
  if (p->argc == p->argmax)
    p->argv = e2nrealloc (p->argv, &p->argmax, sizeof (p->argv[0]));
}

static void
param_free_argv (struct command_param *p)
{
  size_t i;

  for (i = 0; i < p->argc; i++)
    gdbmarg_destroy (&p->argv[i]);
  p->argc = 0;
}

static void
param_free (struct command_param *p)
{
  param_free_argv (p);
  free (p->argv);
  p->argv = NULL;
  p->argmax = 0;
}

static struct gdbmarg *coerce (struct gdbmarg *arg, struct argdef *def);

static int
param_push_arg (struct command_param *p, struct gdbmarg *arg,
		struct argdef *def)
{
  param_expand (p);
  if ((p->argv[p->argc] = coerce (arg, def)) == NULL)
    {
      return 1;
    }
  p->argc++;
  return 0;
}

static void
param_term (struct command_param *p)
{
  param_expand (p);
  p->argv[p->argc] = NULL;
}

typedef struct gdbmarg *(*coerce_type_t) (struct gdbmarg *arg,
					  struct argdef *def);

struct gdbmarg *
coerce_ref (struct gdbmarg *arg, struct argdef *def)
{
  ++arg->ref;
  return arg;
}

struct gdbmarg *
coerce_k2d (struct gdbmarg *arg, struct argdef *def)
{
  datum d;
  
  if (datum_scan (&d, dsdef[def->ds], arg->v.kvpair))
    return NULL;
  return gdbmarg_datum (&d, &arg->loc);
}

struct gdbmarg *
coerce_s2d (struct gdbmarg *arg, struct argdef *def)
{
  datum d;
  struct kvpair kvp;

  memset (&kvp, 0, sizeof (kvp));
  kvp.type = KV_STRING;
  kvp.val.s = arg->v.string;
  
  if (datum_scan (&d, dsdef[def->ds], &kvp))
    return NULL;
  return gdbmarg_datum (&d, &arg->loc);
}

#define coerce_fail NULL

coerce_type_t coerce_tab[GDBM_ARG_MAX][GDBM_ARG_MAX] = {
  /*             s            d            k */
  /* s */  { coerce_ref,  coerce_fail, coerce_fail },
  /* d */  { coerce_s2d,  coerce_ref,  coerce_k2d }, 
  /* k */  { coerce_fail, coerce_fail, coerce_ref }
};

char *argtypestr[] = { "string", "datum", "k/v pair" };
  
static struct gdbmarg *
coerce (struct gdbmarg *arg, struct argdef *def)
{
  if (!coerce_tab[def->type][arg->type])
    {
      lerror (&arg->loc, _("cannot coerce %s to %s"),
		    argtypestr[arg->type], argtypestr[def->type]);
      return NULL;
    }
  return coerce_tab[def->type][arg->type] (arg, def);
}

static struct command *last_cmd;
static struct gdbmarglist last_args;

void
run_last_command (void)
{
  if (interactive ())
    {
      if (last_cmd)
	{
	  switch (last_cmd->repeat)
	    {
	    case REPEAT_NEVER:
	      break;
	    case REPEAT_NOARG:
	      gdbmarglist_free (&last_args);
	      /* FALLTHROUGH */
	    case REPEAT_ALWAYS:
	      if (run_command (last_cmd, &last_args))
		exit (EXIT_USAGE);
	      break;
	    default:
	      abort ();
	    }
	}
    }
}

int
run_command (struct command *cmd, struct gdbmarglist *arglist)
{
  int i;
  struct gdbmarg *arg;
  char *pager = NULL;
  char argbuf[128];
  size_t expected_lines, *expected_lines_ptr;
  FILE *pagfp = NULL;
  struct command_param param = HANDLER_PARAM_INITIALIZER;
  struct command_environ cenv = COMMAND_ENVIRON_INITIALIZER;
  
  variable_get ("pager", VART_STRING, (void**) &pager);
  
  arg = arglist ? arglist->head : NULL;

  for (i = 0; cmd->args[i].name && arg; i++, arg = arg->next)
    {
      if (param_push_arg (&param, arg, &cmd->args[i]))
	{
	  param_free (&param);
	  return 1;
	}
    }

  for (; cmd->args[i].name; i++)
    {
      char *argname = cmd->args[i].name;
      struct gdbmarg *t;
      
      if (*argname == '[')
	/* Optional argument */
	break;

      if (!interactive ())
	{
	  terror (_("%s: not enough arguments"), cmd->name);
	  param_free (&param);
	  return 1;
	}
      printf ("%s? ", argname);
      fflush (stdout);
      if (fgets (argbuf, sizeof argbuf, stdin) == NULL)
	{
	  terror (_("unexpected eof"));
	  exit (EXIT_USAGE);
	}

      trimnl (argbuf);
      
      t = gdbmarg_string (estrdup (argbuf), &yylloc);
      if (param_push_arg (&param, t, &cmd->args[i]))
	{
	  param_free (&param);
	  gdbmarg_free (t);
	  return 1;
	}
    }

  if (arg && !cmd->variadic)
    {
      terror (_("%s: too many arguments"), cmd->name);
      param_free (&param);
      return 1;
    }

  /* Prepare for calling the handler */
  param_term (&param);
  param.vararg = arg;
  pagfp = NULL;
      
  expected_lines = 0;
  expected_lines_ptr = (interactive () && pager) ? &expected_lines : NULL;
  if (!(cmd->begin && cmd->begin (&param, &cenv, expected_lines_ptr)))
    {
      if (pager && expected_lines > get_screen_lines ())
	{
	  pagfp = popen (pager, "w");
	  if (pagfp)
	    cenv.fp = pagfp;
	  else
	    {
	      terror (_("cannot run pager `%s': %s"), pager,
		      strerror (errno));
	      pager = NULL;
	      cenv.fp = stdout;
	    }	  
	}
      else
	cenv.fp = stdout;
  
      cmd->handler (&param, &cenv);
      if (cmd->end)
	cmd->end (cenv.data);
      else if (cenv.data)
	free (cenv.data);

      if (pagfp)
	pclose (pagfp);
    }

  param_free (&param);
  
  last_cmd = cmd;
  if (arglist->head != last_args.head)
    {
      gdbmarglist_free (&last_args);
      last_args = *arglist;
    }
  
  return 0;
}

int
gdbmshell_run (int (*init) (void *, instream_t *), void *data)
{
  int rc;
  int i;
  instream_t instream;

  if (!commands_sorted)
    {
      qsort (command_tab, ARRAY_SIZE (command_tab) - 1,
	     sizeof (command_tab[0]), cmdcmp);
      commands_sorted = 1;
    }
  
  /* Initialize variables. */
  dsdef[DS_KEY] = dsegm_new_field (datadef_lookup ("string"), NULL, 1);
  dsdef[DS_CONTENT] = dsegm_new_field (datadef_lookup ("string"), NULL, 1);

  variables_init ();
  variable_set ("open", VART_STRING, "wrcreat");
  variable_set ("pager", VART_STRING, getenv ("PAGER"));

  last_cmd = NULL;
  gdbmarglist_init (&last_args, NULL);
  
  lex_trace (0);

  rc = init (data, &instream);
  if (rc == 0)
    {
      rc = input_context_push (instream);
      if (rc == 0)
	{
	  struct sigaction act, old_act;
	  
	  act.sa_flags = 0;
	  sigemptyset(&act.sa_mask);
	  act.sa_handler = SIG_IGN;
	  sigaction (SIGPIPE, &act, &old_act);
	  /* Welcome message. */
	  if (instream_interactive (instream) && !variable_is_true ("quiet"))
	    printf (_("\nWelcome to the gdbm tool.  Type ? for help.\n\n"));
	  input_init ();
	  rc = yyparse ();
	  yylex_destroy ();
	  input_done ();
	  closedb ();
	  sigaction (SIGPIPE, &old_act, NULL);
	}
      else
	instream_close (instream);
    }

  gdbmarglist_free (&last_args);

  for (i = 0; i < DS_MAX; i++)
    {
      dsegm_list_free (dsdef[i]);
      dsdef[i] = NULL;
    }

  variables_free ();
		   
  return rc;
}

static int
init (void *data, instream_t *pinstr)
{
  *pinstr = data;
  return 0;
}

int
gdbmshell (instream_t input)
{
  return gdbmshell_run (init, input);
}
