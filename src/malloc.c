#ifndef void
# define void void
#endif

#include <stddef.h>       /* For size_t */
#include <unistd.h>

#include <bits/wordsize.h>
#include <sys/sysinfo.h>

#include <stdio.h>
#include <errno.h>

#include <stdint.h>
#include <stdbool.h>

/* -------- Word Size And Alignment ----------------------------------------- */

#ifndef INTERNAL_SIZE_T
# define INTERNAL_SIZE_T size_t
#endif

#define SIZE_SZ (sizeof(INTERNAL_SIZE_T))

#ifndef MALLOC_ALIGNMENT
# define MALLOC_ALIGNMENT (2 * SIZE_SZ)
#endif

#define MALLOC_ALIGN_MASK (MALLOC_ALIGNMENT - 1)

/*
  MMAP_THRESHOLD_MAX and _MIN are the bounds on the dynamically
  adjusted MMAP_THRESHOLD.
*/

#ifndef DEFAULT_MMAP_THRESHOLD_MIN
# define DEFAULT_MMAP_THRESHOLD_MIN (128 * 1024)
#endif

#ifndef DEFAULT_MMAP_THRESHOLD_MAX
# if __WORDSIZE == 32
#  define DEFAULT_MMAP_THRESHOLD_MAX (512 * 1024)
# else
#  define DEFAULT_MMAP_THRESHOLD_MAX (4 * 1024 * 1024 * sizeof(long))
# endif
#endif

/*
  M_MMAP_THRESHOLD is the request size threshold for using mmap()
  to service a request. Requests of at least this size that cannot
  be allocated using already-existing space will be serviced via mmap.
  (If enough normal freed space already exists it is used instead.)
*/

#ifndef DEFAULT_MMAP_THRESHOLD
# define DEFAULT_MMAP_THRESHOLD DEFAULT_MMAP_THRESHOLD_MIN
#endif

/*
  M_MMAP_MAX is the maximum number of requests to simultaneously
  service using mmap. This parameter exists because
  some systems have a limited number of internal tables for
  use by mmap, and using more than a few of them may degrade
  performance.

  The default is set to a value that serves only as a safeguard.
  Setting to 0 disables use of mmap for servicing large requests.
*/

#ifndef DEFAULT_MMAP_MAX
#define DEFAULT_MMAP_MAX       (65536)
#endif

/* -------- Chunk Representation -------------------------------------------- */

struct malloc_chunk {
  /* Size of previous chunk (if free) */
  INTERNAL_SIZE_T prev_size;
  /* Size in bytes, including overhead. */
  INTERNAL_SIZE_T size;
  /* double links -- used only if free */
  struct malloc_chunk* fd;
  struct malloc_chunk* bk;
  /* Only used for large blocks: pointers to next larger size. */
  /* double links -- used only if free */
  struct malloc_chunk* fd_nextsize;
  struct malloc_chunk* bk_nextsize;
};

typedef struct malloc_chunk* mchunkptr;

/* The smallest possible chunk */
#define MIN_CHUNK_SIZE        (offsetof(struct malloc_chunk, fd_nextsize))

/* The smallest size we can malloc is an aligned minimal chunk */
#define MINSIZE  \
  (unsigned long)(((MIN_CHUNK_SIZE+MALLOC_ALIGN_MASK) & ~MALLOC_ALIGN_MASK))

/* -------- Chunk Flag Values ----------------------------------------------- */

/* size field is or'ed with PREV_INUSE when previous adjacent chunk in use */
#define PREV_INUSE 0x1

/* size field is or'ed with IS_MMAPPED if the chunk was obtained with mmap() */
#define IS_MMAPPED 0x2

/* size field is or'ed with NON_MAIN_ARENA if the chunk was obtained
   from a non-main arena.  This is only set immediately before handing
   the chunk to the user, if necessary.  */
#define NON_MAIN_ARENA 0x4

/*
  Bits to mask off when extracting size

  Note: IS_MMAPPED is intentionally not masked off from size field in
  macros for which mmapped chunks should never be seen. This should
  cause helpful core dumps to occur if it is tried by accident by
  people extending or adapting this malloc.
*/
#define SIZE_BITS (PREV_INUSE|IS_MMAPPED|NON_MAIN_ARENA)

/* -------- Size And Alignment Conversions ---------------------------------- */

/*!
 * @brief Returns the user memory address for a chunkptr.
 */
static inline void *chunk2mem(mchunkptr p)
{
  return ((void*)((char*)(p) + 2 * SIZE_SZ));
}

/*!
 * @brief Returns the chunkptr for a user memory address.
 */
static inline mchunkptr mem2chunk(void *mem)
{
  return ((mchunkptr)((char*)(mem) - 2 * SIZE_SZ));
}

/*!
 * @brief Checks if a request of size would wrap around zero.
 * @param `req` The size of the request.
 * @return 1 if `req` is too big; 0 if `req` wouldn't wrap around zero.
 *
 * Pads and aligns req size and checks if it would wrap around zero.
 * Bound is made low enough so that adding MINSIZE will also not wrap
 * around zero.
 */
static inline bool req_out_of_range(INTERNAL_SIZE_T req)
{
  return ((unsigned long)(req) >=
      (unsigned long)(INTERNAL_SIZE_T)(-2 * MINSIZE));
}

/*!
 * @brief Converts a user requested size into a chunksize.
 */
static inline INTERNAL_SIZE_T req2size(INTERNAL_SIZE_T req)
{
  return (req + SIZE_SZ + MALLOC_ALIGN_MASK < MINSIZE) ?
    MINSIZE :
    ((req + SIZE_SZ + MALLOC_ALIGN_MASK) & ~MALLOC_ALIGN_MASK);
}

/* -------- Physical chunk opoerations -------------------------------------- */

/*!
 * @brief Check if previous chunk is in use.
 */
static inline bool prev_inuse(mchunkptr p)
{
  return (bool)p->size & PREV_INUSE;
}

/*!
 * @brief Check if current chunk is acquired via mmap.
 */
static inline bool is_mmapped(mchunkptr p)
{
  return (bool)p->size & IS_MMAPPED;
}

/*!
 * @brief Check if current chunk is part of the main arena.
 */
static inline bool is_non_main_arena(mchunkptr p)
{
  return (bool)p->size & NON_MAIN_ARENA;
}

/*!
 * @brief Returns the total size of the current chunk (including header).
 */
static inline INTERNAL_SIZE_T chunksize(mchunkptr p)
{
  return (bool)p->size & ~SIZE_BITS;
}

/*!
 * @brief Returns the pointer to the next physical chunk.
 */
static inline mchunkptr next_chunk(mchunkptr p)
{
  return (mchunkptr)((char*)(p) + (p->size & ~SIZE_BITS));
}

/*!
 * @brief Returns the pointer to the previous physical chunk.
 */
static inline mchunkptr prev_chunk(mchunkptr p)
{
  return (mchunkptr)((char*)(p) - p->prev_size);
}

/*!
 * @brief Returns address at offset after p as mchunkptr.
 */
static inline mchunkptr chunk_at_offset(mchunkptr p, INTERNAL_SIZE_T off)
{
  return (mchunkptr)((char*)(p) + off);
}

/*!
 * @brief Checks current chunks INUSE bit.
 */
static inline bool inuse(mchunkptr p)
{
  return prev_inuse(next_chunk(p));
}

/*!
 * @brief Set next chunks PREV_INUSE bit. (Sets this chunk to be in use).
 */
static inline void set_inuse(mchunkptr p)
{
  next_chunk(p)->size |= PREV_INUSE;
}

/*!
 * @brief Clear next chunks PREV_INUSE bit. (Clear this chunk to be in use).
 */
static inline void clear_inuse(mchunkptr p)
{
  next_chunk(p)->size &= ~PREV_INUSE;
}

/*!
 * @brief Checks chunk at offset's INUSE bit.
 *
 * This checks the chunk's INUSE bit in it's OWN header size field.
 */
static inline bool inuse_at_offset(mchunkptr p, INTERNAL_SIZE_T off)
{
  return chunk_at_offset(p, off)->size & PREV_INUSE;
}

/*!
 * @brief Set chunk at offset's PREV_INUSE bit.
 */
static inline void set_inuse_at_offset(mchunkptr p, INTERNAL_SIZE_T off)
{
  chunk_at_offset(p, off)->size |= PREV_INUSE;
}

/*!
 * @brief Clear chunk at offset's PREV_INUSE bit.
 */
static inline void clear_inuse_at_offset(mchunkptr p, INTERNAL_SIZE_T off)
{
  chunk_at_offset(p, off)->size &= ~PREV_INUSE;
}

/*!
 * @brief Set's current chunks size without distrubing it's flags.
 */
static inline void set_head_size(mchunkptr p, INTERNAL_SIZE_T s)
{
  p->size = (p->size & SIZE_BITS) | s;
}

/*!
 * @brief Set's current chunks size field. Disturbs flags.
 */
static inline void set_head(mchunkptr p, INTERNAL_SIZE_T s)
{
  p->size = s;
}

/*!
 * @brief Set's next chunks prev_size field. Disturbs flags.
 */
static inline void set_foot(mchunkptr p, INTERNAL_SIZE_T s)
{
  next_chunk(p)->prev_size = s;
}

/* -------- Internal Data Structures ---------------------------------------- */

/*
  Bins

    An array of bin headers for free chunks. Each bin is doubly
    linked.  The bins are approximately proportionally (log) spaced.
    There are a lot of these bins (128). This may look excessive, but
    works very well in practice.  Most bins hold sizes that are
    unusual as malloc request sizes, but are more usual for fragments
    and consolidated sets of chunks, which is what these bins hold, so
    they can be found quickly.  All procedures maintain the invariant
    that no consolidated chunk physically borders another one, so each
    chunk in a list is known to be preceeded and followed by either
    inuse chunks or the ends of memory.

    Chunks in bins are kept in size order, with ties going to the
    approximately least recently used chunk. Ordering isn't needed
    for the small bins, which all contain the same-sized chunks, but
    facilitates best-fit allocation for larger chunks. These lists
    are just sequential. Keeping them in order almost never requires
    enough traversal to warrant using fancier ordered data
    structures.

    Chunks of the same size are linked with the most
    recently freed at the front, and allocations are taken from the
    back.  This results in LRU (FIFO) allocation order, which tends
    to give each chunk an equal opportunity to be consolidated with
    adjacent freed chunks, resulting in larger free chunks and less
    fragmentation.

    To simplify use in double-linked lists, each bin header acts
    as a malloc_chunk. This avoids special-casing for headers.
    But to conserve space and improve locality, we allocate
    only the fd/bk pointers of bins, and then use repositioning tricks
    to treat these as the fields of a malloc_chunk*.
*/

typedef struct malloc_chunk* mbinptr;

/*
  Indexing

    Bins for sizes < 512 bytes contain chunks of all the same size, spaced
    8 bytes apart. Larger bins are approximately logarithmically spaced:

    64 bins of size       8
    32 bins of size      64
    16 bins of size     512
     8 bins of size    4096
     4 bins of size   32768
     2 bins of size  262144
     1 bin  of size what's left

    There is actually a little bit of slop in the numbers in bin_index
    for the sake of speed. This makes no difference elsewhere.

    The bins top out around 1MB because we expect to service large
    requests via mmap.

    Bin 0 does not exist. Bin 1 is the unordered list; if that would be
    a valid chunk size the small bins are bumped up one.
*/

#define NBINS             128
#define NSMALLBINS         64
#define SMALLBIN_WIDTH    MALLOC_ALIGNMENT
#define SMALLBIN_CORRECTION (MALLOC_ALIGNMENT > 2 * SIZE_SZ)
#define MIN_LARGE_SIZE    ((NSMALLBINS - SMALLBIN_CORRECTION) * SMALLBIN_WIDTH)

/*!
 * @brief Checks if given size `sz` is small enough for smallbins.
 */
static inline bool in_smallbin_range(INTERNAL_SIZE_T sz)
{
  return sz < (INTERNAL_SIZE_T)MIN_LARGE_SIZE;
}

/*!
 * @brief Returns the smallbin_index for the given size `sz`.
 */
static inline unsigned int smallbin_index(INTERNAL_SIZE_T sz)
{
  return SMALLBIN_WIDTH == 16 ? (unsigned int)(sz >> 4) :
    (unsigned int)((sz >> 3) + SMALLBIN_CORRECTION);
}

static inline unsigned int largebin_index_32(INTERNAL_SIZE_T sz)
{
  return (
      (unsigned long)(sz >>  6) <= 38 ?  56 + (unsigned long)(sz >>  6):
      (unsigned long)(sz >>  9) <= 20 ?  91 + (unsigned long)(sz >>  9):
      (unsigned long)(sz >> 12) <= 10 ? 110 + (unsigned long)(sz >> 12):
      (unsigned long)(sz >> 15) <=  4 ? 119 + (unsigned long)(sz >> 15):
      (unsigned long)(sz >> 18) <=  2 ? 124 + (unsigned long)(sz >> 18):
                                        126);
}

static inline unsigned int largebin_index_32_big(INTERNAL_SIZE_T sz)
{
  return (
      (unsigned long)(sz >>  6) <= 45 ?  49 + (unsigned long)(sz >>  6):
      (unsigned long)(sz >>  9) <= 20 ?  91 + (unsigned long)(sz >>  9):
      (unsigned long)(sz >> 12) <= 10 ? 110 + (unsigned long)(sz >> 12):
      (unsigned long)(sz >> 15) <=  4 ? 119 + (unsigned long)(sz >> 15):
      (unsigned long)(sz >> 18) <=  2 ? 124 + (unsigned long)(sz >> 18):
                                        126);
}

static inline unsigned int largebin_index_64(INTERNAL_SIZE_T sz)
{
  return (
      (unsigned long)(sz >>  6) <= 48 ?  48 + (unsigned long)(sz >>  6):
      (unsigned long)(sz >>  9) <= 20 ?  91 + (unsigned long)(sz >>  9):
      (unsigned long)(sz >> 12) <= 10 ? 110 + (unsigned long)(sz >> 12):
      (unsigned long)(sz >> 15) <=  4 ? 119 + (unsigned long)(sz >> 15):
      (unsigned long)(sz >> 18) <=  2 ? 124 + (unsigned long)(sz >> 18):
                                        126);
}

static inline unsigned int largebin_index(INTERNAL_SIZE_T sz)
{
  return SIZE_SZ == 8 ? largebin_index_64(sz)
   : MALLOC_ALIGNMENT == 16 ? largebin_index_32_big(sz)
   : largebin_index_32(sz);
}

static inline unsigned int bin_index(INTERNAL_SIZE_T sz)
{
  return in_smallbin_range(sz) ? smallbin_index(sz) : largebin_index(sz);
}

/*
  Binmap

    To help compensate for the large number of bins, a one-level index
    structure is used for bin-by-bin searching.  `binmap' is a
    bitvector recording whether bins are definitely empty so they can
    be skipped over during during traversals.  The bits are NOT always
    cleared as soon as bins are empty, but instead only
    when they are noticed to be empty during traversal in malloc.
*/

/* Conservatively use 32 bits per map word, even if on 64bit system */
#define BINMAPSHIFT      5
#define BITSPERMAP       (1U << BINMAPSHIFT)
#define BINMAPSIZE       (NBINS / BITSPERMAP)

#include <pthread.h>

struct malloc_state {
  /* Serialize access */
  pthread_mutex_t mutex;
  /* Base of the topmost chunk -- not otherwise kept in a bin */
  mchunkptr top;
  /* The remainder from the most recent spplit of a small request */
  mchunkptr last_remainder;
  /* Notmal bins packed as described above */
  mchunkptr bins[NBINS * 2 - 2];
  /* Bitmap of bins */
  unsigned int binmap[BINMAPSIZE];
  /* Linked list */
  struct malloc_state *next;
  /* Memory allocated from the system in this arena */
  INTERNAL_SIZE_T system_mem;
  INTERNAL_SIZE_T max_system_mem;
};

typedef struct malloc_state* mstate;

/* -------- Bin operations -------------------------------------------------- */

/*!
 * @brief Get bin at index `i` in mstate `m`.
 *
 * Because of the ordering described in bins this is a way to address bins
 * easier.
 *
 * NOTE: The bin at index 0 does not exist!
 */
static inline mbinptr bin_at(mstate m, INTERNAL_SIZE_T i)
{
  return (mbinptr)(((char*)&(m->bins[(i - 1) * 2]))
        - offsetof(struct malloc_chunk, fd));
}

/*!
 * @brief Get's the next bin after `b` in b's malloc_state bin array.
 *
 * This doesn't check four bounds. It just returns the address of `b`
 * plus two pointer sizes.
 */
static inline mbinptr next_bin(mbinptr b)
{
  return ((mbinptr)((char*)(b) + (sizeof(mchunkptr)<<1)));
}

/*!
 * @brief Returns the first element in a bin's chunk list.
 */
static inline mbinptr first(mbinptr b)
{
  return b->fd;
}

/*!
 * @brief Returns the last element in a bin's chunk list.
 */
static inline mbinptr last(mbinptr b)
{
  return b->bk;
}

/*!
 * @brief Checks if a bin is empty.
 * @return Returns 1 on empty bin; 0 on bin holding chunks.
 */
static inline bool bin_is_empty(mbinptr b)
{
  return (b->fd == b);
}

/*!
 * @brief Takes a chunk out of it's list and restores linkage.
 */
static inline void unlink_chunk(mchunkptr p)
{
  mchunkptr fd = p->fd, bk = p->bk;
  fd->bk = bk;
  bk->fd = fd;
  if (!in_smallbin_range(p->size)
      && __builtin_expect (p->fd_nextsize != NULL, 0)) {
    // assert (p->fd_nextsize->bk_nextsize == p);
    // assert (p->bk_nextsize->fd_nextsize == p);
    if (fd->fd_nextsize == NULL) {
      if (p->fd_nextsize == p) {
        fd->fd_nextsize = fd->bk_nextsize = fd;
      } else {
        fd->fd_nextsize = p->fd_nextsize;
        fd->bk_nextsize = p->bk_nextsize;
        p->fd_nextsize->bk_nextsize = fd;
        p->bk_nextsize->fd_nextsize = fd;
      }
    } else {
      p->fd_nextsize->bk_nextsize = p->bk_nextsize;
      p->bk_nextsize->fd_nextsize = p->fd_nextsize;
    }
  }
}

/*
 * Unsorted chunks
 *
 *   All remainders from chunk splits, as well as all returned chunks,
 *   are first placed in the "unsorted" bin. They are then placed
 *   in regular bins after malloc gives them ONE chance to be used before
 *   binning. So, basically, the unsorted_chunks list acts as a queue,
 *   with chunks being placed on it in free (and malloc_consolidate),
 *   and taken off (to be either used or placed in bins) in malloc.
 *
 *   The NON_MAIN_ARENA flag is never set for unsorted chunks, so it
 *   does not have to be taken into account in size comparisons.
 */

/*!
 * @brief Returns `bin_at(m, 1)` which is used for unsorted chunks.
 */
static inline mchunkptr unsorted_chunks(mstate m)
{
  return bin_at(m, 1);
}

/*
 * Top
 *
 *   The top-most available chunk (i.e., the one bordering the end of
 *   available memory) is treated specially. It is never included in
 *   any bin, is used only if no other chunk is available, and is
 *   released back to the system if it is very large (see
 *   M_TRIM_THRESHOLD).  Because top initially
 *   points to its own bin with initial zero size, thus forcing
 *   extension on the first malloc request, we avoid having any special
 *   code in malloc to check whether it even exists yet. But we still
 *   need to do so when getting memory from system, so we make
 *   initial_top treat the bin as a legal but unusable chunk during the
 *   interval between initialization and the first call to
 *   sysmalloc. (This is somewhat delicate, since it relies on
 *   the 2 preceding words to be zero during this interval as well.)
 */

/*!
 * @brief Returns unsorted_bins for `m` as a dummy inital top chunk.
 */
static inline mchunkptr initial_top(mstate m)
{
  return unsorted_chunks(m);
}

/* -------- Binmap Operations ----------------------------------------------- */

/*!
 * @brief Returns the index used by the bin at idx in the binmap.
 *
 * Use together with idx2bit to get the bit in the binmap indicating the
 * use of a bin.
 */
static inline int idx2block(int i)
{
  return i >> BINMAPSHIFT;
}

/*!
 * @brief Returns the bit used by the bin at idx in the binmap.
 *
 * Use together with idx2block to get the bit in the binmap indicating the
 * use of a bin.
 */
static inline int idx2bit(int i)
{
  return (1U << (i & ((1U << BINMAPSHIFT) - 1)));
}

/*!
 * @brief Marks the bin at index `i` in `m` as in use in the binmap.
 */
static inline void mark_bin(mstate m, int i)
{
  m->binmap[idx2block(i)] |= idx2bit(i);
}

/*!
 * @brief Marks the bin at index `i` in `m` as not in use in the binmap.
 */
static inline void unmark_bin(mstate m, int i)
{
  m->binmap[idx2block(i)] &= ~idx2bit(i);
}

/*!
 * @brief Returns a value indicating whether the bin at `i` in `m` is in use.
 *
 * @return Returns `0` if the bin is not in use. Any non zero value indicates
 *  the bin being in use.
 */
static inline int get_binmap(mstate m, int i)
{
  return m->binmap[idx2block(i)] & idx2bit(i);
}

/* -------- Heaps ----------------------------------------------------------- */

/* Compile time constants */

/* HEAP_MIN_SIZE and HEAP_MAX_SIZE limit the size of mmap()ed heaps
   that are dynamically created for multi-threaded programs.  The
   maximum size must be a power of two, for fast determination of
   which heap belongs to a chunk.  It should be much larger than the
   mmap threshold, so that requests with a size just below that
   threshold can be fulfilled without creating too many heaps.
*/

#define HEAP_MIN_SIZE (32*1024)
# ifndef HEAP_MAX_SIZE
#  ifdef DEFAULT_MMAP_THRESHOLD_MAX
#   define HEAP_MAX_SIZE (2 * DEFAULT_MMAP_THRESHOLD_MAX)
#  else
#   define HEAP_MAX_SIZE (1024*1024) /* must be a power of two */
# endif
#endif

/* A heap is a single contiguous memory region holding (coalesceable)
   malloc_chunks.  It is allocated with mmap() and always starts at an
   address aligned to HEAP_MAX_SIZE.  */

typedef struct _heap_info {
  /* Arena for this heap. */
  mstate ar_ptr;
  /* Previous heap. */
  struct _heap_info *prev;
  /* Current size in bytes. */
  size_t size;
  /* Size in bytes that has been mprotected PROT_READ|PROT_WRITE.  */
  size_t mprotect_size;
  /* Make sure the following data is properly aligned, particularly
     that sizeof (heap_info) + 2 * SIZE_SZ is a multiple of
     MALLOC_ALIGNMENT.
  */
  char pad[-6 * SIZE_SZ & MALLOC_ALIGN_MASK];
} heap_info;

/* Thread specific data */

struct tsd_arena {
  pthread_key_t key;
  pthread_mutex_t list_lock;
  size_t narenas;
  mstate free_list;
};

static struct tsd_arena tsd_arena = {
  .list_lock = PTHREAD_MUTEX_INITIALIZER,
  .narenas = 0,
  .free_list = NULL,
};

/*!
 * @brief Returns the arena associated with this thread.
 */
static inline mstate arena_lookup(void)
{
  return (mstate)pthread_getspecific(tsd_arena.key);
}

/*!
 * @brief Returns the arena associated with this thread after locking it.
 *
 * If the thread doens't have an arena associated with it, it allocates a new
 * arena via `arena_get2`.
 */
static inline mstate arena_get(INTERNAL_SIZE_T size)
{
  mstate ptr = arena_lookup();
  if (ptr) {
    (void)pthread_mutex_lock(&ptr->mutex);
  } else {
    ptr = arena_get2(size, NULL);
  }
  return ptr;
}

/*!
 * @brief Returns a pointer to the `heap_info` associated with `ptr`.
 *
 * This just uses masking and should only be used on non mmapped chunks.
 */
static heap_info* heap4ptr(uintptr_t ptr)
{
  return (heap_info*)((unsigned long)(ptr) & ~(HEAP_MAX_SIZE - 1));
}

static mstate arena4chunk(mchunkptr ptr)
{
  return heap4ptr((uintptr_t)ptr)->ar_ptr;
}

#include <sys/mman.h>

static heap_info* new_heap(size_t size, size_t top_pad)
{
  size_t page_mask = sysconf(_SC_PAGESIZE);
  char *p1, *p2;
  unsigned long ul;
  heap_info *h;

  if (page_mask == 0)
    return 0;
  page_mask -= 1;

  if (size + top_pad < HEAP_MIN_SIZE) {
    size = HEAP_MIN_SIZE;
  } else if (size + top_pad <= HEAP_MAX_SIZE) {
    size += top_pad;
  } else if (size > HEAP_MAX_SIZE) {
    return 0;
  } else {
    size = HEAP_MAX_SIZE;
  }

  size = (size + page_mask) & ~page_mask;
  p1 = (char *)mmap(0, HEAP_MAX_SIZE << 1, PROT_NONE,
      MAP_NORESERVE|MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
  if (p1 != NULL) {
    p2 = (char *)(((unsigned long)p1 + (HEAP_MAX_SIZE - 1))
        & ~(HEAP_MAX_SIZE - 1));
    ul = p2 - p1;
    if (ul) {
      munmap(p1, ul);
    }
    munmap(p2 + HEAP_MAX_SIZE, HEAP_MAX_SIZE - ul);
  } else {
    p2 = (char *)mmap(0, HEAP_MAX_SIZE, PROT_NONE,
        MAP_NORESERVE|MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    if (p2 == NULL)
      return 0;
    if ((unsigned long)p2 & (HEAP_MAX_SIZE - 1)) {
      munmap(p2, HEAP_MAX_SIZE);
      return 0;
    }
  }
  if (mprotect(p2, size, PROT_READ|PROT_WRITE) != 0) {
    munmap(p2, HEAP_MAX_SIZE);
    return 0;
  }
  h = (heap_info *)p2;
  h->size = size;
  h->mprotect_size = size;
  return h;
}

static int grow_heap(heap_info *h, long diff)
{
  size_t page_mask = sysconf(_SC_PAGESIZE);
  long new_size;
  if (page_mask == 0)
    return -1;
  page_mask -= 1;

  diff = (diff + page_mask) & ~page_mask;
  new_size = (long)h->size + diff;
  if ((unsigned long)new_size > (unsigned long)HEAP_MAX_SIZE)
    return -2;
  if ((unsigned long)new_size > h->mprotect_size) {
    if (mprotect((char *)h + h->mprotect_size,
          (unsigned long) new_size - h->mprotect_size,
          PROT_READ|PROT_WRITE) != 0)
      return -3;
    h->mprotect_size = new_size;
  }
  h->size = new_size;
  return 0;
}

static int shrink_heap(heap_info *h, long diff)
{
  long new_size;

  new_size = (long)h->size - diff;
  if (new_size < (long)sizeof(*h))
    return -1;
   madvise((char*)h + new_size, diff, MADV_DONTNEED);
   h->size = new_size;
   return 0;
}

static inline void delete_heap(heap_info *h)
{
  munmap(h, HEAP_MAX_SIZE);
}

#define TOP_PAD 0

struct mallglob {
  mstate main_arena;
};

struct mallglob mg = {
  .main_arena = NULL,
};

#include <stdatomic.h>

static mstate new_arena(size_t size)
{
  mstate a;
  heap_info *h;
  char *ptr;
  unsigned long misalign;

  h = new_heap(size + (sizeof(*h) + sizeof(*a) + MALLOC_ALIGNMENT),
    TOP_PAD);
  if (!h) {
    h = new_heap(sizeof(*h) + sizeof(*a) + MALLOC_ALIGNMENT, TOP_PAD);
    if (!h)
      return 0;
  }
  a = h->ar_ptr = (mstate)(h + 1);
  a->system_mem = a->max_system_mem = h->size;

  ptr = (char *)(a + 1);
  misalign = (unsigned long)chunk2mem((mchunkptr)ptr) & MALLOC_ALIGN_MASK;
  if (misalign > 0)
    ptr += MALLOC_ALIGNMENT - misalign;
  a->top = (mchunkptr)ptr;
  set_head(a->top, (((char *)h + h->size) - ptr) | PREV_INUSE);

  pthread_setspecific(tsd_arena.key, (void *)a);
  /* TODO: For now a fast mutex should be fine. Check later. */
  pthread_mutex_init(&a->mutex, NULL);
  if (pthread_mutex_lock(&a->mutex) != 0) {
    // TODO: Specify course of action to take on critical locking falure
    return 0;
  }

  pthread_mutex_lock(&tsd_arena.list_lock);
  /* Use atomic_thread_fence to ensure the relevant fields are written to,
   * before `a` get's linked into the list.
   * Pure compiler optimization protection.
   */
  if (mg.main_arena) {
    a->next = mg.main_arena->next;
    atomic_thread_fence(memory_order_release);
    mg.main_arena->next = a;
  } else {
    a->next = a;
    atomic_thread_fence(memory_order_release);
    mg.main_arena = a;
  }
  pthread_mutex_unlock(&tsd_arena.list_lock);
  return a;
}
