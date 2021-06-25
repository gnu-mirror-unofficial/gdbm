/* gdbmsync.c - Sync the disk with the in memory state. */

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
   along with GDBM. If not, see <http://www.gnu.org/licenses/>.   */

/* Include system configuration before all else. */
#include "autoconf.h"

#include "gdbmdefs.h"

#ifdef GDBM_FAILURE_ATOMIC

#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

/* Sometimes, to ensure durability, a new file *and* all directories
   on its full path must be fsync()'d up to the root directory.  */
static int
fsync_to_root (const char *f)
{
  char path[PATH_MAX], *p;
  if (realpath (f, path) == NULL)
    return GDBM_ERR_REALPATH;
  while ((p = strrchr (path, '/')) != NULL && (*p = 0, path[0] != 0))
    {
      int fd;

      fd = open (path, O_RDONLY);
      if (fd == -1)
	return GDBM_FILE_OPEN_ERROR;
      if (fsync (fd))
	{
	  close (fd);
	  return GDBM_FILE_SYNC_ERROR;
	}
      if (close (fd))
	return GDBM_FILE_CLOSE_ERROR;
    }
  return GDBM_NO_ERROR;
}

/* Note:  Valgrind complains about ioctl() call below, but it appears
   that Valgrind is simply confused; it issues similar complaints
   about very simple and correct uses of ioctl(FICLONE). */
int
_gdbm_snapshot (GDBM_FILE dbf)
{
  int s;	     /* snapshot file descriptor */
  
  if (dbf->snapfd[0] < 0)
    /* crash consistency hasn't been requested on this database */
    return 0;

  if (!(dbf->eo == 0 || dbf->eo == 1))
    {
      /* Shouldn't happen, but still... */
      _gdbmsync_done (dbf);
      _gdbmsync_init (dbf);
      GDBM_SET_ERRNO (dbf, GDBM_ERR_USAGE, TRUE);
      return -1;      
    }
  
  s = dbf->snapfd[dbf->eo];
  dbf->eo = !dbf->eo;

  /* says "DON'T recover from this snapshot, writing in progress " */
  if (fchmod (s, S_IWUSR)) 
    {
      GDBM_SET_ERRNO (dbf, GDBM_ERR_FILE_MODE, FALSE);
      return -1;
    }

  /* commit permission bits */
  if (fsync (s))
    {
      GDBM_SET_ERRNO (dbf, GDBM_FILE_SYNC_ERROR, FALSE);
      return -1;
    }

  /* make efficient reflink copy into snapshot file, overwrite previous
     contents */
  if (ioctl (s, FICLONE, dbf->desc) == -1)
    {
      if (errno == EINVAL || errno == ENOSYS)
	{
	  _gdbmsync_done (dbf);
	  _gdbmsync_init (dbf);
	}
      GDBM_SET_ERRNO (dbf, GDBM_ERR_SNAPSHOT_CLONE, FALSE);
      return -1;
    }

  /* commit snapshot data */
  if (fsync (s))
    {
      GDBM_SET_ERRNO (dbf, GDBM_FILE_SYNC_ERROR, FALSE);
      return -1;
    }

  /* says "DO recover from this snapshot, writing completed successfully" */
  if (fchmod (s, S_IRUSR))
    {    
      GDBM_SET_ERRNO (dbf, GDBM_ERR_FILE_MODE, FALSE);
      return -1;
    }

  /* commit permission bits again */
  if (fsync (s))
    {
      GDBM_SET_ERRNO (dbf, GDBM_FILE_SYNC_ERROR, FALSE);
      return -1;
    }
  return 0;
}

/* Snapshot files even & odd must not exist already. */
int
gdbm_failure_atomic (GDBM_FILE dbf, const char *even, const char *odd)
{
  int r;
  
  /* Return immediately if the database needs recovery */
  GDBM_ASSERT_CONSISTENCY (dbf, -1);

  if (!even || !odd || strcmp (even, odd) == 0)
    {
      errno = EINVAL;
      GDBM_SET_ERRNO (dbf, GDBM_ERR_USAGE, FALSE);
      return -1;
    }
  
  if (dbf->snapfd[0] != -1)
    {
      /*
       * This function has been called before for this dbf: reinitialize
       * the snapshot system.
       */
      _gdbmsync_done (dbf);
      _gdbmsync_init (dbf);
    }

  dbf->snapfd[0] = open (even, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR);
  if (dbf->snapfd[0] == -1) 
    GDBM_SET_ERRNO (dbf, GDBM_FILE_OPEN_ERROR, FALSE);
  else
    {
      dbf->snapfd[1] = open (odd, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR);
      if (dbf->snapfd[1] == -1)
	GDBM_SET_ERRNO (dbf, GDBM_FILE_OPEN_ERROR, FALSE);
      else if ((r = fsync_to_root (even)) != 0 ||
	       (r = fsync_to_root (odd)) != 0)
	{
	  GDBM_SET_ERRNO (dbf, r, FALSE);
	}
      else
	{
	  dbf->eo = 0;
	  if (_gdbm_snapshot (dbf) == 0)
	    return 0;
	}
    }

  _gdbmsync_done (dbf);
  _gdbmsync_init (dbf);

  return -1;
}

static inline int
timespec_cmp (struct timespec const *a, struct timespec const *b)
{
  if (a->tv_sec < b->tv_sec)
    return -1;
  if (a->tv_sec > b->tv_sec)
    return 1;
  if (a->tv_nsec < b->tv_nsec)
    return -1;
  if (a->tv_nsec > b->tv_nsec)
    return 1;
  return 0;
}

static int
stat_snapshot (const char *f, struct stat *st)
{
  if (stat (f, st))
    return -1;
  if (!S_ISREG (st->st_mode))	/* f is not a regular file */
    return -1;
  if (S_IXUSR & st->st_mode)	/* f is executable */
    return -1;
  if ((S_IRUSR & st->st_mode) && (S_IWUSR & st->st_mode))
    return -1;				/* f is both readable and writable */
  return 0;
}

/*
 * Selects among the two given snapshot files the one to be used for
 * post-crash recovery and stores its value in *RET.
 * Returns 0 (GDBM_SNAPSHOT_OK) on success, GDBM_SNAPSHOT_ERR on error (examine
 * errno) and GDBM_SNAPSHOT_SAME in the unlikely case when two snapshots have
 * the same modification time.
 */
int
gdbm_latest_snapshot (const char *even, const char *odd, const char **ret)
{
  int r, sum;
  struct stat st_even, st_odd;
  
  if (!ret || !even || !odd || strcmp (even, odd) == 0)
    {
      errno = EINVAL;
      return GDBM_SNAPSHOT_ERR;
    }

  if (stat_snapshot (even, &st_even))
    return GDBM_SNAPSHOT_ERR;
  if (stat_snapshot (odd, &st_odd))
    return GDBM_SNAPSHOT_ERR;
  
  if (st_even.st_mode & S_IRUSR)
    {
      if (!(st_odd.st_mode & S_IRUSR))
	{
	  *ret = even;
	  return GDBM_SNAPSHOT_OK;
	}
    }
  else if (st_odd.st_mode & S_IRUSR)
    {
      *ret = odd;
      return GDBM_SNAPSHOT_OK;
    }
  
  switch (timespec_cmp (&st_even.st_mtim, &st_odd.st_mtim))
    {
    case -1:
      *ret = even;
      break;

    case 1:
      *ret = odd;
      break;
    
    case 0:
      /* Shouldn't happen */
      return GDBM_SNAPSHOT_SAME;
    }

  return GDBM_SNAPSHOT_OK;
}
#else
int
gdbm_failure_atomic (GDBM_FILE dbf, const char *even, const char *odd)
{
	errno = ENOSYS;
	GDBM_SET_ERRNO (dbf, GDBM_ERR_USAGE, FALSE);
	return -1;
}

int
gdbm_latest (const char *even, const char *odd, const char *ret)
{
	errno = ENOSYS;
	return GDBM_SNAPSHOT_ERR;
}
#endif /* GDBM_FAILURE_ATOMIC */

/* Make sure the database is all on disk. */

int
gdbm_sync (GDBM_FILE dbf)
{
  /* Return immediately if the database needs recovery */
  GDBM_ASSERT_CONSISTENCY (dbf, -1);

  /* Initialize the gdbm_errno variable. */
  gdbm_set_errno (dbf, GDBM_NO_ERROR, FALSE);

  /* Do the sync on the file. */
  return gdbm_file_sync (dbf);
}
