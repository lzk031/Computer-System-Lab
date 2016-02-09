/*
 * mm.c
 * this is a simple memory allocator.
 *
 * each free block consists of a header, a footer and several
 * free words between them. I store the pointer of the previous and
 * next free blocks in the first two words in the space between header
 * and footer. To save the space, I just store the lower 32 bits of addresses
 *
 * each allocated block only have header.
 * header's lower 3 digit is used to store allocated fileds indicating
 * if this block and the previous block is allocated. the other digits
 * are used to store the size of the block.
 * I use 14 segregated lists to store and manage free blocks. I store the 
 * address of the first free block in each list at 14 blocks before the 
 * prologue block. if the list is empty, set 0 as default value.
 *
 * I use combination of best fit and first fit. that is to say, I use first
 * fit to find free block for small size request and best fit for large size
 * malloc requests.
 * 
 * Andrew ID : zlyu
 * Name: Zekun Lyu
 *
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

 /* $begin mallocmacros */
 /* these macro definitions are copied from CSAPP website*/
/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */ 
#define DSIZE       8       /* Doubleword size (bytes) */
#define CHUNKSIZE  464//464  /* Extend heap by this amount (bytes) */
#define BASEADDR 0x800000000/* base point of all the address we consider*/

/* segregate size */
#define SEG_SIZE1  16
#define SEG_SIZE2  32
#define SEG_SIZE3  64
#define SEG_SIZE4  128
#define SEG_SIZE5  256
#define SEG_SIZE6  480
#define SEG_SIZE7  960
#define SEG_SIZE8  1920
#define SEG_SIZE9  3840
#define SEG_SIZE10 7680
#define SEG_SIZE11 15360
#define SEG_SIZE12 30720
#define SEG_SIZE13 61440

#define SEG_NUM    14
#define INT_MAX    (1<<30)-1

#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc))

 /* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(p) (((size_t)(p) + (ALIGNMENT-1)) & ~0x7)

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)((unsigned long)p))
#define PUT(p, val)  (*(unsigned int *)((unsigned long)p) = (val))

/* given the block pointer, get and set prev free pointer p */
#define GET_PREV(p)       (*(unsigned int *)((unsigned long)p))
#define SET_PREV(p, val)  (*(unsigned int *)((unsigned long)p) = \
 							(unsigned)(val))

/* given the block pointer, get and set next free pointer p */
#define GET_NEXT(p)       (*(unsigned int *)((char*)(unsigned long)p+WSIZE))
#define SET_NEXT(p, val)  (*(unsigned int *)((char*)(unsigned long)p+WSIZE) = \
 							(unsigned)(val))

/* given offset, return real address. both of them are pointers */
#define OFFSET_REAL(o)  ((unsigned long)(o) + BASEADDR)

/* given the real address, return the offset. both of them are pointers */
#define REAL_OFFSET(r)  ((unsigned int)((unsigned long)(r) - BASEADDR))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)


/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* get allocated fields of prev alloc based on pointer of this block */
#define GET_PREV_ALLOC(p) (GET(HDRP(p)) & 0x2)


/* $end mallocmacros */

/* Function prototypes for internal helper routines */
static inline char *get_seg_listp(size_t size);
static inline void *find_seg_fit(size_t asize, void* free_listp);
static void *extend_heap(size_t words);
static void place(void *ptr, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *ptr);
static inline void add_free_block(char *ptr);
static inline void remove_free_block(char *ptr);
static void check_block(char* ptr);
static int check_list(char* listp, size_t size_lo, size_t size_hi);

/* Global variables */
static char *heap_listp = 0;/* pointer to first block*/

/* pointer to top of free lists, the start of heap*/
static char *seg_list_start = 0;



/*
 * Initialize: return -1 on error, 0 on success.
 * Allocate a 8 bytes block for each segregated list
 * to store the address of the first free block.
 * initialize prologue and epilogue.
 */
int mm_init(void) {
	int i;
	char* seg_listp = NULL;
	seg_list_start = (char*)(unsigned long)BASEADDR;
	/* create a 2 word space for each of the segregated lists*/
	for(i=0; i<SEG_NUM; i++){
		if ((seg_listp = mem_sbrk(2*WSIZE)) == (void *)-1)
			return -1;
		/* set the prev pointer offset as 0 (point to itself)*/
		PUT(seg_listp, 0);
		/* set the next pointer offset as 0 (point to itself)*/
		PUT(seg_listp + (1*WSIZE), 0);
	}
	

	/* create the initial empty heap*/
	if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
		return -1;
    PUT(heap_listp, 0);                          /* Alignment padding */
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); /* Prologue header */ 
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */ 
    PUT(heap_listp + (3*WSIZE), PACK(0, 3));     /* Epilogue header */
    heap_listp += (2*WSIZE);

    /* extend the empty heap with a free block of CHUNKSIZE bytes*/
    if (extend_heap(CHUNKSIZE/WSIZE)==NULL)
    	return -1;
    return 0;
}

/* 
 * extend_heap - Extend heap with free block and return its block pointer
 */
/* $begin extendheap */
static void *extend_heap(size_t words) 
{
    char *bp;
    size_t size;
    char *end = (char*)mem_heap_hi()+1;

    /* Allocate an even number of words to maintain alignment */
    /* if the previous block is not allocated, shrink the size*/
    if(!GET_PREV_ALLOC(end))
    	size = size - (size_t)GET_SIZE(HDRP(PREV_BLKP(end)));
    /* do size alignment*/
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    size = (size_t)MAX(size, DSIZE);

    if ((long)(bp = mem_sbrk(size)) == -1)  
		return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, GET_PREV_ALLOC(bp)));  /* Free block header */ 
    PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */  
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */

    /* add it to free list, minimum size is 4 words*/
    if(size>=2*DSIZE)
    	add_free_block(bp);
    /* Coalesce if the previous block was free */
    return coalesce(bp);
}
/* $end extendheap */

/*
 * malloc - find a suitable free block and return its pointer
 */
void *malloc (size_t size) {

	size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *ptr;      

    /* if the heap has not be initialized*/
    if (heap_listp == 0){
		mm_init();
    }
    /* Ignore spurious requests */
    if (size == 0)
		return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE)
		asize = 2*DSIZE;
    else
		asize = DSIZE * ((size + (WSIZE) + (DSIZE-1)) / DSIZE);

    /* Search the free list for a fit */
    if ((ptr = find_fit(asize)) != NULL) {
		place(ptr, asize);
		return ptr;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);
    if ((ptr = extend_heap(extendsize/WSIZE)) == NULL)  
		return NULL;
    place(ptr, asize);

    // mm_checkheap(250);
    return ptr;
}

/*
 * free: given the pointer, free an allocated block
 */
 /* $begin free */
void free (void *ptr) {
	char *next;
	// printf("free: %lx\n", (long int)ptr);
	/* check invalid input pointer */
	if(!ptr){
		return;
	}
	if(ptr>mem_heap_hi()||ptr<mem_heap_lo()){
		return;
	}


	size_t size = GET_SIZE(HDRP(ptr));

	/* change the allocate fields in the header*/
	PUT(HDRP(ptr), PACK(size, GET_PREV_ALLOC(ptr)));
	/* add footer for this free block */
	PUT(FTRP(ptr), PACK(size, 0));

	/* change the alloc fileds of next block*/
	if(NEXT_BLKP(ptr)!=NULL){
		next = NEXT_BLKP(ptr);
		*(unsigned int*)(HDRP(next)) &= (~0x2);
	}

	add_free_block(ptr);

    coalesce(ptr);
}
/* $end free */

/*
 * coalesce: check if the previous and the 
 * next block are free. Coalesce blocks if
 * necessary. return the pointer of the new
 * free block;
 */
 /* $begin coalesce */
static void *coalesce(void *ptr){
	size_t prev_alloc = GET_PREV_ALLOC(ptr);
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
	size_t size = GET_SIZE(HDRP(ptr));

	if(prev_alloc && next_alloc){   /* neither of them are free */
		return ptr;
	}

	/* remove ptr from free list, update it and readd to free list*/
	remove_free_block(ptr);

	if(prev_alloc && !next_alloc){  /* next is free*/
		/* remove next block from free list*/
		remove_free_block(NEXT_BLKP(ptr));
		size += GET_SIZE(HDRP(NEXT_BLKP(ptr)));
		PUT(HDRP(ptr), PACK(size, 2));
		PUT(FTRP(ptr), PACK(size, 0));

	}else if(!prev_alloc && next_alloc){ /* prev is free*/
		remove_free_block(PREV_BLKP(ptr));
		size += GET_SIZE(HDRP(PREV_BLKP(ptr)));
		PUT(FTRP(ptr), PACK(size, 0));
		PUT(HDRP(PREV_BLKP(ptr)), PACK(size, GET_PREV_ALLOC(PREV_BLKP(ptr))));
		ptr = PREV_BLKP(ptr);

	}else{                               /* both are free*/
		remove_free_block(PREV_BLKP(ptr));
		remove_free_block(NEXT_BLKP(ptr));
		size += GET_SIZE(HDRP(NEXT_BLKP(ptr))) +
			GET_SIZE(HDRP(PREV_BLKP(ptr)));
		PUT(HDRP(PREV_BLKP(ptr)), PACK(size, GET_PREV_ALLOC(PREV_BLKP(ptr))));
		PUT(FTRP(NEXT_BLKP(ptr)), PACK(size, 0));
		ptr = PREV_BLKP(ptr);
	}

	add_free_block(ptr);

	return ptr;
}
/* $end coalesce */

/*
 * place - make a block allocated
 * set header and remove it from 
 * free list
 */
static void place(void *ptr, size_t asize){
	size_t csize = GET_SIZE(HDRP(ptr));
	size_t prev_alloc = GET_PREV_ALLOC(ptr);
	remove_free_block(ptr);   

    if ((csize - asize) >= (2*DSIZE)) { 
    	/* if the free block is much bigger than requested size */

    	/* chage header, no footer for allocated block */
		PUT(HDRP(ptr), PACK(asize, prev_alloc|1));

		/* create a new block */
		ptr = NEXT_BLKP(ptr);
		PUT(HDRP(ptr), PACK(csize-asize, 2));
		PUT(FTRP(ptr), PACK(csize-asize, 0));
		/* add this newly generated block to free list*/
		add_free_block(ptr);
    }
    else { 
    	/* change header, no footer for allocated block */
		PUT(HDRP(ptr), PACK(csize, prev_alloc|1));
		/* update header of next block*/
		ptr = NEXT_BLKP(ptr);
		if(ptr!=NULL){
			*(unsigned int*)(HDRP(ptr)) |= (0x2);
		}
    }
}

/*
 * find_seg_fit - take in the request size and a list
 * start pointer, search for fit free block in this list.
 * retur null if no fit block founded.
 */
static inline void *find_seg_fit(size_t asize, void* free_listp){
	unsigned long first_free = *(unsigned long*)(free_listp);
	size_t small_size = 0; /* smallest suitable block, 0 indicate no suitable*/
	size_t block_size;
	void *result = NULL; /* store the temp most suitable block*/

	if(first_free==0)
		return NULL;
	void *bp;
	bp=(char*)(first_free);
	if(asize < SEG_SIZE7){ /* if asize is small, first fit*/
		if (asize <= GET_SIZE(HDRP(bp))) {
		    return bp;
		}
		while(GET_NEXT(bp)!=0){
			bp=(char*)(OFFSET_REAL(GET_NEXT(bp)));
			if (asize <= GET_SIZE(HDRP(bp))) {
				// printf("bp: %lx\n", (unsigned long)bp);
			    return bp;
			}
		}
    	return NULL;
	}else{ /* if asize is large, best fit to improve utilization*/
		block_size = (size_t)GET_SIZE(HDRP(bp));
		if (asize == block_size)
		    return bp;
		if(asize < block_size){
			small_size = block_size;
			result = bp;
		}
		while(GET_NEXT(bp)!=0){
			bp=(char*)(OFFSET_REAL(GET_NEXT(bp)));
			block_size = (size_t)GET_SIZE(HDRP(bp));
			if (asize == block_size)
				return bp;
			if(asize < block_size){
				if(result==NULL||block_size<small_size){
					small_size = block_size;
					result = bp;
				}
			}
		}
		return result;
	}
}

/*
 * find_fit - take in the size of request,
 * find a suitable free block and return its pointer
 */
static void *find_fit(size_t asize){
	void* fit;
	char* free_listp = get_seg_listp(asize);
	/* if there is fit block in the most suitable list, return the fit*/
	if((fit=find_seg_fit(asize, free_listp))!=NULL)
		return fit;
	/* if there is no fit block, look for at larger seg lists*/
	while(free_listp < (char*)seg_list_start+(SEG_NUM-1)*DSIZE){
		free_listp = (char*)free_listp + DSIZE;
		if((fit=find_seg_fit(asize, free_listp))!=NULL)
			return fit;
	}
	return find_seg_fit(asize, free_listp);
	
}


/*
 * realloc - take in an ald pointer, copy part (or total amount) of the data 
 * it points to, create a new memory block and return
 */
void *realloc(void *oldptr, size_t size) {
	size_t oldsize;
    void *newptr;

	/* if the pointer is null, allocate a new block */
    if(oldptr == NULL) {
		return malloc(size);
    }

    /* If size == 0, free this block */
    if(size == 0) {
		free(oldptr);
		return NULL;
    }

    
    /* create a new allocated block */
    newptr = malloc(size);

    /* If realloc() fails the original block is left untouched  */
    if(!newptr) {
		return NULL;
    }

    /* Copy the old data. */
    oldsize = GET_SIZE(HDRP(oldptr));
    if(size < oldsize) 
    	oldsize = size;
    memcpy(newptr, oldptr, oldsize);

    /* Free the old block. */
    free(oldptr);

    return newptr;
}

/*
 * calloc -  Allocates memory for an array of nmemb elements of 
 * size bytes each and returns a pointer to the allocated memory
 */
void *calloc (size_t nmemb, size_t size) {
	size_t bytes = nmemb * size;
 	void *newptr;

 	newptr = malloc(bytes);
 	/* initialize as 0 */
	memset(newptr, 0, bytes);

	return newptr;
}

/***********************************************
 * Helper routines that manipulate the free list
 **********************************************/

/*
 * given a pointer of a free block, calculate
 * it's size and find the pointer of relevant
 * seglist
 */
 static inline char* get_seg_listp(size_t size){
 	int seg_index;

	/* check which seglist ptr belongs to*/
 	if(size < SEG_SIZE1){
 		seg_index = 0;
 	}else if(size < SEG_SIZE2){
 		seg_index = 1;
 	}else if(size < SEG_SIZE3){
 		seg_index = 2;
 	}else if(size < SEG_SIZE4){
 		seg_index = 3;
 	}else if(size < SEG_SIZE5){
 		seg_index = 4;
 	}else if(size < SEG_SIZE6){
 		seg_index = 5;
 	}else if(size < SEG_SIZE7){
 		seg_index = 6;
 	}else if(size < SEG_SIZE8){
 		seg_index = 7;
 	}else if(size < SEG_SIZE9){
 		seg_index = 8;
 	}else if(size < SEG_SIZE10){
 		seg_index = 9;
 	}else if(size < SEG_SIZE11){
 		seg_index = 10;
 	}else if(size < SEG_SIZE12){
 		seg_index = 11;
 	}else if(size < SEG_SIZE13){
 		seg_index = 12;
 	}else{
 		seg_index = 13;
 	}

 	return (char*) seg_list_start + seg_index*DSIZE;

 }


/*
 * add the pointer of a free block to the top
 * of the list. in this case, free list act as a stack
 * listp is the pointer to top of the list.
 */
static inline void add_free_block(char *ptr){
	char* free_listp = get_seg_listp(GET_SIZE(HDRP(ptr)));
	unsigned long first_free = *(unsigned long*)(free_listp);
	if(first_free==0){    /* list is empty*/
		/* set first free block*/
		(*(unsigned long *)(free_listp)) = (size_t) ptr;
		/* set prev as 0 which indicate it is the first free block */
		SET_PREV(ptr,0);
		/* set next as 0 which indicats it its the last free block */
		SET_NEXT(ptr, 0);
	}else{ /* the free list is not empty*/
		SET_PREV(ptr, 0);
		SET_NEXT(ptr,(unsigned long)first_free - BASEADDR);

		/* update first free block stored in free_listp*/
		(*(unsigned long *)(free_listp)) = (size_t) ptr;

		/* update prev of the next block*/
		SET_PREV((char*)first_free,(unsigned long)ptr - BASEADDR);
	}
}

/*
 * remove the pointer of a free block
 */
static inline void remove_free_block(char *ptr){
	char* free_listp = get_seg_listp(GET_SIZE(HDRP(ptr)));
	unsigned int prev = GET_PREV(ptr);
	unsigned int next = GET_NEXT(ptr);
	if(!prev){    /* if ptr is the first free block*/
		if(!next){ /* if ptr is the only block in the list*/
			*(unsigned long*)(free_listp)=(size_t)(0);
		}else{
			*(unsigned long*)(free_listp)=(unsigned long)next+BASEADDR;
			SET_PREV(OFFSET_REAL(next), 0);
		}
	}else{ /* ptr is not the first block*/
		if(!next){ /* if ptr is the end of the list*/
			SET_NEXT(OFFSET_REAL(prev), 0);
		}else{
			SET_PREV(OFFSET_REAL(next), prev);
			SET_NEXT(OFFSET_REAL(prev), next);
		}
	}

}


/*
 * Return whether the pointer is in the heap.
 * May be useful for debugging.
 */
static int in_heap(const void *p) {
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}

/*
 * Return whether the pointer is aligned.
 * May be useful for debugging.
 */
static int aligned(const void *p) {
    return (size_t)ALIGN(p) == (size_t)p;
}

/*
 * mm_checkheap - check heap correctness
 * it will check all the invariants of 
 * my heap representing data structure
 */
void mm_checkheap(int lineno) {
	char *ptr;
	char *next;
	lineno = lineno;
	char *listp;
	int i;
	int count;
	int free_count;
	size_t sizes[SEG_NUM+1]={0,16,32,64,128,256,480,960,1920,3840,7680,15360,
								30720,61440,INT_MAX};


	/* Check prologue blocks. */
	if (!aligned(heap_listp))
		printf("prologue alignment error\n");
	if (GET_SIZE(heap_listp)!=DSIZE)
		printf("prologue header size error\n");

	if (GET_ALLOC(heap_listp)!=1)
		printf("prologue header allocate error\n");

	if (GET_SIZE(heap_listp-WSIZE)!=DSIZE)
		printf("prologue header size error\n");

	if (GET_ALLOC(heap_listp-WSIZE)!=1)
		printf("prologue header allocate error\n");

	/* check epilogue blocks. */
	if (GET_SIZE(mem_heap_hi()-3)!=0)
		printf("epilogue size error\n");

	if (GET_ALLOC(mem_heap_hi()-3)!=1)
		printf("epilogue allocate error\n");

	/* check heap from the first block to last block*/
	count = 0;
	for (ptr = heap_listp; GET_SIZE(HDRP(ptr))>0; ptr = NEXT_BLKP(ptr)){
		check_block(ptr);
		if(!GET_ALLOC(HDRP(ptr)))
			count++;
		/* check previous allocate bit consistency */
		if(GET_ALLOC(HDRP(ptr)) != (GET_PREV_ALLOC(NEXT_BLKP(ptr))/2))
			printf("at block %lx, allocate flag error of next block\n", 
				(unsigned long)ptr );
		next = NEXT_BLKP(ptr);

		/* check coalescing */
		if((GET_ALLOC(HDRP(ptr))|GET_ALLOC(HDRP(next)))==0)
			printf("two consecutive free blocks exists\n");
	}


	/* check segregated lists lists*/
	free_count = 0;
	for(i = 0; i<SEG_NUM; i++){
		listp = (char*)seg_list_start+i*DSIZE;
		free_count += check_list(listp,sizes[i],sizes[i+1]);
	}
	/* check if all the free blocks is put into one of the free lists */
	if(free_count!=count)
		printf("free blocks number not match\n");
}

/*
 * check_list - check the following things
 * 1. if the free block is in heap and aligned correctly
 * 2. if a block falls in the right list
 * 3. check next/previous pointer consistency
 * 
 */
static int check_list(char* listp, size_t size_lo, size_t size_hi){
	unsigned long first_free = *(unsigned long*)(listp);
	size_t cur_size;
	int count;
	if(first_free==0)
		return 0;
	char *bp;
	char *next;
	bp = (char*)(first_free);

	count = 0;

	while(GET_NEXT(bp)!=0){
		count++;
		/* check alignment correctness */
		if(!aligned(bp))
			printf("not aligned correctly\n");
		/* all free block pointers points into heap*/
		if(!in_heap(bp))
			printf("block out of heap\n");
		cur_size = GET_SIZE(HDRP(bp));
		if(cur_size<size_lo||cur_size>=size_hi)
			printf("free block falls in wrong segregated list\n");
		next=(char*)(OFFSET_REAL(GET_NEXT(bp)));
		if(next==0)
			continue;
		if(GET_PREV(next)!=REAL_OFFSET(bp))
			printf("next/previous pointers are not consistent\n");

		bp=(char*)(OFFSET_REAL(GET_NEXT(bp)));

	}

	return count+1;
}

/*
 * check_block - check the following things
 * 1. if the block is in heap
 * 2. if the header and footer is consistent
 * 3. if the block is aligned correctly
 * 
 */
static void check_block(char* ptr){
	size_t head_size = GET_SIZE(HDRP(ptr));
	size_t foot_size;

	/* check in heap boundary*/
	if(!in_heap(ptr))
		printf("block %lx not in heap \n", (unsigned long)ptr);
	/* check alignment*/
	if(!aligned(ptr))
		printf("the block %lx is not correctly aligned\n", (unsigned long) ptr);

	/* check minimum size*/
	if(head_size<2*DSIZE){
		if(ptr!=heap_listp)
			printf("wrong size of block %lx\n", (unsigned long) ptr);
	}

	if(!GET_ALLOC(HDRP(ptr))){
		foot_size = GET_SIZE(FTRP(ptr));
		/* check footer header match */
		if(foot_size!=head_size)
			printf("the head_size and foot size is not the same\n");
	}

}


