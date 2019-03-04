/*
 * Simple allocator based on an explicit free list,
 * first fit placement, and boundary tag coalescing.
 *
 * Blocks are aligned to double-word boundaries which
 * yields 16-byte aligned blocks.
 *
 * U the standardtype uintptr_t to define unsigned integers
 * that are the same size as a pointer.
 *
 * A best fit placement has been implemented but
 * commented out as it shows a Seg fault for one of the
 * test cases.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "memlib.h"
#include "mm.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Team A",
    /* First member's full name */
    "Nikhilesh Sharma",
    /* First member's email address */
    "",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* Basic constants and macros: */
#define WSIZE      8 			  /* Word and header/footer size (bytes) */
#define DSIZE      (2 * WSIZE)    /* Doubleword size (bytes) */
#define CHUNKSIZE  (1 << 12)      /* Extend heap by this amount (bytes) */
#define OVERHEAD   16

#define MAX(x, y)  ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into a word. */
#define PACK(size, alloc)  ((size) | (alloc))

/* Read and write a word at address p. */
#define GET(p)       (*(uintptr_t *)(p))
#define PUT(p, val)  (*(uintptr_t *)(p) = (val))

/* Read the size and allocated fields from address p. */
#define GET_SIZE(p)   (GET(p) & ~(DSIZE - 1))
#define GET_ALLOC(p)  (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer. */
#define HDRP(bp)  ((char *)(bp) - WSIZE)
#define FTRP(bp)  ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks. */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Function prototypes for internal helper routines: */
static void *coalesce(void *bp);
static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
//static void *best_fit(size_t asize);
static void place(void *bp, size_t asize);

/* Function prototypes for heap consistency check subroutines */
static void checkblock(void *bp);
static void printblock(void *bp);

/*Pointer to get NEXT and PREVIOUS pointer of free_list*/
#define NEXT_PTR(p)  (*(char **)(p + WSIZE))
#define PREV_PTR(p)  (*(char **)(p))

/* Prototypes for adding and deleting free memory blocks in free_list */
static void add_block_to_list(void* ptr);
static void delete_block_from_list(void* ptr);

/* Global variables: */
static char *heap_listp = 0; /* Pointer to first block in heap */
static char *free_listp = 0;  /* Pointer to start of free list */


/*
 *   Initializes the memory manager to allocate initial
 *   heap, etc. Returns 0 if the memory manager was
 *   successfully initialized and -1 otherwise.
 */
int mm_init(void) {

	/* Create the initial empty heap. */
	if ((heap_listp = mem_sbrk(8 * WSIZE)) == NULL)
		return -1;

	PUT(heap_listp, 0);                            /* Alignment padding */
	PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); /* Prologue header */
	PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); /* Prologue footer */
	PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     /* Epilogue header */
	heap_listp += DSIZE;

	/* Point free_listp to start of free memory in heap */
	free_listp = heap_listp;

	/* Extend the empty heap with a free block of CHUNKSIZE bytes. */
	if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
		return (-1);

	return 0;
}

/*
 *   Allocates a block with at least "size" bytes. Returns a
 *	 16 byte aligned pointer to the allocated block if the
 * 	 allocation was successful and NULL otherwise.
 */
void *mm_malloc(size_t size) {

	size_t asize;      		/* Adjusted block size */
	size_t extendsize; 		/* Amount to extend heap if a fit is not found */
	char *bp;

	/* Ignore spurious requests. */
	if (size <= 0)
		return (NULL);

	/* Adjust block size to include overhead and alignment reqs. */
	if (size <= DSIZE)
		asize = DSIZE + OVERHEAD;
	else
		asize = DSIZE * ((size + (OVERHEAD) + (DSIZE - 1)) / DSIZE);

	/* Search the free list for a fit. */
	if ((bp = find_fit(asize)) != NULL) {
		place(bp, asize);
		return (bp);
	}

	/* No fit found. Get more memory and place the block. */
	extendsize = MAX(asize, CHUNKSIZE);
	if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
		return NULL;

	place(bp, asize);
	return (bp);
}

/*
 *   Frees a block pointed to by bp.
 *	 Returns nothing.
 *	 Only guaranteed to work when ptr was
 * 	 returned by an earlier call to mm_malloc
 * 	 or mm_realloc and hasn't been freed yet.
 */
void mm_free(void *bp) {

	size_t size;

	/* Ignore spurious requests. */
	if (bp == NULL)
		return;

	/* Free and coalesce the block. */
	size = GET_SIZE(HDRP(bp));

	/* Clear allocated bits in header and footer */
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));

	/*Coalesce with neighboring blocks, if possible */
	coalesce(bp);
}

/*
 *   Reallocates the block "ptr" to a block with at least "size" bytes of
 *   payload with the following constraints:
 *		1.  If "size" is zero, realloc frees the block "ptr" and returns NULL.
 *		2.  If the block "ptr" is already a block with at least "size" bytes
 *			 of payload, then "ptr" may optionally be returned.
 *   	3.  Otherwise, a new block is allocated and the contents of the old
 * 			block "ptr" are copied to that new block.
 *
 *	Returns the address of this new block if the allocation was successful
 * 		and NULL otherwise.
 */
void *mm_realloc(void *ptr, size_t size) {
	size_t oldsize,newsize;
	void *newptr;

	/* If size is negative, return NULL */
	if((int)size < 0)
    	return NULL;

	/* If size == 0 then this is just free, call mm_free and return NULL. */
	if (size == 0) {
		mm_free(ptr);
		return (NULL);
	}

	/* If oldptr is NULL, simply return malloc. */
	if (ptr == NULL)
		return mm_malloc(size);

	/* Original block size */
	oldsize = GET_SIZE(HDRP(ptr));

	/* Adjusted size with header and footer */
	newsize = size + (2 * WSIZE);

	/* Copy the old data. */
	/*If the size needs to be decreased, return the same pointer */
	if (newsize <= oldsize)
		return ptr;

	else{

		/* check if next block is allocated */
		size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));

		/* Next block size */
		size_t next_blk_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));

		/* Total free size of current and next block */
		size_t total_free_size = oldsize + next_blk_size;

		/* Combine current and next block if total_free_size is
		 * greater then or equal to new size
		 */
		if(!next_alloc && total_free_size >= newsize){
			/* Remove block from free list */
			delete_block_from_list(NEXT_BLKP(ptr));

			/* Set allocated bit in header and footer */
			PUT(HDRP(ptr), PACK(total_free_size, 1));
			PUT(FTRP(ptr), PACK(total_free_size, 1));
			return ptr;
		}

		/* Find newsize block in free_list and copy old data to
		 * new address
		 */
		else{
			newptr = mm_malloc(newsize);

			/* If realloc fails, leave the block as it is */
			if (newptr == NULL)
				return NULL;

			/* Place new block in memory */
			place(newptr, newsize);

			/* Copy old data to new address */
			memcpy(newptr, ptr, oldsize);

			/* Free old memory */
			mm_free(ptr);
			return newptr;
		}
	}
}

/*
 *   Check of the heap for consistency.
 *	 	1. If all blocks in list are free.
 * 		2. Prologue consistency
 *		3. Epilogue consistency
 *		4. Block alignment
 *		5. Header and Footer alignment
 */
int mm_check(int verbose)
{
	void*bp = free_listp;
        while (NEXT_PTR(bp)!= NULL) {

            /* Checks if blocks in free_list are actually free */
            if (GET_ALLOC(HDRP(bp)) == 1 || GET_ALLOC(FTRP(bp)) == 1){
                    printf("Allocated block in free list\n");
                    return -1;
            }
            bp  = NEXT_PTR(bp);
        }

    /* Print heap if verbose is set. */
    if (verbose)
		printf("Heap (%p):\n", heap_listp);

	/* Check prologue consistency. */
	if (GET_SIZE(HDRP(heap_listp)) != DSIZE || !GET_ALLOC(HDRP(heap_listp))){
		printf("Bad prologue header\n");
		return -1;
	}

	checkblock(heap_listp);

	/* Check blocks in heap for consistency and print
	 * if verbose is set.
	 */
	for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
		if (verbose)
			printblock(bp);

		checkblock(bp);
	}

	if (verbose)
		printblock(bp);

	/* Check Epilogue consistency. */
	if (GET_SIZE(HDRP(bp)) != 0 || !GET_ALLOC(HDRP(bp))) {
		printf("Bad epilogue header\n");
		return -1;
	}

	return 1;
}

/* Internal helper routines */

/*
 *   Perform boundary tag coalescing. Returns the address of the
 *   coalesced block.
 */
static void *coalesce(void *bp) {

	/*  prev_alloc will be true if previous block is allocated or its size is zero */
	bool prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))) ||
								PREV_BLKP(bp) == bp ;	  /* Front of free list */
	bool next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));

	size_t size = GET_SIZE(HDRP(bp));

	/* Case 1: Both prev and next blocks are occupied */
	if (prev_alloc && next_alloc) {
		/* add current block to free_list */
		add_block_to_list(bp);
		return bp;
	}

	/* Case 2: Prev is allocated and next is free */
	else if (prev_alloc && !next_alloc) {
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));

		/* Delete next free block from free_list */
		delete_block_from_list(NEXT_BLKP(bp));

		/* Update size and allocated bit of header and footer */
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}

	/* Case 3: Prev is free and next is allocated */
	else if (!prev_alloc && next_alloc) {
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));

		/* Delete previous free block from free_list */
		delete_block_from_list(PREV_BLKP(bp));

		/* Update size and allocation bit of header and footer */
		PUT(FTRP(bp), PACK(size, 0));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}

	/* Case 4: Both prev and next are free */
	else {
		size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));

		/* Delete both free blocks from free_list */
		delete_block_from_list(PREV_BLKP(bp));
		delete_block_from_list(NEXT_BLKP(bp));

		/* Update size and allocation bit of header and footer */
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}

	/* add newly coalesced block to free_list */
	add_block_to_list(bp);

	return bp;
}

/*
 *   Extend the heap with a free block and return that block's address.
 */
static void *extend_heap(size_t words) {
	void *bp;
	size_t size;

	/* Allocate an even number of words to maintain alignment. */
	size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

	if ((bp = mem_sbrk(size)) == (void *)-1)
		return (NULL);

	/* Initialize free block header/footer and the epilogue header. */
	PUT(HDRP(bp), PACK(size, 0));         /* Free block header */
	PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */

	/* Coalesce if the previous block was free. */
	return coalesce(bp);
}

/*
 *   Find a fit for a block with "asize" bytes.
 *	 Returns that block's address or NULL if no suitable block was found.
 */
static void *find_fit(size_t asize) {
	void *bp;

	/* First fit. */
	for (bp = free_listp; GET_ALLOC(HDRP(bp)) == 0; bp = NEXT_PTR(bp)) {
		/* Block of required size is found */
		if (asize <= GET_SIZE(HDRP(bp)))
			return bp;
	}

	/* No fit. */
	return NULL;
}

/*
 * Place block of asize bytes at start of free block bp
 * and split if remainder would be at least minimum block size
 */
static void place(void *bp, size_t asize) {
	size_t csize = GET_SIZE(HDRP(bp));

	if ((csize - asize) >= (2 * DSIZE)) {
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));

		/* Delete free block from free_list */
		delete_block_from_list(bp);

		bp = NEXT_BLKP(bp);

		/* Split the block */
		PUT(HDRP(bp), PACK(csize - asize, 0));
		PUT(FTRP(bp), PACK(csize - asize, 0));

		coalesce(bp);
	} else {
		/* Allocate and update the allocated bit */
		PUT(HDRP(bp), PACK(csize, 1));
		PUT(FTRP(bp), PACK(csize, 1));

		/* Delete block from free_list */
		delete_block_from_list(bp);
	}
}

/*
 *   Perform a minimal check on the block bp.
 */
static void checkblock(void *bp) {

	if ((uintptr_t)bp % OVERHEAD)
		printf("Error: %p is not doubleword aligned\n", bp);
	if (GET(HDRP(bp)) != GET(FTRP(bp)))
		printf("Error: header does not match footer\n");
}


/* Print the block pointed to by bp */
static void printblock(void *bp) {
	bool halloc, falloc;
	size_t hsize, fsize;
	int verbose = 0;
	int res;

	res = mm_check(verbose);

	/* Size and Allocation information. */
	hsize = GET_SIZE(HDRP(bp));
	halloc = GET_ALLOC(HDRP(bp));
	fsize = GET_SIZE(FTRP(bp));
	falloc = GET_ALLOC(FTRP(bp));

	/* Print block information. */
	if (res){
		if (hsize == 0) {
			printf("%p: end of heap\n", bp);
			return;
		}

		printf("%p: header: [%zu:%c] footer: [%zu:%c]\n", bp,
	    	hsize, (halloc ? 'a' : 'f'),
	    	fsize, (falloc ? 'a' : 'f'));
	}
}


/* Add a free block pointed by ptr to the free list */
static void add_block_to_list(void* ptr) {

	NEXT_PTR(ptr) = free_listp;
	PREV_PTR(free_listp) = ptr;
	PREV_PTR(ptr) = NULL;
	free_listp = ptr;
}

/* Delete a free block from the free list */
static void delete_block_from_list(void* ptr) {

	/* If this is the first pointer of free list */
	if(PREV_PTR(ptr) == NULL)
		free_listp = NEXT_PTR(ptr);

	else 	/* Update pointer links */
		NEXT_PTR(PREV_PTR(ptr)) = NEXT_PTR(ptr);
	PREV_PTR(NEXT_PTR(ptr)) = PREV_PTR(ptr);
}


/*
static void *best_fit(size_t asize) {

	void *bp;
	int flag = 0;
	int min;

	/* Loop through the free list
	for(bp = free_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
		/* If block is free and required size < free block size
		if (!GET_ALLOC(HDRP(bp)) && asize <= GET_SIZE(HDRP(bp))) {
			/* Initialize min with size of first block
			if(flag == 0) {
				min = GET_SIZE(HDRP(bp));
				free_listp = bp;
				flag = 1;
			}

			else {
				/* Find next closest block to required size
				if(GET_SIZE(HDRP(bp)) < min) {
					min = GET_SIZE(HDRP(bp));
					free_listp = bp;
				}
			}
		}
	}

	if(flag == 1)
		return free_listp;

	return NULL;
}
*/
