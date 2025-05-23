/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include "mm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"

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

/* single word (4) or double word (8) alignment */
#define DSIZE 8

#define WSIZE 4

#define MIN_SIZE (2 * (DSIZE))

#define CHUNKSIZE (1 << 12)

#define ALLOCATED 0x1

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (DSIZE - 1)) & ~0x7)

/* R/W word at address p*/
#define PUT(p, val) (*(unsigned int *)(p) = (val))
#define GET(p) (*(unsigned int *)(p))

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLCKP(bp) (char *)(bp) + ((GET_SIZE(HDRP(bp))))
#define PREV_BLCKP(bp) (char *)(bp) - ((GET_SIZE(HDRP(bp) - (WSIZE))))

#define PACK(val, alloc) (val | alloc)

#define MAX(x, y) ((x) > (y) ? (x) : (y))

static char *heap_listp;

static void *find_fit(size_t asize);
static void *extend_heap(size_t words);
static void *place(void *bp, size_t asize);
static void *coalesce(void *bp);

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void) {
  // mem_heap()

  if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1) {
    // TODO: handle error
    return -1;
  };

  PUT(heap_listp, 0);
  PUT(heap_listp + (WSIZE * 1), PACK(DSIZE, 1));
  PUT(heap_listp + (WSIZE * 2), PACK(DSIZE, 1));
  PUT(heap_listp + (WSIZE * 3), PACK(0, 1));
  heap_listp += (2 * WSIZE);

  if (extend_heap(CHUNKSIZE / WSIZE) == NULL) {
    return -1;
  }

  return 0;
}

static void *extend_heap(size_t words) {
  char *bp;
  size_t size;

  size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

  if ((long)(bp = mem_sbrk(size)) == -1)
    return NULL;

  PUT(HDRP(bp), PACK(size, !ALLOCATED));
  PUT(FTRP(bp), PACK(size, !ALLOCATED));
  PUT(HDRP(NEXT_BLCKP(bp)), PACK(0, ALLOCATED));

  return coalesce(bp);
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size) {
  // int newsize = ALIGN(size + SIZE_T_SIZE);
  // void *p = mem_sbrk(newsize);
  // if (p == (void *)-1)
  //     return NULL;
  // else {
  //     *(size_t *)p = size;
  //     return (void *)((char *)p + SIZE_T_SIZE);
  // }
  //

  // x64 -- 111....111 (64 bits) -> 2^64
  //   unsigned int(*p) = (unsigned int *)0xDEADBEEF; // 8 * 8 => 64

  size_t asize;
  char *bp;
  if (size < 2 * DSIZE) {
    asize = 2 * DSIZE;
  } else {
    asize = ALIGN(size + SIZE_T_SIZE);
  }

  if ((bp = find_fit(asize)) != NULL) {
    place(bp, asize);
    return bp;
  }
  if ((bp = extend_heap(MAX(asize, CHUNKSIZE) / WSIZE)) == NULL)
    return NULL;
  place(bp, asize);
  return bp;
}

static void *place(void *bp, size_t asize) {
  size_t curr_size = GET_SIZE(HDRP(bp));

  if ((curr_size - asize) >= MIN_SIZE) {
    PUT(HDRP(bp), PACK(asize, ALLOCATED));
    PUT(FTRP(bp), PACK(asize, ALLOCATED));
    bp = NEXT_BLCKP(bp);
    PUT(HDRP(bp), PACK((curr_size - asize), !ALLOCATED));
    PUT(FTRP(bp), PACK((curr_size - asize), !ALLOCATED));
  } else {
    PUT(HDRP(bp), PACK(curr_size, ALLOCATED));
    PUT(FTRP(bp), PACK(curr_size, ALLOCATED));
  }

  return bp;
}

static void *find_fit(size_t asize) {
  void *bp;

  for (bp = heap_listp; GET_SIZE(HDRP(bp)) != 0; bp = NEXT_BLCKP(bp)) {
    if (!GET_ALLOC(HDRP(bp)) && GET_SIZE(HDRP(bp)) >= asize) {
      return bp;
    }
  }
  return NULL;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr) {
  size_t _size = GET_SIZE(HDRP(ptr));
  PUT(HDRP(ptr), PACK(_size, !ALLOCATED));
  PUT(FTRP(ptr), PACK(_size, !ALLOCATED));

  coalesce(ptr);
}

static void *coalesce(void *bp) {
  int prev_allocated = GET_ALLOC(FTRP(PREV_BLCKP(bp)));
  int next_allocated = GET_ALLOC(HDRP(NEXT_BLCKP(bp)));

  size_t curr_size = GET_SIZE(HDRP(bp));

  if (prev_allocated && next_allocated)
    return bp;

  else if (!prev_allocated && next_allocated) {
    curr_size += GET_SIZE(HDRP(PREV_BLCKP(bp)));

    PUT(HDRP(PREV_BLCKP(bp)), PACK(curr_size, !ALLOCATED));
    PUT(FTRP(bp), PACK(curr_size, !ALLOCATED));
    bp = PREV_BLCKP(bp);
  }

  else if (prev_allocated && !next_allocated) {
    curr_size += GET_SIZE(HDRP(NEXT_BLCKP(bp)));

    PUT(FTRP(NEXT_BLCKP(bp)), PACK(curr_size, !ALLOCATED));
    PUT(HDRP(bp), PACK(curr_size, !ALLOCATED));
  }

  else {
    curr_size +=
        GET_SIZE(HDRP(NEXT_BLCKP(bp))) + GET_SIZE(HDRP(PREV_BLCKP(bp)));

    PUT(HDRP(PREV_BLCKP(bp)), PACK(curr_size, !ALLOCATED));
    PUT(FTRP(NEXT_BLCKP(bp)), PACK(curr_size, !ALLOCATED));

    bp = PREV_BLCKP(bp);
  }

  return bp;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size) {
  void *oldptr = ptr;
  void *newptr;
  size_t copySize;

  newptr = mm_malloc(size);
  if (newptr == NULL)
    return NULL;
  copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
  if (size < copySize)
    copySize = size;
  memcpy(newptr, oldptr, copySize);
  mm_free(oldptr);
  return newptr;
}
