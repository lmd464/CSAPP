/*
 * mm-explicit.c - an empty malloc package
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 *
 * @id : 201702081
 * @name : 최재범
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif


/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(p) (((size_t)(p) + (ALIGNMENT-1)) & ~0x7)

// 매크로 ~ realloc
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
#define SIZE_PTR(p)  ((size_t*)(((char*)(p)) - SIZE_T_SIZE))


// 매크로
#define HDRSIZE   4
#define FTRSIZE   4
#define WSIZE 		4			                // Word 크기
#define DSIZE 		8			                // Double Word 크기
#define CHUNKSIZE	(1 << 12)			        // 초기 heap 크기
#define OVERHEAD	8			                // Header + Footer 크기

#define MAX(x, y)		((x) > (y) ? (x) : (y))		  // 둘중 더 큰 값
#define MIN(x, y)   ((x) < (y) ? (x) : (y))

#define PACK(size, alloc)	((unsigned) ((size) | (alloc)))		        // size, alloc 값을 하나의 Word로 묶음

#define GET(p)		        (*(unsigned*)(p))	                        // 포인터 p가 가리키는 위치에서 word 크기의 값을 읽음
#define PUT(p, val)       (*(unsigned*)(p) = (unsigned)(val))	      // 포인터 p가 가리키는 위치에 word 크기의 val 값을 씀
#define GET8(p)           (*(unsigned long*)(p))
#define PUT8(p, val)      (*(unsigned long*)(p) = (unsigned long)(val))

#define GET_SIZE(p)	(GET(p) & ~0x7)		        // Header에서 block size 읽을 때 사용 (한 워드 읽고 하위3비트 버림)
#define GET_ALLOC(p) 	(GET(p) & 0x1)		      // Header에서 alloc 여부 읽을 때 사용 (한 워드 읽고 하위1비트 읽음)
#define HDRP(bp)		((char*)(bp) - WSIZE)			// Header 주소 계산
#define FTRP(bp)		((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)	        // Header로부터 크기 참조하여, Footer 주소 계산
#define NEXT_BLKP(bp)	((char*)(bp) + GET_SIZE(((char*)(bp) - WSIZE)))	  // 다음 block의 주소 계산
#define PREV_BLKP(bp)	((char*)(bp) - GET_SIZE(((char*)(bp) - DSIZE)))	  // 이전 block의 주소 계산

#define NEXT_FREEP(bp)    ((char*)(bp))							// 현재 블록의 Next주소 가져옴
#define PREV_FREEP(bp)    ((char*)(bp) + DSIZE)					// 현재 블록의 Prev주소 가져옴
#define NEXT_FREE_BLKP(bp)  ((char*)GET8((char*)(bp)))			// Next block의 NEXT 가져옴
#define PREV_FREE_BLKP(bp)  ((char*)GET8((char*)(bp) + DSIZE))  // Prev block의 NEXT 가져옴

#define FREE_LISTP        ((char*)heap_start)         // Free List 루트의 주소
#define GET_FREE_LISTP    ((void*)GET8(FREE_LISTP))   // Free List

void* coalesce(void *bp);
void remove_block(void* bp);
void* extend_heap(size_t words);
void* find_fit(size_t asize);
void place(void *bp, size_t asize);

void *heap_listp;
void *heap_start;

/*
 * Initialize: return -1 on error, 0 on success.
 */
int mm_init(void) {

  // 초기 Empty Heap 생성
  if( (heap_listp = mem_sbrk(2*DSIZE + 4*WSIZE)) == NULL ) return -1;

  PUT8(heap_listp, NULL);           // root NEXT
  PUT8(heap_listp + DSIZE, NULL);   // root PREV

  heap_listp = heap_listp + 2*DSIZE;				// 포인터 뒤로 이동
  PUT(heap_listp, 0);								// padding
  PUT(heap_listp + WSIZE, PACK(OVERHEAD, 1));	    // Prologue Header
  PUT(heap_listp + 2 * WSIZE, PACK(OVERHEAD, 1));	// Prologue Footer
  PUT(heap_listp + 3 * WSIZE, PACK(0, 1));			// Epilogue Header

  heap_listp += DSIZE;								// Header와 Footer 사이로 이동
  if((extend_heap(CHUNKSIZE / WSIZE)) == NULL)		// CHUNKSIZE 바이트의 Free Block만큼 Empty Heap 확장
    return -1;					                    // WSIZE로 align 되어있지 않으면 에러

  return 0;
}

/*
 * malloc
 */
void *malloc (size_t size) {
	size_t asize;
	size_t extendsize;
	char *bp;

	if (size == 0)
		return NULL;
	if (size <= DSIZE)
		asize = 2 * DSIZE;
	else
		asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

	// Free 공간 찾기
	if ((bp = find_fit(asize)) != NULL) {
		place(bp, asize);
		return bp;
	}

	// 못찾음, 메모리 더 얻어서 재시도
	extendsize = MAX(asize, CHUNKSIZE);
	if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
		return NULL;
	place(bp, asize);
	return bp;
}

/*
 * free
 */
void free (void *ptr) {
    if(!ptr) return;						// 잘못된 Free 요청인 경우 이전 프로시져로 return
	size_t size = GET_SIZE(HDRP(ptr));		// bp의 헤더에서 block size를 읽어옴

	// Header와 Footer의 최하위 1bit (Allocation bit) 만 수정 ~ 실제 데이터는 안지움
	PUT(HDRP(ptr), PACK(size, 0));	// bp의 Header에 block size와 alloc = 0을 저장
	PUT(FTRP(ptr), PACK(size, 0));	// bp의 Footer에 block size와 alloc = 0을 저장

	coalesce(ptr);			// 주위에 빈 블록이 있을 시 병합
}





// coalesce : 병합
void *coalesce(void *bp) {
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));	// 이전 블록의 할당 여부
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));	// 다음 블록의 할당 여부
	size_t size = GET_SIZE(HDRP(bp));	// 현재 블록의 크기

	// Case 1 : 이전, 다음 블록의 최하위 bit가 둘다 1 (할당됨) : 병합 없이 bp return
	if (prev_alloc && next_alloc)
		return bp;

	// Case 2 : 이전 블록의 최하위 bit가 1이고 (할당됨), 다음 블록의 최하위 bit가 0임 (미할당) : 다음 블록과 병합 후 bp return
	else if (prev_alloc && !next_alloc) {
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));	// 다음 블록의 헤더로부터, 크기 읽어옴
		remove_block(NEXT_BLKP(bp));			// Free list에서 다음 블록 삭제
		PUT(HDRP(bp), PACK(size, 0));			// bp의 Header에 block size와 alloc = 0을 저장
		PUT(FTRP(bp), PACK(size, 0));			// bp의 Footer에 block size와 alloc = 0을 저장
	}

	// Case 3 : 이전 블록의 최하위 bit가 0이고 (미할당), 다음 블록의 최하위 bit가 1임 (할당됨) :
	// 이전 블록과 병합 후 새로운 bp return
	else if (!prev_alloc && next_alloc) {
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));		// 이전 블록의 헤더로부터, 크기 읽어옴
		remove_block(PREV_BLKP(bp));				// Free list에서 이전 블록 삭제
		PUT(FTRP(bp), PACK(size, 0));				// 현재 블록의 Footer에 block size와 alloc = 0 저장
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));	// 이전 블록의 Header에 block size와 alloc = 0 저장
		bp = PREV_BLKP(bp);							// 이전 블록을 새로운 블록으로 설정
	}

	// Case 4 : 이전, 다음 블록의 최하위 bit가 둘다 0 (미할당) :
	// 이전, 다음 블록을 모두 병합한 뒤 새로운 bp return
	else {
		size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));	// 전,후,현재 크기 더함
		remove_block(PREV_BLKP(bp));				// Free list에서 이전 블록 삭제
		remove_block(NEXT_BLKP(bp));				// Free list에서 다음 블록 삭제
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));	// 이전 블록의 Header에 block size와 alloc = 0 저장
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));	// 다음 블록의 Footer에 block size와 alloc = 0 저장
		bp = PREV_BLKP(bp);							// 이전 블록을 새로운 블록으로 설정
	}

	return bp;	// 병합된 블록의 주소 bp return
}


// Free list에서 제거
void remove_block(void* bp) {
	if (PREV_FREEP(bp))
		PUT8(NEXT_FREEP(PREV_FREEP(bp)), NEXT_FREEP(bp));	// 이전 블록이 존재 : 이전 블록의 Next가 현재의 Next를 가리키도록 함
	else
		heap_start = NEXT_FREEP(bp);						// 이전 블록이 없음 : free list의 처음이 Next를 가리키도록 함
	PUT8(PREV_FREEP(NEXT_FREEP(bp)), PREV_FREEP(bp));		// 다음 블록의 Prev가 현재 블록의 Prev를 가리키도록 함
}


// extend_heap
void *extend_heap(size_t words)
{
    char *bp;
    unsigned size;

    // Allocate an even number of words to maintain alignment
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

    // Request more memory from heap
    if((long)(bp = mem_sbrk(size)) < 0)
        return NULL;

    // 새로운 Free Block에 Header, Footer, Epilegoue Header 넣기
    PUT(HDRP(bp), PACK(size, 0));			// Header
    PUT(FTRP(bp), PACK(size, 0));			// Footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));	// Epilogue Header

    // 이전 Block이 Free라면 병합
    return coalesce(bp);
}


// First-fit 탐색
void *find_fit(size_t asize)
{
	void *bp;
	for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_FREEP(bp)) {
		// ALLOC 비트가 0이면서, 해당 블록의 사이즈가 요청된 사이즈 이상이어야 함
		if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp))))
			return bp;
	}
	return NULL;	// No Fit
}


// Place : find-fit으로 찾아낸 free block의 Header, Footer alloc 비트를 1로 설정
void place(void *bp, size_t asize)
{
	size_t csize = GET_SIZE(HDRP(bp));

	// 찾아낸 Free Block과 요청된 크기의 차이가 24(최소블록 사이즈) 이상인 경우 : 사용할 만큼만 alloc 1 세팅
	if ((csize - asize) >= (3 * DSIZE)) {
		PUT(HDRP(bp), PACK(asize, 1));			// 블록 할당
		PUT(FTRP(bp), PACK(asize, 1));
		remove_block(bp);						// Free list에서 할당한 블록 제거

		// 남은 Free block으로 이동 후, 병합
		bp = NEXT_BLKP(bp);
		PUT(HDRP(bp), PACK(csize - asize, 0));
		PUT(FTRP(bp), PACK(csize - asize, 0));
		coalesce(bp);
	}

	else {
		PUT(HDRP(bp), PACK(csize, 1));			// 블록 할당
		PUT(FTRP(bp), PACK(csize, 1));
		remove_block(bp);						// Free list에서 할당한 블록 제거
	}
}



/*
 * realloc - you may want to look at mm-naive.c
 */
void *realloc(void *oldptr, size_t size) {
	size_t oldsize;
	void *newptr;

	/* If size == 0 then this is just free, and we return NULL. */
	if (size == 0) {
		free(oldptr);
		return 0;
	}

	/* If oldptr is NULL, then this is just malloc. */
	if (oldptr == NULL) {
		return malloc(size);
	}

	newptr = malloc(size);

	/* If realloc() fails the original block is left untouched  */
	if (!newptr) {
		return 0;
	}

	/* Copy the old data. */
	oldsize = *SIZE_PTR(oldptr);
	if (size < oldsize) oldsize = size;
	memcpy(newptr, oldptr, oldsize);

	/* Free the old block. */
	free(oldptr);

	return newptr;
}

/*
 * calloc - you may want to look at mm-naive.c
 * This function is not tested by mdriver, but it is
 * needed to run the traces.
 */
void *calloc (size_t nmemb, size_t size) {
	size_t bytes = nmemb * size;
	void *newptr;

	newptr = malloc(bytes);
	memset(newptr, 0, bytes);

	return newptr;
}


/*
 * Return whether the pointer is in the heap.
 * May be useful for debugging.
 */
static int in_heap(const void *p) {
    return p < mem_heap_hi() && p >= mem_heap_lo();
}

/*
 * Return whether the pointer is aligned.
 * May be useful for debugging.
 */
static int aligned(const void *p) {
    return (size_t)ALIGN(p) == (size_t)p;
}

/*
 * mm_checkheap
 */
void mm_checkheap(int verbose) {
}
