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

#ifndef INTERNAL_SIZE_T
# define INTERNAL_SIZE_T size_t
#endif

#define SIZE_SZ (sizeof(INTERNAL_SIZE_T))

#ifndef MALLOC_ALIGNMENT
# define MALLOC_ALIGNMENT (2 * SIZE_SZ)
#endif

#define MALLOC_ALIGN_MASK (MALLOC_ALIGNMENT - 1)

/* -------- Chunk Representation -------------------------------------------- */

/* Forward declaration */
struct malloc_chunk;
typedef struct malloc_chunk* mchunkptr;

/* List extracted from malloc_chunk since it will be used for bins in
 * malloc_state aswell */
typedef struct malloc_chunk_list {
  mchunkptr fwd;
  mchunkptr bck;
} mchunklist;

struct malloc_chunk {
  /* Size of previous chunk (if free) */
  INTERNAL_SIZE_T prev_size;
  /* Size in bytes, including overhead. */
  INTERNAL_SIZE_T size;
  /* double links -- used only if free */
  mchunklist list;
  /* Only used for large blocks: pointers to next larger size. */
  /* double links -- used only if free */
  mchunklist nextsize;
};

#include <pthread.h>

typedef struct malloc_chunk* mfastbinptr;

struct malloc_state {
  pthread_mutex_t mutex;
  int flags;
  mfastbinptr fastbinsY[];
  mchunkptr top;
  mchunkptr last_remainder;
  mchunklist bins[];
  unsigned int binmap[];
  struct malloc_state *next;
  INTERNAL_SIZE_T system_mem;
  INTERNAL_SIZE_T max_system_mem;
};

/* -------- Internal Data Structures ---------------------------------------- */

typedef struct malloc_chunk* mbinptr;

