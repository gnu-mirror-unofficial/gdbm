/* lock.c - Implement basic file locking for GDBM. */

/* This file is part of GDBM, the GNU data base manager.
   Copyright (C) 2008-2022 Free Software Foundation, Inc.

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

/* Include system configuration before all else. */
#include "autoconf.h"

#include "gdbmdefs.h"

#include <errno.h>

#if HAVE_FLOCK
# ifndef LOCK_SH
#  define LOCK_SH 1
# endif

# ifndef LOCK_EX
#  define LOCK_EX 2
# endif

# ifndef LOCK_NB
#  define LOCK_NB 4
# endif

# ifndef LOCK_UN
#  define LOCK_UN 8
# endif
#endif

#if defined(F_SETLK) && defined(F_RDLCK) && defined(F_WRLCK)
# define HAVE_FCNTL_LOCK 1
#else
# define HAVE_FCNTL_LOCK 0
#endif

/* Return values for try_lock_ functions: */
enum
  {
    TRY_LOCK_OK,    /* Locking succeeded */
    TRY_LOCK_FAIL,  /* File already locked by another process. */
    TRY_LOCK_NEXT   /* Another error (including locking mechanism not
		       available).  The caller should try next locking
		       mechanism. */
		       
  };

/*
 * Locking using flock().
 */
static int
try_lock_flock (GDBM_FILE dbf)
{
#if HAVE_FLOCK
  if (flock (dbf->desc,
	     ((dbf->read_write == GDBM_READER) ? LOCK_SH : LOCK_EX)
	     | LOCK_NB) == 0)
    {
      return TRY_LOCK_OK;
    }
  else if (errno == EWOULDBLOCK)
    {
      return TRY_LOCK_FAIL;
    }
#endif
  return TRY_LOCK_NEXT;
}

static void
unlock_flock (GDBM_FILE dbf)
{
#if HAVE_FLOCK
  flock (dbf->desc, LOCK_UN);
#endif
}

/*
 * Locking via lockf.
 */

static int
try_lock_lockf (GDBM_FILE dbf)
{
#if HAVE_LOCKF
  /*
   * NOTE: lockf will fail with EINVAL unless the database file was opened
   * with write-only permission (O_WRONLY) or with read/write permission
   * (O_RDWR).  This means that this locking mechanism will always fail for
   * databases opened with GDBM_READER,
   */
  if (dbf->read_write != GDBM_READER)
    {
      if (lockf (dbf->desc, F_TLOCK, (off_t)0L) == 0)
	return TRY_LOCK_OK;

      switch (errno)
	{
	case EACCES:
	case EAGAIN:
	case EDEADLK:
	  return TRY_LOCK_FAIL;
	  
	default:
	  /* try next locking method */
	  break;
	}
    }
#endif
  return TRY_LOCK_NEXT;
}

static void
unlock_lockf (GDBM_FILE dbf)
{
#if HAVE_LOCKF
  lockf (dbf->desc, F_ULOCK, (off_t)0L);
#endif
}

/*
 * Locking via fcntl().
 */

static int
try_lock_fcntl (GDBM_FILE dbf)
{
#if HAVE_FCNTL_LOCK
  struct flock fl;

  /* If we're still here, try fcntl. */
  if (dbf->read_write == GDBM_READER)
    fl.l_type = F_RDLCK;
  else
    fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = fl.l_len = (off_t)0L;
  if (fcntl (dbf->desc, F_SETLK, &fl) == 0)
    return TRY_LOCK_OK;

  switch (errno)
    {
    case EACCES:
    case EAGAIN:
    case EDEADLK:
      return TRY_LOCK_FAIL;

    default:
      /* try next locking method */
      break;
    }
  
#endif
  return TRY_LOCK_NEXT;
}

static void
unlock_fcntl (GDBM_FILE dbf)
{
#if HAVE_FCNTL_LOCK
  struct flock fl;

  fl.l_type = F_UNLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = fl.l_len = (off_t)0L;
  fcntl (dbf->desc, F_SETLK, &fl);
#endif
}

/* Try each supported locking mechanism. */
int
_gdbm_lock_file (GDBM_FILE dbf)
{
  int res;

  dbf->lock_type = LOCKING_NONE;
  if ((res = try_lock_flock (dbf)) == TRY_LOCK_OK)
    dbf->lock_type = LOCKING_FLOCK;
  else if (res == TRY_LOCK_NEXT)
    {
      if ((res = try_lock_lockf (dbf)) == TRY_LOCK_OK)
	dbf->lock_type = LOCKING_LOCKF;
      else if (res == TRY_LOCK_NEXT)
	{
	  if (try_lock_fcntl (dbf) == TRY_LOCK_OK)
	    dbf->lock_type = LOCKING_FCNTL;
	}
    }

  return dbf->lock_type == LOCKING_NONE ? -1 : 0;
}

void
_gdbm_unlock_file (GDBM_FILE dbf)
{
  void (*unlock_fn[]) (GDBM_FILE) = {
    [LOCKING_FLOCK] = unlock_flock,
    [LOCKING_LOCKF] = unlock_lockf,
    [LOCKING_FCNTL] = unlock_fcntl
  };

  if (dbf->lock_type != LOCKING_NONE)
    {
      unlock_fn[dbf->lock_type] (dbf);
      dbf->lock_type = LOCKING_NONE;
    }
}

