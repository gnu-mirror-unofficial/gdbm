/* proto.h - The prototypes for the dbm routines. */

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


/* From bucket.c */
void _gdbm_new_bucket	(GDBM_FILE, hash_bucket *, int);
int _gdbm_get_bucket	(GDBM_FILE, int);
int _gdbm_fetch_data   (GDBM_FILE dbf, off_t off, size_t size, void *buf);

int _gdbm_split_bucket (GDBM_FILE, int);
int _gdbm_write_bucket (GDBM_FILE, cache_elem *);
int _gdbm_cache_init   (GDBM_FILE, size_t);
void _gdbm_cache_free  (GDBM_FILE dbf);
int _gdbm_cache_flush  (GDBM_FILE dbf);

/* From falloc.c */
off_t _gdbm_alloc       (GDBM_FILE, int);
int  _gdbm_free         (GDBM_FILE, off_t, int);
void _gdbm_put_av_elem  (avail_elem, avail_elem [], int *, int);
int _gdbm_avail_block_read (GDBM_FILE dbf, avail_block *avblk, size_t size);

/* From findkey.c */
char *_gdbm_read_entry  (GDBM_FILE, int);
int _gdbm_findkey       (GDBM_FILE, datum, char **, int *);

/* From hash.c */
int _gdbm_hash (datum);
void _gdbm_hash_key (GDBM_FILE dbf, datum key, int *hash, int *bucket,
		     int *offset);
int _gdbm_bucket_dir (GDBM_FILE dbf, int hash);

/* From update.c */
int _gdbm_end_update   (GDBM_FILE);
void _gdbm_fatal	(GDBM_FILE, const char *);

/* From gdbmopen.c */
int _gdbm_validate_header (GDBM_FILE dbf);

int _gdbm_file_size (GDBM_FILE dbf, off_t *psize);

/* From mmap.c */
int _gdbm_mapped_init	(GDBM_FILE);
void _gdbm_mapped_unmap	(GDBM_FILE);
ssize_t _gdbm_mapped_read	(GDBM_FILE, void *, size_t);
ssize_t _gdbm_mapped_write	(GDBM_FILE, void *, size_t);
off_t _gdbm_mapped_lseek	(GDBM_FILE, off_t, int);
int _gdbm_mapped_sync	(GDBM_FILE);

/* From lock.c */
void _gdbm_unlock_file	(GDBM_FILE);
int _gdbm_lock_file	(GDBM_FILE);

/* From fullio.c */
int _gdbm_full_read (GDBM_FILE, void *, size_t);
int _gdbm_full_write (GDBM_FILE, void *, size_t);
int _gdbm_file_extend (GDBM_FILE dbf, off_t size);

/* From base64.c */
int _gdbm_base64_encode (const unsigned char *input, size_t input_len,
			 unsigned char **output, size_t *output_size,
			 size_t *outbytes);
int _gdbm_base64_decode (const unsigned char *input, size_t input_len,
			 unsigned char **output, size_t *output_size,
			 size_t *inbytes, size_t *outbytes);

int _gdbm_load (FILE *fp, GDBM_FILE *pdbf, unsigned long *line);
int _gdbm_dump (GDBM_FILE dbf, FILE *fp);

/* From recover.c */
int _gdbm_next_bucket_dir (GDBM_FILE dbf, int bucket_dir);

/* cachetree.c */
cache_tree *_gdbm_cache_tree_alloc (void);
void _gdbm_cache_tree_destroy (cache_tree *tree);
void _gdbm_cache_tree_delete (cache_tree *tree, struct cache_node *n);

/* avail.c */
int gdbm_avail_block_validate (GDBM_FILE dbf, avail_block *avblk, size_t size);
int gdbm_bucket_avail_table_validate (GDBM_FILE dbf, hash_bucket *bucket);
int gdbm_avail_traverse (GDBM_FILE dbf,
			 int (*cb) (avail_block *, off_t, void *),
			 void *data);


/* Return codes for _gdbm_cache_tree_lookup. */
enum
  {
    node_found,   /* Returned element was found in cache. */
    node_new,     /* Returned element has been created and inserted to cache */
    node_failure  /* An error occurred. */
  };

int _gdbm_cache_tree_lookup (cache_tree *tree, off_t adr, cache_node **retval);

/* I/O functions */
static inline ssize_t
gdbm_file_read (GDBM_FILE dbf, void *buf, size_t size)
{
#if HAVE_MMAP
  return _gdbm_mapped_read (dbf, buf, size);
#else
  return read (dbf->desc, buf, size);
#endif
}

static inline ssize_t
gdbm_file_write (GDBM_FILE dbf, void *buf, size_t size)
{
#if HAVE_MMAP
  return _gdbm_mapped_write (dbf, buf, size);
#else
  return write (dbf->desc, buf, size);
#endif
}

static inline off_t
gdbm_file_seek (GDBM_FILE dbf, off_t off, int whence)
{
#if HAVE_MMAP
  return _gdbm_mapped_lseek (dbf, off, whence);
#else
  return lseek (dbf->desc, off, whence);
#endif
}

#ifdef GDBM_FAILURE_ATOMIC
/* From gdbmsync.c */
extern int _gdbm_snapshot(GDBM_FILE);
#endif /* GDBM_FAILURE_ATOMIC */

static inline void
_gdbmsync_init (GDBM_FILE dbf)
{
#ifdef GDBM_FAILURE_ATOMIC
  dbf->snapfd[0] = dbf->snapfd[1] = -1;
  dbf->eo = 0;
#endif
}

static inline void
_gdbmsync_done (GDBM_FILE dbf)
{
#ifdef GDBM_FAILURE_ATOMIC
  if (dbf->snapfd[0] >= 0)
    close (dbf->snapfd[0]);
  if (dbf->snapfd[1] >= 0)
    close (dbf->snapfd[1]);
#endif
}

static inline int
gdbm_file_sync (GDBM_FILE dbf)
{
  int r = 0;  /* return value */
#if HAVE_MMAP
  r = _gdbm_mapped_sync (dbf);
#elif HAVE_FSYNC
  if (fsync (dbf->desc))
    {
      GDBM_SET_ERRNO (dbf, GDBM_FILE_SYNC_ERROR, TRUE);
      r = 1;
    }
#else
  sync ();
  sync ();
#endif
#ifdef GDBM_FAILURE_ATOMIC
  /* If and only if the conventional fsync/msync/sync succeeds,
     attempt to clone the data file. */
  if (r == 0)
    r = _gdbm_snapshot(dbf);
#endif /* GDBM_FAILURE_ATOMIC */
  return r;
}
