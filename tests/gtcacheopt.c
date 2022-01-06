/*
  NAME
    gtcacheopt - test GDBM_GETCACHESIZE and GDBM_SETCACHESIZE options.

  SYNOPSIS
    gtcacheopt [-v]

  DESCRIPTION
    Reducing the cache size should retain most recently used elements
    and ensure correct rehashing.

    Operation:

    1) Create new database.
    2) Generate at least 10 full buckets,
    3) Check GDBM_GETCACHESIZE.
    4) Get cache statistics for the first 8 most recently
       used cache elements.
    5) Set cache size to 8.
    6) Retrieve each of the bucket pointed to by stats obtained in 4.
    7) Verify that (6) retrieved buckets from cache.

  OPTIONS
     -v   Verbosely print what's being done.

  EXIT CODE
     0    success
     1    failure
     2    usage error
     
  LICENSE
    This file is part of GDBM test suite.
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
#include "gdbmdefs.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>

char dbname[] = "a.db";
int verbose = 0;

#define NBUCKETS 10
#define CACHE_SIZE 8
#define DATASIZE (4*IGNORE_SIZE)

static void
test_getcachesize (GDBM_FILE dbf, size_t expected_size,
		   struct gdbm_cache_stat *pstat, size_t *pnstat)
{
  size_t size;
  int cache_auto;

  if (gdbm_setopt (dbf, GDBM_GETCACHESIZE, &size, sizeof (size)))
    {
      fprintf (stderr, "GDBM_GETCACHESIZE: %s\n", gdbm_strerror (gdbm_errno));
      exit (1);
    }

  if (verbose)
    printf ("size = %zu\n", size);

  if (expected_size && expected_size != size)
    {
      fprintf (stderr, "expected_size != size (%zu != %zu)\n",
	       expected_size, size);
      exit (1);
    }
  
  if (gdbm_setopt (dbf, GDBM_GETCACHEAUTO, &cache_auto, sizeof (cache_auto)))
    {
      fprintf (stderr, "GDBM_GETCACHESIZE: %s\n", gdbm_strerror (gdbm_errno));
      exit (1);
    }

  if (verbose)
    printf ("cache_auto = %d\n", cache_auto);

  if (expected_size && cache_auto != 0)
    {
      fprintf (stderr, "cache_auto != 0\n");
      exit (1);
    }

  if (pstat)
    {
      gdbm_get_cache_stats (dbf, NULL, NULL, pnstat, pstat, CACHE_SIZE);
    }
}

static int
dir_index (GDBM_FILE dbf, off_t adr)
{
  int i;

  for (i = 0; i < dbf->header->dir_size; i++)
    if (dbf->dir[i] == adr)
      return i;
  fprintf (stderr, "%lu: can't find bucket in directory\n", adr);
  exit (1);
}
  
int
main (int argc, char **argv)
{
  GDBM_FILE dbf;
  datum key, content;
  
  int nkeys;

  char data[DATASIZE];

  int i;

  struct gdbm_cache_stat stat[2][CACHE_SIZE];
  size_t nstat[2];
  
  while ((i = getopt (argc, argv, "v")) != EOF)
    {
      switch (i)
	{
	case 'v':
	  verbose++;
	  break;

	default:
	  return 2;
	}
    }

  /*
   * 1) Create new database.
   */
  if (verbose)
    printf ("creating database\n");
  
  dbf = gdbm_open (dbname, GDBM_MIN_BLOCK_SIZE, GDBM_NEWDB, 0644, NULL);
  if (!dbf)
    {
      fprintf (stderr, "gdbm_open: %s\n", gdbm_strerror (gdbm_errno));
      return 1;
    }

  /*
   * 2) Generate 10 full buckets or key/value pairs are created.
   */
  
  /* Initialize keys. */
  nkeys = NBUCKETS * dbf->header->bucket_elems;

  /* Initialize content */
  for (i = 0; i < DATASIZE; i++)
    data[i] = i+1;
  content.dsize = DATASIZE;
  content.dptr = data;

  /* Populate the database. */
  if (verbose)
    printf ("populating database (%d keys)\n", nkeys);
  key.dsize = sizeof (i);
  key.dptr = (char*) &i;
  for (i = 0; i < nkeys; i++)
    {
      if (gdbm_store (dbf, key, content, 0) != 0)
	{
	  fprintf (stderr, "%d: item not inserted: %s\n",
		   i, gdbm_db_strerror (dbf));
	  gdbm_close (dbf);
	  return 1;
	}
    }

  /*
   * 3) Check if the value retrieved by GDBM_GETCACHESIZE matches the
   *    expected one and
   * 4) save cache statistics for the first CACHE_SIZE most recently used
   *    cache elements.
   */
  test_getcachesize (dbf, 0, stat[0], &nstat[0]);

  if (verbose)
    printf ("setting new cache size\n");

  /*
   * 5) Set new cache size.
   */
  i = CACHE_SIZE;
  if (gdbm_setopt (dbf, GDBM_SETCACHESIZE, &i, sizeof (i)))
    {
      fprintf (stderr, "GDBM_SETCACHESIZE: %s\n", gdbm_strerror (gdbm_errno));
      return 1;
    }
  
  if (verbose)
    printf ("verifying cache (pass 1)\n");

  /*
   * 6) Retrieve each of the bucket pointed to by stats obtained in 4.
   *
   * To retrieve a bucket, the corresponding directory index must be known.
   * That index is obtained using linear search in the database file directory.
   *
   * Buckets must be retrieved in reverse order, so that the LRU cache
   * remains in the same order after the operation (each retrieval cyclically
   * shifts elements in the queue).
   */
  test_getcachesize (dbf, CACHE_SIZE, stat[1], &nstat[1]);

  for (i = CACHE_SIZE - 1; i >= 0; i--)
    {
      if (stat[0][i].adr != stat[1][i].adr)
	{
	  fprintf (stderr, "%d: address mismatch\n", i);
	  return 1;
	}
      if (_gdbm_get_bucket (dbf, dir_index (dbf, stat[0][i].adr)))
	{
	  fprintf (stderr, "%d: _gdbm_get_bucket: %s\n", i,
		   gdbm_db_strerror (dbf));
	  return 1;
	}
    }

  if (verbose)
    printf ("getting cache statistics\n");
  test_getcachesize (dbf, CACHE_SIZE, stat[0], &nstat[0]);
  
  gdbm_close (dbf);

  /*
   * 7) Verify that the buckets were retrieved from cache.
   *
   * To do so, compare addresses and hit counts in statistic buffers
   * stat[0] and stat[1].  Each pair of elements must have the same
   * bucket address.  Hit counts must differ by 1.
   */
  if (verbose)
    printf ("verifying cache (pass 2)\n");
  for (i = 0; i < CACHE_SIZE; i++)
    {
      if (stat[0][i].adr != stat[1][i].adr)
	{
	  fprintf (stderr, "%d: address mismatch\n", i);
	  return 1;
	}
      if (stat[0][i].hits != stat[1][i].hits + 1)
	{
	  fprintf (stderr, "%d: hit count mismatch: %zu != %zu\n", i,
		   stat[0][i].hits, stat[1][i].hits);
	  return 1;
	}
    }
  
  return 0;
}
  

  
