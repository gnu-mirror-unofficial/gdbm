/*
  NAME
    gtconv - test GDBM database conversion to extended format.

  SYNOPSIS
    gtconv [-v]

  DESCRIPTION
    When converting a traditional GDBM database to extended (numsync)
    format, the size of the master av_table shrinks.  Consequently,
    if it is full or nearly full, the entries near its end that
    don't fit into the new size are returned to the per-bucket
    available pools using _gdbm_free (see _gdbm_convert_to_numsync).
    
    This test program verifies that all main av_table entries are
    preserved during format upgrade.

    Operation:

    1) Create a database with the minimal possible block size, to ensure
       the mimimal size of the av_table array
    2) Set the GDBM_SETCENTFREE option, so all released entries are
       returned to the main av_table.
    3) Populate the database with a sufficient number of entries.
    4) Keep deleting entries until main av_table becomes full.
    5) Save a copy of all avail_elems (both master and per-block),
       sorted by av_adr.
    6) Convert the database to the GDBM_NUMSYNC format.
    7) Get a copy of all avail_elems similar to (5)
    8) Compare arrays obtained in 5 and 7.
    
    The array obtained in step 7 is normally one entry longer than the
    one from step 5.  The comparison in 8 ignores such extra entries.

  OPTIONS
     -v   Verbosely print what's being done.
          Repeated twice, dumps listings of av_table arrays received in
	  steps 5 and 7.

  EXIT CODE
     0    success
     1    failure
     2    usage error
    77    unable to fill the av_table in step 4.
	  
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

char dbname[] = "a.db";

#define DATASIZE (4*IGNORE_SIZE)

static int
avail_counter (avail_block *blk, off_t off, void *closure)
{
  int *np = closure;
  *np += blk->count;
  return 0;
}

static int
avail_saver (avail_block *blk, off_t off, void *closure)
{
  avail_block *ab = closure;
  memcpy (ab->av_table + ab->count, blk->av_table,
	  blk->count * sizeof (blk->av_table[0]));
  ab->count += blk->count;
  return 0;
}

static int
av_table_cmp (const void *a, const void *b)
{
  const avail_elem *ea = a;
  const avail_elem *eb = b;
  if (ea->av_adr < eb->av_adr)
    return -1;
  else if (ea->av_adr > eb->av_adr)
    return 1;
  return 0;
}

static avail_block *
collect_avail (GDBM_FILE dbf)
{
  int av_count;
  avail_block *ab;
  
  /* Compute the number of entries in avail block */
  av_count = 0;
  gdbm_avail_traverse (dbf, avail_counter, &av_count);

  /* Allocate temporary storage */
  ab = malloc (sizeof (ab[0]) + (av_count - 1) * sizeof (ab->av_table[0]));
  assert (ab != NULL);

  /* Save the avail table */
  ab->size = av_count;
  ab->count = 0;		     
  gdbm_avail_traverse (dbf, avail_saver, ab);

  /* Sort it */
  qsort (ab->av_table, ab->count, sizeof (ab->av_table[0]), av_table_cmp);

  return ab;
}

static void
dump_avail (avail_block *ab, char const *title, FILE *fp)
{
  int i;
  unsigned long total = 0;

  fprintf (fp, "%s\n", title);
  for (i = 0; i < ab->count; i++)
    {
      total += ab->av_table[i].av_size;
      fprintf (fp, "% 4d %6lu\n", ab->av_table[i].av_size,
	      (unsigned long) ab->av_table[i].av_adr);
    }
  fprintf (fp, "total = %lu\n", total);
}

int
main (int argc, char **argv)
{
  int avcount;
  GDBM_FILE dbf;
  datum key, content;
  char data[DATASIZE];

  int nkeys;
  int *keys;

  int i, n;
  
  avail_block *av_saved, *av_new;

  int verbose = 0;
  int rc;
  
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
  
  /* Make sure we create new database */
  unlink (dbname);

  /* Create the database */
  if (verbose)
    printf ("creating database\n");
  dbf = gdbm_open (dbname, GDBM_MIN_BLOCK_SIZE, GDBM_NEWDB, 0644, NULL);
  if (!dbf)
    {
      fprintf (stderr, "gdbm_open: %s\n", gdbm_strerror (gdbm_errno));
      return 1;
    }

  int t = 1;
  if (gdbm_setopt (dbf, GDBM_SETCENTFREE, &t, sizeof (t)) == -1)
    {
      fprintf (stderr, "gdbm_setopt: %s\n", gdbm_strerror (gdbm_errno));
      return 1;
    }
  
  avcount = dbf->avail->size;
  if (verbose)
    printf ("main av_table capacity: %d\n", avcount);

  
  /* Initialize keys */
  nkeys = 2*avcount;
  keys = calloc (nkeys, sizeof (keys));
  assert (keys != NULL);
  for (i = 0; i < nkeys; i++)
    {
      keys[i] = i+1;
    }
  
  /* Initialize content */
  for (i = 0; i < DATASIZE; i++)
    data[i] = i+1;
  content.dsize = DATASIZE;
  content.dptr = data;

  /* Populate the database */
  if (verbose)
    printf ("populating database (%d keys)\n", nkeys);
  key.dsize = sizeof (keys[0]);
  for (i = 0; i < nkeys; i++)
    {
      key.dptr = (char*) &keys[i];
      if (gdbm_store (dbf, key, content, 0) != 0)
	{
	  fprintf (stderr, "%d: item not inserted: %s\n",
		   i, gdbm_db_strerror (dbf));
	  gdbm_close (dbf);
	  return 1;
	}
    }

  /* Delete all keys */
  if (verbose)
    printf ("deleting keys\n");
  i = 0;
  while (dbf->avail->count < dbf->avail->size)
    {
      if (i == nkeys)
	{
	  if (verbose)
	    printf ("failed to fill av_table\n");
	  gdbm_close (dbf);
	  return 77;
	}
      key.dptr = (char*) &keys[i];
      if (gdbm_delete (dbf, key))
	{
	  fprintf (stderr, "%d: gdbm_delete: %s\n",
		   i, gdbm_db_strerror (dbf));
	  gdbm_close (dbf);
	  return 1;
	}
      i++;
    }

  if (verbose)
    printf ("main av_table elements: %d\n", dbf->avail->count);
  
  av_saved = collect_avail (dbf);
  if (verbose)
    printf ("total number of avail_elem entries used: %d\n", av_saved->count);
  if (verbose > 1)
    dump_avail (av_saved, "av_saved", stdout);
  
  /* Upgrade the database */
  if (verbose)
    printf ("converting database\n");

  if (gdbm_convert (dbf, GDBM_NUMSYNC))
    {
      fprintf (stderr, "gdbm_convert: %s\n", gdbm_db_strerror (dbf));
      gdbm_close (dbf);
      return 1;
    }

  if (verbose)
    printf ("main av_table elements: %d / %d\n", dbf->avail->count, dbf->avail->size);

  av_new = collect_avail (dbf);
  if (verbose)
    printf ("total number of avail_elem entries used: %d\n", av_new->count);
  
  if (verbose > 1)
    dump_avail (av_new, "av_new", stdout);

  n = (av_saved->count < av_new->count) ? av_saved->count : av_new->count;
  rc = 0;
  for (i = 0; i < n; i++)
    {
      if (!(av_saved->av_table[i].av_adr == av_new->av_table[i].av_adr &&
	    av_saved->av_table[i].av_size == av_new->av_table[i].av_size))
	{
	  fprintf (stderr, "element %d differs\n", i);
	  rc = 1;
	  break;
	}
    }

  if (rc)
    {
      dump_avail (av_saved, "av_saved", stderr);
      dump_avail (av_new, "av_new", stderr);
    }

  free (av_saved);
  free (av_new);
  free (keys);
  gdbm_close (dbf);
  return rc;
}
  
