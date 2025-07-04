/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * Segmented explicit linked list implementation
 */
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "bteam",
    /* First member's full name */
    "tgair",
    /* First member's email address */
    "tgair@csapp.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1 << 12)

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE))
#define ALIGN_NEAREST(size) (DSIZE * ((size + (DSIZE - 1)) / DSIZE))

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define PACK(word, alloc) ((word) | (alloc))

#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_SIZE(p) (GET(p) & ~0x7)

#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define N_BLK(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define P_BLK(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))

typedef char *opcode_t;
static void *heap_listp;

static void *get_fit(size_t asize);
static void *
find_fit(size_t asize) __attribute_maybe_unused__; // ignore compiler warnings
static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *coalesce(void *bp);
static void mm_check(opcode_t opcode) __attribute_maybe_unused__;
static void handle_segfault(int sig);

/***************************************************
 * SegList wrapper Implementation
 ***************************************************/

#define SEG_MAX 14 // 1<<14 max

// seglist datastructure - head_nodes of the segmented lists
static void *seglist_start[SEG_MAX];

/* Given a free block size, get the index of the seglist it belongs to*/
int get_index(size_t fbsize) {
  if (fbsize == 0)
    return -1;
  int idx = 63 - __builtin_clzll(fbsize);
  return idx >= SEG_MAX ? SEG_MAX - 1 : idx;
}

/***************************************************
 * ELL Implementation
 ***************************************************/

#define PREV_FBLK(bp)                                                          \
  (*(void *                                                                    \
         *)(bp)) // (*(unsigned int *)(bp)) works fine for 32 bit (ul will
                 // probably work for 62 bit) but causes the compiler to warn
#define NEXT_FBLK(bp) (*(void **)((char *)(bp) + WSIZE))

// static char *_ell_start;
// static char *_ell_end;
/* Insert a free block naively at the start (maybe do the address ordering
 * later) */
void insert_free_block(void *bp) {
  /*
  Idea for an individual ELL
  prev_start -> prev = bp;
  bp->prev = null;
  bp->next = prev_start;
  start = bp;
  */

  int index = get_index(GET_SIZE(HDRP(bp)));

  //   printf("size: %d index: %d\n", GET_SIZE(HDRP(bp)), index);
  NEXT_FBLK(bp) = seglist_start[index];
  PREV_FBLK(bp) = NULL;

  if (seglist_start[index] != NULL && seglist_start[index] != 0x0)
    PREV_FBLK(seglist_start[index]) = bp;

  seglist_start[index] = bp;
}

/* Remove a block from the list */
void remove_free_block(void *bp) {
  /*
  bp->prev->next = bp->next;
  bp->next = prev_start;
  */
  if (PREV_FBLK(bp) && PREV_FBLK(bp) != 0x0)
    NEXT_FBLK(PREV_FBLK(bp)) = NEXT_FBLK(bp);
  else
    seglist_start[get_index(GET_SIZE(HDRP(bp)))] =
        NEXT_FBLK(bp); // In case bp is the start block

  if (NEXT_FBLK(bp) && NEXT_FBLK(bp) != 0x0)
    PREV_FBLK(NEXT_FBLK(bp)) = PREV_FBLK(bp);

  NEXT_FBLK(bp) = NULL;
  PREV_FBLK(bp) = NULL;
}

/*
 * Find the in a ELL - extended to use the seglist
 * Get the node from seglist and apply ELL logic
 */
void *find_fit_ll(size_t asize) {
  //   unsigned int *start = (unsigned int *)_ell_start;
  void *bp;
  for (int i = get_index(asize); i < SEG_MAX; i++) {
    void *start = seglist_start[i];
    if (start == NULL)
      continue;
    for (bp = start; (bp != 0x0) && (bp != NULL);) {
      if (GET_SIZE(HDRP(bp)) >= asize) {
        return bp;
      };
      bp = NEXT_FBLK(bp);
    }
  }

  return NULL;
}

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void) {
  if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1) {
    return -1;
  }

  PUT(heap_listp, 0);
  PUT(heap_listp + (WSIZE * 1), PACK(DSIZE, 1));
  PUT(heap_listp + (WSIZE * 2), PACK(DSIZE, 1));
  PUT(heap_listp + (WSIZE * 3), PACK(0, 1));
  heap_listp += (2 * WSIZE);

  // initialize the seglist to nulls
  for (int i = 0; i < SEG_MAX; i++)
    seglist_start[i] = NULL;

  if (extend_heap(CHUNKSIZE / WSIZE) == NULL) {
    return -1;
  }
  return 0;
}

/******************************************
 * Utils start
 ******************************************/

static const opcode_t OP_FREE = "free";
static const opcode_t OP_ALLOC = "alloc";
static const opcode_t OP_REALLOC = "realloc";
static const opcode_t OP_FAULT = "fatal error";

static void mm_check(opcode_t op_code) {
  // traverse and print out the implicit list layout
  void *bp;
#ifdef DEBUG
  printf("\n---op %s---\n", op_code);
  printf("heapstart-> ");
  for (bp = N_BLK(heap_listp); GET_SIZE(HDRP(bp)) > 0; bp = (N_BLK(bp))) {
    printf(
        "||bp: %p with hsize: %x fsize:%x (%d) and allocation status: %d|| -> ",
        (unsigned int *)bp, GET_SIZE(HDRP(bp)), GET_SIZE(FTRP(bp)),
        GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)));
  }
  printf("heapend\n");
#endif
}

static void handle_segfault(int sig) {
  printf("\nSIGSEGV\nDumping info through mm_check->");

  mm_check(OP_FAULT);

  exit(1);
}

/********************************************
 * Utils end
 ********************************************/

static void *extend_heap(size_t words) {
  char *bp;
  size_t size;

  size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

  if ((long)(bp = mem_sbrk(size)) == -1)
    return NULL;

  PUT(HDRP(bp), PACK(size, 0));
  PUT(FTRP(bp), PACK(size, 0));
  // void *nb = N_BLK(bp);
  PUT(HDRP(N_BLK(bp)), PACK(0, 1));

  return coalesce(bp);
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size) {
  mm_check("alloc init");
  //   int newsize = ALIGN(size + SIZE_T_SIZE);
  //   void *p = mem_sbrk(newsize);
  //   if (p == (void *)-1)
  //     return NULL;
  //   else {
  //     *(size_t *)p = size;
  //     return (void *)((char *)p + SIZE_T_SIZE);
  //   }
  signal(SIGSEGV, handle_segfault); // Register the handler

  size_t asize, extendsize;
  char *bp;

  if (size == 0)
    return NULL;

  if (size <= DSIZE)
    asize = 2 * DSIZE;
  else
    asize = ALIGN(size);

  if ((bp = get_fit(asize)) != NULL) {
    place(bp, asize);
    mm_check(OP_ALLOC);
    return bp;
  }

  extendsize = MAX(asize, CHUNKSIZE);

  if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
    return NULL;

  place(bp, asize);
  mm_check(OP_ALLOC);
  return bp;
}

/* Wrapper around "size fit" logic*/
static void *get_fit(size_t asize) { return find_fit_ll(asize); }

static void *find_fit(size_t asize) {
  void *bp;

  for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = N_BLK(bp)) {
    if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
      return bp;
    }
  }
  return NULL;
}

static void place(void *bp, size_t asize) {
  size_t csize = GET_SIZE(HDRP(bp));

  remove_free_block(bp);

  if ((csize - asize) >= (2 * DSIZE)) {
    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));
    bp = N_BLK(bp);
    PUT(HDRP(bp), PACK(csize - asize, 0));
    PUT(FTRP(bp), PACK(csize - asize, 0));
    insert_free_block(bp);
  } else {
    PUT(HDRP(bp), PACK(csize, 1));
    PUT(FTRP(bp), PACK(csize, 1));
  }
}

/*
 * mm_free - Freeing deallocates and coalesces the block
 */
void mm_free(void *bp) {
  size_t size = GET_SIZE(HDRP(bp));

  PUT(HDRP(bp), PACK(size, 0));
  PUT(FTRP(bp), PACK(size, 0));

  coalesce(bp);
  mm_check(OP_FREE);
}

static void *coalesce(void *bp) {
  size_t prev_alloc = GET_ALLOC(FTRP(P_BLK(bp)));
  size_t next_alloc = GET_ALLOC(HDRP(N_BLK(bp)));
  size_t size = GET_SIZE(HDRP(bp));

  void *prev = P_BLK(bp);
  void *next = N_BLK(bp);
  if (prev_alloc && next_alloc) {

  } else if (prev_alloc && !next_alloc) {
    size += GET_SIZE(HDRP(N_BLK(bp)));

    remove_free_block(next);
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
  } else if (!prev_alloc && next_alloc) {
    size += GET_SIZE(HDRP(P_BLK(bp)));

    remove_free_block(prev);
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(P_BLK(bp)), PACK(size, 0));

    bp = P_BLK(bp);
  } else {
    size += GET_SIZE(HDRP(P_BLK(bp))) + GET_SIZE(FTRP(N_BLK(bp)));

    remove_free_block(prev);
    remove_free_block(next);
    PUT(HDRP(P_BLK(bp)), PACK(size, 0));
    PUT(FTRP(N_BLK(bp)), PACK(size, 0));

    bp = P_BLK(bp);
  }

  insert_free_block(bp);
  return bp;
}

/*************************************************
 * REALLOC implementation
 *************************************************/

#define OVERHEAD (2 * WSIZE)

typedef enum adjacent_realloc_t {
  ADJ_REALLOC_NO_OP = 0,
  ADJ_REALLOC_NEXT = 1,
  ADJ_REALLOC_PREV = 2,
  ADJ_REALLOC_ALL = 3
} adjacent_realloc_t;

void *mm_realloc_(void *ptr, size_t size);
void *old_mm_realloc(void *ptr, size_t size);

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size) {

#ifdef DEBUG
  printf("\n--------realloc execution-------------");
  printf("\nptr to realloc: %p with size: %u\n", ptr, size);
#endif
  void *newptr = mm_realloc_(ptr, size);
#ifdef DEBUG
  printf("\n--------realloc executed-------------\n");
#endif
  return newptr;
}

adjacent_realloc_t can_realloc_adjacent(void *ptr, size_t asize) {
  int prev_alloc = GET_ALLOC(HDRP(P_BLK(ptr))),
      next_alloc = GET_ALLOC(HDRP(N_BLK(ptr)));
  size_t prev_size = GET_SIZE(HDRP(P_BLK(ptr))),
         next_size = GET_SIZE(HDRP(N_BLK(ptr)));
  size_t curr_size = GET_SIZE(HDRP(ptr));

  if (!next_alloc && (next_size + curr_size) >= asize) {
    return ADJ_REALLOC_NEXT; // prefer this because we won't need to memcpy
  }
  if (!prev_alloc && (prev_size + curr_size) >= asize) {
    return ADJ_REALLOC_PREV;
  }
  if (!prev_alloc && !next_alloc &&
      (prev_size + next_size + curr_size) >= asize) {
    return ADJ_REALLOC_ALL;
  }

#ifdef DEBUG
  printf("cant realloc adjacent!!!!!\n");
#endif
  return ADJ_REALLOC_NO_OP;
}

void *realloc_adjacent(void *bp, size_t asize, adjacent_realloc_t type) {
#ifdef DEBUG
  printf("\nrealloc type: %d", type);
#endif
  size_t csize = GET_SIZE(HDRP(bp));
  switch (type) {
  case ADJ_REALLOC_NEXT:
    csize += GET_SIZE(HDRP(N_BLK(bp)));
    remove_free_block(N_BLK(bp));
    if ((csize - asize) >= (2 * DSIZE)) {
      PUT(HDRP(bp), PACK(asize, 1));
      PUT(FTRP(bp), PACK(asize, 1));

      PUT(HDRP(N_BLK(bp)), PACK(csize - asize, 0));
      PUT(FTRP(N_BLK(bp)), PACK(csize - asize, 0));
      insert_free_block(N_BLK(bp));
    } else {
      PUT(HDRP(bp), PACK(csize, 1));
      PUT(FTRP(bp), PACK(csize, 1));
    }
    break;
  case ADJ_REALLOC_PREV:
    csize += GET_SIZE(HDRP(P_BLK(bp)));
    remove_free_block(P_BLK(bp));
    bp = P_BLK(bp);
    memcpy(bp, N_BLK(bp), GET_SIZE(HDRP(N_BLK(bp))));
    if ((csize - asize) >= (2 * DSIZE)) {
      PUT(HDRP(bp), PACK(asize, 1));
      PUT(FTRP(bp), PACK(asize, 1));

      PUT(HDRP(N_BLK(bp)), PACK(csize - asize, 0));
      PUT(FTRP(N_BLK(bp)), PACK(csize - asize, 0));
      insert_free_block(N_BLK(bp));
    } else {
      PUT(HDRP(bp), PACK(csize, 1));
      PUT(FTRP(bp), PACK(csize, 1));
    }
    break;
  case ADJ_REALLOC_ALL:
    csize += GET_SIZE(HDRP(P_BLK(bp))) + GET_SIZE(HDRP(N_BLK(bp)));
    remove_free_block(P_BLK(bp));
    remove_free_block(N_BLK(bp));
    bp = P_BLK(bp);
    memcpy(bp, N_BLK(bp), GET_SIZE(HDRP(N_BLK(bp))));
    if ((csize - asize) >= (2 * DSIZE)) {
      PUT(HDRP(bp), PACK(asize, 1));
      PUT(FTRP(bp), PACK(asize, 1));

      PUT(HDRP(N_BLK(bp)), PACK(csize - asize, 0));
      PUT(FTRP(N_BLK(bp)), PACK(csize - asize, 0));
      insert_free_block(N_BLK(bp));
    } else {
      PUT(HDRP(bp), PACK(csize, 1));
      PUT(FTRP(bp), PACK(csize, 1));
    }
    memcpy(bp, N_BLK(bp), asize);
    break;
  }

  mm_check(OP_REALLOC);
  return bp;
}

void *mm_realloc_(void *ptr, size_t size) {
  void *oldptr = ptr;
  void *newptr;
  size_t copySize;

  // Base cases
  if (ptr == NULL) {

    return mm_malloc(size);
  }
  if (size == 0) {
    mm_free(ptr);
    return;
  }

#ifdef DEBUG
  printf("size + overhead: %d", size + OVERHEAD);
#endif
  copySize = GET_SIZE(HDRP(ptr));
  size_t asize = ALIGN(size);
  if (asize <= copySize) {
    if (copySize - asize >= (2 * DSIZE)) {
      PUT(HDRP(N_BLK(ptr)), PACK(copySize - asize, 0));
      PUT(FTRP(N_BLK(ptr)), PACK(copySize - asize, 0));
      coalesce(N_BLK(ptr));
    }
    mm_check(OP_REALLOC);
    return ptr;
  }
  adjacent_realloc_t adjacent_realloc_type;
  adjacent_realloc_type = can_realloc_adjacent(ptr, asize);

  if (adjacent_realloc_type == ADJ_REALLOC_NO_OP) {
    newptr = mm_malloc(asize);
    if (newptr == NULL)
      return NULL;

    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    mm_check(OP_REALLOC);
    return newptr;
  }

  return realloc_adjacent(ptr, asize, adjacent_realloc_type);
}

/* bad :(*/
void *old_mm_realloc(void *ptr, size_t size) {
  void *oldptr = ptr;
  void *newptr;
  size_t copySize;

  // Base cases
  if (ptr == NULL) {

    mm_check(OP_REALLOC);
    return mm_malloc(size);
  }
  if (size == 0) {
    mm_free(ptr);
    mm_check(OP_REALLOC);
    return;
  }

  copySize = GET_SIZE(HDRP(oldptr));

  newptr = mm_malloc(size);
  if (newptr == NULL)
    return NULL;
  if (size < copySize)
    copySize = size;
  memcpy(newptr, oldptr, copySize);
  mm_free(oldptr);
  return newptr;
}