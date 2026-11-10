#include <stddef.h>     /* For size_t. */

#include <bits/wordsize.h>
#include <sys/sysinfo.h>

#include <errno.h>

#include <stdint.h>     /* For uintptr_t. */
#include <sys/param.h>  /* For MIN, MAX, powerof2. */


#ifndef INTERNAL_SIZE_T
#define INTERNAL_SIZE_T size_t
#endif

/* The corresponding word size. */
#define SIZE_SZ (sizeof (INTERNAL_SIZE_T))

#ifndef MALLOC_ALIGNMENT
#define MALLOC_ALIGNMENT (2 * SIZE_SZ)
#endif

/* The corresponding bit mask */
#define MALLOC_ALIGN_MASK (MALLOC_ALIGNMENT - 1)

#ifndef MALLOC_DEBUG
#define MALLOC_DEBUG 0
#endif

#if USE_TCACHE
/* 64 entries. Arbitrary limit that tunables can reduce. */
# define TCACHE_MAX_BINS 64
# define MAX_TCACHE_SIZE tidx2usize (TCACHE_MAX_BINS - 1)

/* Onlt used to pre-fill the tunables. */
# define tidx2usize(idx) (((size_t) idx) * MALLOC_ALIGNMENT + MINSIZE - SIZE_SZ)

/* When 'x' is from chunksize(). */
# define csize2tidx(x) (((x) - MINSIZE + MALLOC_ALIGNMENT - 1) / MALLOC_ALIGNMENT)
/* When 'x' is a user-provided size. */
# define usize2tdix(x) csize2idx (request2size (x))

/* With rounding and alignment, the bins are...
 * idx 0  bytes 0..24 (64-bit) or 0..12 (32-bit)
 * idx 1  bytes 25..40 or 13..20
 * idx 2  bytes 41..56 or 21..28
 * etc. */

/* This is another arbitrary limit, which tunables can change. Each
 * tcache bin will hold at most this number of chunks. */
# define TCACHE_FILL_COUNT 7

/* Maximum chunks in tcache bin for tunables. This value must fit the range
 * of tcache->counts[] entries, else they may overflow. */
# define MAX_TCACHE_COUNT UINT16_MAX
#endif

/* Safe-Linking:
 * Use randomness from ASLR (mmap_base) to protect single-linked lists
 * of Fast-Bins and TCache. That is, mask the "next" pointer of the
 * lists' chunks, and also perform allocation alignment checks on them.
 * This mechanism reduces the risk of pointer hijacking, as was done with
 * Safe-Unlinking in the double-linked lists of Smal-Bins.
 * It assumes a minimum page size of 4096 bytes (12 bits). Systems with
 * larger pages provide less entropy, although the pointer mangling
 * still works. */
#define PROTECT_PTR(pos, ptr) \
  ((__typeof (ptr)) ((((size_t) pos) >> 12) ^ ((size_t) ptr)))
#define REVEAL_PTR(ptr) PROTECT_PTR (&ptr, ptr)

/*
 * TRIM_FASTBINS controls whether free() of a very small chunk can
 * immediately lead to trimming. Setting to true (1) can reduce memory
 * footprint, but will almost always slow down programs that use a lot
 * of small chunks.
 */

#ifndef TRIM_FASTBINS
#define TRIM_FASTBINS 0
#endif

/* TODO look into MEMORY_TAGGING (__lib_mtag_XXX) */

/* mallopt tuning options */

/*
 * M_MXFAST is the maxiumum request size used for "fastbins", special bins
 * that hold returned chunks without consolidating their spaces. This
 * enables future requests for chunks of the same size to be handled
 * very quickly, but can increase fragmentation, and thus increase the
 * overall memory footprint of a program.
 *
 * The maximum supported value of MXFAST is 80. You wouldn't want it any
 * higher than that anyway. Fastbins are designed especially for use with
 * many small structs, objects or strings -- the default handles structs/
 * objects/arrays with sizes up to 8 4byte fields, or small strings
 * representing words, tokens, etc.
 *
 * M_MXFAST is set in REQUEST size units. It is internally used in
 * chunksize units, which adds padding and alignment. You can reduce
 * M_MXFAST to 0 to disable all use of fastbins. This causes the malloc
 * algorithm to be a closer aproximation of fifo-best-fit in all cases,
 * not just for larger requests, but will generally cause it to be
 * slower.
 */

#ifndef M_MXFAST
#define M_MXFAST 1
#endif

#ifndef DEFAULT_MXFAST
#define DEFAULT_MXFAST (64 * SIZE_SZ / 4)
#endif

#ifndef DEFAULT_MMAP_THRESHOLD_MIN
#define DEFAULT_MMAP_THRESHOLD_MIN (128 * 1024)
#endif

#ifndef DEFAULT_MMAP_THRESHOLD_MAX
#define DEFAULT_MMAP_THRESHOLD_MAX (4 * 1024 * 1024 * sizeof(long))
#endif

/*
 * M_MMAP_THRESHOLD is the request size threshold for using mmap()
 * to service a request. Reqeuests of at least this size that cannot be
 * allocated using already-existing space will be serviced via mmpa.
 * (If enough normal freed space already exists it is used instead.)
 */

#define M_MMAP_THRESHOLD -3

#ifndef DEFAULT_MMAP_THRESHOLD
#define DEFAULT_MMAP_THRESHOLD DEFAULT_MMAP_THRESHOLD_MIN
#endif

/*
 * M_MMAP_MAX is the maximum number of requests to simulatneously
 * service using mmap. This parameter exists because
 * some systems have a limited number of internal tables for
 * use by mmap, and using more than a few of them may degrade
 * performance.
 * 
 * The default is set to a value that serves only as a safequard.
 * Setting to 0 disables use of mmap for servicing large requests.
 */

#define M_MMAP_MAX -4

#ifndef DEFAULT_MMAP_MAX
#define DEFAULT_MMAP_MAX (65536)
#endif

/* Forward declartations. */
struct malloc_chunk;
typedef struct malloc_chunk* mchunkptr;

struct malloc_state;
typedef struct malloc_state* mstate;

/* Internal routines */

static void* _int_malloc(mstate, size_t);
static void  _int_free(mstate, mchunkptr, int);
static void* _int_realloc(mstate, mchunkptr, INTERNAL_SIZE_T, INTERNAL_SIZE_T);

/* ---------------- MMAP support ---------------- */

#include <fcntl.h>
#include <sys/mman.h>

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
# define MAP_ANONYMOUS MAP_ANON
#endif

#define MMAP(addr, size, prot, flags) \
  mmap((addr), (size), (prot), (flags)|MAP_ANONYMOUS|MAP_PRIVATE, -1, 0)

/* ---------------- Chunk representations ---------------- */

struct malloc_chunk {
  INTERNAL_SIZE_T mchunk_prev_size; /* Size of previous chunk (if free).  */
  INTERNAL_SIZE_T mchunk_size;      /* Size in bytes, including overhead. */

  struct malloc_chunk* fd;  /* Double links -- used only if free. */
  struct malloc_chunk* bk;

  /* Only used for large blocks: pointer to next larger size. */
  struct malloc_chunk* fd_nextsize; /* Double links -- used only if free. */
  struct malloc_chunk *bk_nextsize;
};

/* Conversion from malloc to user pointers, and back. */

/* The chunk header is two SIZE_SZ elements, but this is used widely, so
 * we define it here for clarity later. */
#define CHUNK_HDR_SIZE (2 * SIZE_SZ)

/* Convert a chunk address to a user mem pointer. */
#define chunk2mem(p) ((void*)((char*)(p) + CHUNK_HDR_SZ))

/* Convert a user mem pointer to a chunk address. */
#define mem2chunk(mem) ((mchunkptr)((char*)(mem) - CHUNK_HDR_SZ))

/* Smallest possible chunk size. */
#define MIN_CHUNK_SIZE (offsetof(struct malloc_chunk, fd_nextsize))

/* The smallest size we can malloc is an aligned minimal chunk. */
#define MINSIZE \
  (unsigned long)(((MIN_CHUNK_SIZE+MALLOC_ALIGN_MASK) & ~MALLOC_ALIGN_MASK))

/* Check if 'm' has acceptable alignment. */
#define aligned_OK(m) (((unsigned long)(m) & MALLOC_ALIGN_MASK) == 0)


/*
 * Check if a request is so large that it would wrap around zero when
 * padded and aligned. To simplify some other code, the bound is made
 * low enough so that adding MINSIZE will also ot wrap around zero.
 */
#define REQUEST_OUT_OF_RANGE(req) \
  ((unsigned long)(req) >= \
   (unsigned long)(INTERNAL_SIZE_T)(-2 * MINSIZE))

/* pad request bytes into a usable size -- internal version. */
#define request2size(req) \
  (((req) + SIZE_SZ + MALLOC_ALIGN_MASK < MINSIZE) ? \
   MINSIZE : \
   ((req) + SIZE_SZ + MALLOC_ALIGN_MASK) & ~MALLOC_ALIGN_MASK)

/* Same as `request2size`, except also performs argument check. */
#define checked_request2size(req, sz) \
  if (REQUEST_OUT_OF_RANGE(req)) { \
    errno = ENOMEM; \
    return 0; \
  } \
  (sz) = request2size(req);

/* ---------------- Physical chunk operations ---------------- */

/* Size field is or'ed with PREV_INUSE when previous adjacent chunk in use. */
#define PREV_INUSE 0x1

/* Check if previous chunk is inuse. */
#define prev_inuse(p) ((p)->size & PREV_INUSE)

/* Size field is or'ed with IS_MMAPPED of the chunk was obtained with mmap(). */
#define IS_MMAPPED 0x2

/* Check if chunk was mmap()'ed. */
#define chunk_is_mapped(p) ((p)->size & IS_MMAPPED)

/* Size field is or'ed with NON_MAIN_ARENA if the chunk was obtained
 * from a non-main arena. This is only set immediately before handing
 * the chunk to the user, if necessary. */
#define NON_MAIN_ARENA 0x4

/* Check for chunk from non-main arena. */
#define chunk_non_main_arena(p) ((p)->size & NON_MAIN_ARENA)

/* Bits to mask of when extracting size. */
#define SIZE_BITS (PREV_INUSE|IS_MMAPPED|NON_MAIN_ARENA)

/* Get size of chunk, ignoring use bits. */
#define chunksize(p) ((p)->size & ~(SIZE_BITS))

/* Ptr to next physical malloc_chunk. */
#define next_chunk(p) ((mchunkptr)( ((char*)(p)) + ((p)->size & ~SIZE_BITS) ))

/* Ptr to previous physical malloc_chunk. */
#define prev_chunk(p) ((mchunkptr)( ((char*)(p)) - ((p)->prev_size) ))

/* Treat space at ptr + offfset as a chunk. */
#define chunk_at_offset(p, s) ((mchunkptr)(((char*)(p)) + (s)))

/* Extract p's insue bit. */
#define inuse(p) \
  ((((mchunkptr)(((char*)(p))+((p)->size & ~SIZE_BITS)))->size) & PREV_INUSE)

/* Set/clear chunk as being inuse without otherwise disturbing. */
#define set_inuse(p) \
  ((mchunkptr)(((char*)(p)) + ((p)->size & ~SIZE_BITS)))->size |= PREV_INUSE

#define clear_inuse(p) \
  ((mchunkptr)(((char*)(p)) + ((p)->size & ~SIZE_BITS)))->size &= ~(PREV_INUSE)

/* Check/set/clear inuse bits in known places. */
#define inuse_bit_at_offset(p, s) \
  (((mchunkptr)(((char*)(p)) + (s)))->size & PREV_INUSE)

#define set_inuse_bit_at_offset(p, s) \
  (((mchunkptr)(((char*)(p)) + (s)))->size |= PREV_INUSE)

#define clear_inuse_bit_at_offset(p, s) \
  (((mchunkptr)(((char*)(p)) + (s)))->size &= ~(PREV_INUSE))

/* Set size at head, without disturbing its use bit. */
#define set_head_size(p, s) ((p)->size = (((p)->size & SIZE_BITS) | (s)))

/* Set size/use field. */
#define set_head(p, s) ((p)->size = (s))

/* Set size at footer (only when chunk is not in use). */
#define set_foot(p, s) (((mchunkptr)((char*)(p) + (s)))->prev_size = (s))

/* ---------------- Internal data structures ---------------- */

/* Bins
 *
 *   An array of bin headers for free chunks. Each bin is doubly
 *   linked. The bins are approximately proportionally (log) spaced.
 *   There are a lot of these bins (128).
 *
 *   Chunks in bins are kept in size order, with ties going to the
 *   approximately leas recently used chunk. Ordering isn't needed
 *   for the small bins, which all contain the same-sized chunks, but
 *   facilitates best-fit allocation for large chunks. These lists
 *   are jsut sequential. Keeping them in order almost never requires
 *   enough traversal to warrant using fancier ordered data structrues.
 *
 *   Chunks of the same size are linked with the most
 *   recently freed at the front, and allocations are taken from the
 *   back. This results in LRU (FIFO) allocation order, which tends
 *   to give each chunk an equal opportunity to be consolidated with
 *   adjacent freed chunks, resulting in larger free chunks and less
 *   fragmentation.
 *
 *   To simplify use in double-linked lists, each bin header acts
 *   as a malloc_chunk. This avoids special-casing for headers.
 *   But to conserve space and improve locality, we allocate
 *   only the fd/bk pointers of bins, and then use repositioning tricks
 *   to treat these as the fields of a malloc_chunk*.
 */

typedef struct malloc_chunk* mbinptr;

/* Addressing -- note that bin_at(0) does not exist. */
#define bin_at(m, i) \
  (mbinptr) (((char*) &((m)->bins[((i) - 1) * 2])) \
      - offsetof (struct malloc_chunk, fd))

/* Analog of ++bin. */
#define next_bin(b) ((mbinptr)((char *)(b) + (sizeof(mchunkptr)<<1)))

/* Reminders about list directionality within bins. */
#define first(b) ((b)->fd)
#define last(b) ((b)->bk)

/* Take a chunk of a bin list. */
#define unlink(P, BK, FD) {                                                    \
  FD = P->fd;                                                                  \
  BK = P->bk;                                                                  \
  if (__builtin_expect (FD->bk != P || BK->fd != P, 0)) {                      \
    /* malloc_printerr (check_action, "corrupted double-linked lists", P); */  \
    ;                                                                          \
  } else {                                                                     \
    FD->bk = BK;                                                               \
    BK->fd = FD;                                                               \
    if (!in_smallbin_range (P->size)                                           \
        && __builtin_expect (P->fd_nextsize != NULL, 0)) {                     \
      assert (P->fd_nextsize->bk_nextsize == P);                               \
      assert (P->bk_nextsize->fd_nextsize == P);                               \
      if (FD->fd_nextsize == NULL) {                                           \
        if (P->fd_nextsize == P) {                                             \
          FD->fd_nextsize = FD->bk_nextsize = FD;                              \
        } else {                                                               \
          FD->fd_nextsize = P->fd_nextsize;                                    \
          FD->bk_nextsize = P->bk_nextsize;                                    \
          P->fd_nextsize->bk_nextsize = FD;                                    \
          P->bk_nextsize->fd_nextsize = FD;                                    \
        }                                                                      \
      } else {                                                                 \
        P->fd_nextsize->bk_nextsize = P->bk_nextsize;                          \
        P->bk_nextsize->fd_nextsize = P->fd_nextsize;                          \
      }                                                                        \
    }                                                                          \
  }                                                                            \
}

/* Indexing
 *
 *   Bins for sizes < 512 butes contain chunks of all the same size, spaced
 *   8 bytes apart. Larger bins are approximately logarithmically spaced:
 *
 *   64 bins of size       8
 *   32 bins of size      64
 *   16 bins of size     512
 *    8 bins of size    4096
 *    4 bins of size   32768
 *    2 bins of size  262144
 *    1 bin  of size what's left
 *
 *   The bins top out around 1MB because we expect to service large
 *   requests via mmap.
 *
 *   Bin 0 does not exist. Bin 1 is the unordered list; if that would be
 *   a valid chunk size the small bins are bumped up one.
 */

#define NBINS 128
#define NSMALLBINS 64
#define SMALLBIN_WIDTH MALLOC_ALIGNMENT
#define SMALLBIN_CORRECTION (MALLOC_ALIGNMENT > 2 * SIZE_SZ)
#define MIN_LARGE_SIZE ((NSMALLBINS - SMALLBIN_CORRECTION) * SMALLBIN_WIDTH)

#define in_smallbin_range(sz) \
  ((unsigned long)(sz) < (unsigned long)MIN_LARGE_SIZE)

#define smallbin_index(sz) \
  ((SMALLBIN_WIDTH == 16 ? (((unsigned)(sz)) >> 4) : (((unsigned)(sz)) >> 3))  \
   + SMALLBIN_CORRECTION)

#define largebin_index(sz)                                                     \
  (((((unsigned long)(sz)) >>  6) <= 48)?  48 + (((unsigned long)(sz)) >>  6): \
   ((((unsigned long)(sz)) >>  9) <= 20)?  91 + (((unsigned long)(sz)) >>  6): \
   ((((unsigned long)(sz)) >> 12) <= 10)? 110 + (((unsigned long)(sz)) >> 12): \
   ((((unsigned long)(sz)) >> 15) <=  4)? 119 + (((unsigned long)(sz)) >> 15): \
   ((((unsigned long)(sz)) >> 18) <=  2)? 124 + (((unsigned long)(sz)) >> 18): \
    126)

#define bin_index(sz) \
  ((int_xmallbin_range(sz)) ? smallbin_index(sz) : largebin_index(sz))

/*
 * Unsorted chunks
 *
 *   All remainders from chunk splits, as well as all returned chunks,
 *   are first placed in the "unsorted" bin. They are then placed
 *   in regular bins after malloc gives them ONE change to be used before
 *   binning. So, basically, the unsorted_chunks list acts as a queue,
 *   with chunks being placed in it on free (and malloc_consolidate),
 *   and taken off (to be either used or placed in bins) in malloc.
 *
 *   The NON_MAIN_ARENA flag is never set for unsorted chunks, so it
 *   does not have to be taken into account in size comparisons.
 */

/* The otherwise unindexable 1-bin is used to hold unsorted chunks. */
#define unsorted_chunks(M) (bin_at(M, 1))

/*
 * Top
 *
 *   The top-most availble chunk (i.e., the one bordering the end of
 *   available memory) is treated specifally. It is never included in
 *   any bin, is used only if no other chunk is available. Because top
 *   initially points to it's own bin with initial zero size, thus forcing
 *   extension on the first malloc request, we avoid havin any special
 *   code in malloc to check whether it even exists yet. But we still
 *   need to do so when getting memory from system, so we make
 *   initial_top treat the bin as a legal but unusable chunk during the
 *   interval between initialization and the first call to
 *   sysmalloc. (This is somewhat delicate, since it relies on
 *   the 2 preceding words to be zro during this interval as well.)
 */

/* Conveniently, the unsorted bin can be used as dummy top on first call. */
#define initial_top(M) (unsorted_chunks(M))

/*
 * Binmap
 *
 *   To help compensate for the large number of bins, a one-level index
 *   structure is used for bin-by-bin searching. `binmap` is a
 *   bitvector recording whether bins are definitely empty so they can
 *   be skipped over during traversals. The bits are NOT always cleared
 *   as soon as bins are empty, but instead only when they are
 *   noticed to be empty during traversal in malloc.
 */

/* Conservatively use 32 bits per map word, even if on 64bit system. */
#define BINMAPSHIFT       5
#define BITSPERMAP        (1U << BINMAPSHIFT)
#define BINMAPSIZE        (NBINS / BITSPERMAP)

#define idx2block(i)      ((i) >> BINMAPSHIFT)
#define idx2bit(i)        ((1U << ((i) & ((1U << BINMAPSHIFT)-1))))

#define mask_bin(m, i)    ((m)->binmap[idx2block(i)] |=  idx2bit(i))
#define unmark_bin(m, i)  ((m)->binmap[idx2block(i)] &= ~(idx2bit(i)))
#define get_binmap(m, i)  ((m)->binmap[idx2block(i)] &   idx2bit(i))

/*
 * Fastbins
 *
 *   An array of lists holding recently freed small chunks. Fastbins
 *   are not double linked. It is faster to single-link them, and
 *   since chunks are never removed from the middles of thesse lists,
 *   double linking is not necessary. Also, unlike regular bins, they
 *   are not even processed in FIFO order (they use faster LIFO) since
 *   ordering doesn't matter in the transient contexts in which
 *   fastbins are normally used.
 *
 *   Chunks in fastbins keep their inuse bit set, so they cannot
 *   be consolidated with other free chunks. malloc_consolidate
 *   releases all chunks in fastbins and consolidates them with
 *   other free chunks.
 */

typedef struct malloc_chunk* mfastbinptr;
#define fastbin(ar_ptr, idx) ((ar_ptr)->fastbinsY[idx])

/* Offset 2 to use otherwise unindexable first 2 bins. */
#define fastbin_index(sz) \
  ((((unsigned int)(sz)) >> (SIZE_SZ == 8 ? 4 : 3)) - 2)

/* The maximum fastbin request size we support. */
#define MAX_FAST_SIZE (80 * SIZE_SZ / 4)

/* The number of fastbins. */
#define NFASTBINS (fastbin_index(request2size(MAX_FAST_SIZE))+1)

/*
 * FASTCHUNKS_BIT held in max_fast indicates that there are probably
 * some fastbin chunks. It is set true on entering a chunk into any
 * fastbin, and cleared only in malloc_consolidate.
 *
 * The truth value is inverted so that have_fastchunks will be true
 * upon startup (since statics are zero-filled), simplifying
 * initialization checks.
 */

#define FASTCHUNKS_BIT (1U)

#define have_fastchunks(M) (((M)->flags & FASTCHUNKS_BIT) == 0)
#define clear_fastchunks(M) __sync_fetch_and_or (&(M)->flags, FASTCHUNK_BIT)
#define set_fastchunks(M) __sync_fetch_and_adn (&(M)->flags, FASTCHUNK_BIT)

#include <pthread.h>

typedef pthread_mutex_t mutex_t;

/* -------- Internal state representation and initialization -------- */

struct malloc_state {
  /* Serialize access */
  mutex_t mutex;

  /* Flags */
  int flags;

  /* Fastbins */
  mfastbinptr fastbinsY[NFASTBINS];

  /* Base of the topmost chunk -- not otherwise kept in a bin. */
  mchunkptr top;

  /* The remainder from the most recent split of a small request. */
  mchunkptr last_remainder;

  /* Normal bins packed as described above. */
  mchunkptr bins[NBINS * 2 - 2];

  /* Bitmap of bins */
  unsigned int binmap[BINMAPSIZE];

  /* Linked list */
  struct malloc_state *next;

  /* Memory allocated from the system in this arena. */
  INTERNAL_SIZE_T system_mem;
  INTERNAL_SIZE_T max_system_mem;
};

struct malloc_par {
  /* Tunable parameters */
  INTERNAL_SIZE_T mmap_threshold;

  /* Memory map support */
  int n_mmaps;
  int n_mmaps_max;
  int max_n_mmaps;

  int no_dyn_threshold;

  INTERNAL_SIZE_T max_fast;

  /* Statistics */
  INTERNAL_SIZE_T mmapped_mem;
  INTERNAL_SIZE_T max_mmapped_mem;
  INTERNAL_SIZE_T max_total_mem;
};

struct mall_info {
  struct malloc_par params;
  struct malloc_state main_arena;
};

#define MAIN_ARENA mi.main_arena
#define MXFAST mi.params.max_fast

static struct mall_info mi = {
  .params = {
    .n_mmaps_max = DEFAULT_MMAP_MAX,
    .mmap_threshold = DEFAULT_MMAP_THRESHOLD,
    .max_fast = DEFAULT_MXFAST,
  },
  .main_arena = {
    .mutex = NULL,
    .next = &MAIN_ARENA,
  }
};

static void malloc_init_state(mstate av)
{
  int i;
  mbinptr bin;

  for (i = 1; i < NBINS; ++i) {
    bin = bin_at(av, i);
    bin->fd = bin->bk = bin;
  }
  av->flags |= FASTCHUNKS_BIT;
  av->top = initial_top(av);
}


