/* 
 * mm-implicit.c -  Simple allocator based on implicit free lists, 
 *                  first fit placement, and boundary tag coalescing. 
 *
 * Each block has header and footer of the form:
 * 
 *      31                     3  2  1  0 
 *      -----------------------------------
 *     | s  s  s  s  ... s  s  s  0  0  a/f
 *      ----------------------------------- 
 * 
 * where s are the meaningful size bits and a/f is set 
 * iff the block is allocated. The list has the following form:
 *
 * begin                                                          end
 * heap                                                           heap  
 *  -----------------------------------------------------------------   
 * |  pad   | hdr(8:a) | ftr(8:a) | zero or more usr blks | hdr(8:a) |
 *  -----------------------------------------------------------------
 *          |       prologue      |                       | epilogue |
 *          |         block       |                       | block    |
 *
 * The allocated prologue and epilogue blocks are overhead that
 * eliminate edge conditions during coalescing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
  /* Team name */
  "jacksontyler",
  /* First member's full name */
  "Jackson Gothie",
  /* First member's email address */
  "jago6572@colorado.edu",
  /* Second member's full name (leave blank if none) */
  "Tyler Paccione",
  /* Second member's email address (leave blank if none) */
  "tyler.paccione@colorado.edu"
};

/////////////////////////////////////////////////////////////////////////////
// Constants and macros
//
// These correspond to the material in Figure 9.43 of the text
// The macros have been turned into C++ inline functions to
// make debugging code easier.
//
/////////////////////////////////////////////////////////////////////////////
#define WSIZE       4       /* word size (bytes) */  
#define DSIZE       8       /* doubleword size (bytes) */
#define CHUNKSIZE  (1<<12)  /* initial heap size (bytes) */
#define OVERHEAD    8       /* overhead of header and footer (bytes) */

static inline int MAX(int x, int y) {
  return x > y ? x : y;
}
static inline int MIN(int x, int y) {
  return x < y ? x : y;
}

/////////////////////////////////////////////////////////////////////////////
//
// Global Variables
//

static char *heap_listp;  /* pointer to first block */ 
static char *curr_block;



//
// Pack a size and allocated bit into a word
// We mask of the "alloc" field to insure only
// the lower bit is used
//
static inline uint32_t PACK(uint32_t size, int alloc) {
  return ((size) | (alloc & 0x1));
}

//
// Read and write a word at address p
//
static inline uint32_t GET(void *p) { return  *(uint32_t *)p; }
static inline void PUT( void *p, uint32_t val)
{
  *((uint32_t *)p) = val;
}

//
// Read the size and allocated fields from address p
//
static inline uint32_t GET_SIZE( void *p )  { 
  return GET(p) & ~0x7;
}

static inline int GET_ALLOC( void *p  ) {
  return GET(p) & 0x1;
}

//
// Given block ptr bp, compute address of its header and footer
//
static inline void *HDRP(void *bp) {

  return ( (char *)bp) - WSIZE;
}
static inline void *FTRP(void *bp) {
  return ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE);
}

//
// Given block ptr bp, compute address of next and previous blocks
//
static inline void *NEXT_BLKP(void *bp) {
  void *next = ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)));
  
  // Get the heap boundaries
  void *heap_start = heap_listp;
  void *heap_end = mem_sbrk(0);
  
  // Check if the calculated pointer is within heap boundaries
  if (next < heap_start || next >= heap_end) {
    return NULL;  // Out of bounds
  }
  
  return next;
}

static inline void* PREV_BLKP(void *bp){
  void *prev = ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)));
  
  // Get the heap boundaries
  void *heap_start = heap_listp;
  void *heap_end = mem_sbrk(0);
  
  // Check if the calculated pointer is within heap boundaries
  if (prev < heap_start || prev >= heap_end) {
    return NULL;  // Out of bounds
  }
  
  return prev;
}



//
// function prototypes for internal helper routines
//
static void *extend_heap(uint32_t words);
static void place(void *bp, uint32_t asize);
static void *find_fit(uint32_t asize);
static void *coalesce(void *bp);
static void printblock(void *bp); 
static void checkblock(void *bp);

//
// mm_init - Initialize the memory manager 
//
int mm_init(void) 
{
  // Allocate 4 words of memory: 1 for alignment padding, 2 for prologue, 1 for epilogue
    char *mem_start = mem_sbrk(4 * WSIZE);
    if (mem_start == (void *)-1) {
        return -1;
    }

    // Initialize the heap_listp to point to the start of the heap
    heap_listp = mem_start;
    curr_block = heap_listp;

    // Set the alignment padding (first word)
    PUT(heap_listp, 0);

    // Set the prologue header (allocated block of size DSIZE)
    PUT(heap_listp + WSIZE, PACK(0, 1));

    // Set the prologue footer (allocated block of size DSIZE)
    PUT(heap_listp + DSIZE, PACK(0, 1));

    // Set the epilogue header (size 0, allocated)
    PUT(heap_listp + DSIZE + WSIZE, PACK(0, 1));

    // Extend the heap by CHUNKSIZE bytes
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) {
        return -1;
    }

    return 0;
}


//
// extend_heap - Extend heap with free block and return its block pointer
//
static void *extend_heap(uint32_t words) 
{
  void *endp = mem_sbrk(words*4);
  void* newPtr = coalesce(endp);
  return newPtr;
}



//
// Practice problem 9.8
//
// find_fit - Find a fit for a block with asize bytes 
//
static void *find_fit(uint32_t asize)
{

  //
  //check for out of bounds conditions
  //

  for(char* i = curr_block; i< (char*) mem_sbrk(0); i = NEXT_BLKP(i)){
    if(!GET_ALLOC(i) && asize - DSIZE <= GET_SIZE(HDRP(i))){
      return i;
    }
  }
  for(char* i = heap_listp; i<curr_block; i = NEXT_BLKP(i)){
    if(!GET_ALLOC(i) && asize - DSIZE <= GET_SIZE(HDRP(i))){
      return i;
    }
  }
  return NULL; /* no fit */
}

// 
// mm_free - Free a block 
//
void mm_free(void *bp)
{
  //
  // You need to provide this
  //
  int* HDptr = HDRP(bp);
  int* Fptr = FTRP(bp);
  if((*HDptr & 0x1) == 1){
    *HDptr = *HDptr ^ 0x1;
  }
  if((*Fptr & 0x1) ==1){
    *Fptr = *Fptr ^ 0x1;
  }
  coalesce(bp);

}

//
// coalesce - boundary tag coalescing. Return ptr to coalesced block
//
static void *coalesce(void *bp) 
{
  uint32_t prevBlockSize = GET_SIZE(PREV_BLKP(HDRP(bp)));
  uint32_t currBlockSize = GET_SIZE((void*)HDRP(bp));
  uint32_t nextBlockSize = GET_SIZE(NEXT_BLKP(HDRP(bp)));
  uint32_t prevAllocated = GET_ALLOC(HDRP(PREV_BLKP(bp)));
  uint32_t nextAllocated = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
  uint32_t newSize;

  

  
  //write if statement
  if(prevAllocated && nextAllocated){
    //
    // Case 1: No Adjacent Free Blocks (do nothing as they are already freed)
    //
    return bp;
  }
  else if(!prevAllocated){
    //
    // Case 2: Previous Block is Free so merge
    //
    newSize = prevBlockSize + currBlockSize;
    PUT(HDRP(PREV_BLKP(bp)), PACK(newSize, 0));
    PUT(FTRP(PREV_BLKP(bp)), PACK(newSize, 0));
    return PREV_BLKP(bp);
  }
  else if(!nextAllocated){
    //
    // Case 3: Next Block is Free so merge
    //
    newSize = nextBlockSize + currBlockSize;
    PUT(HDRP(bp), PACK(newSize, 0));
    PUT(FTRP(bp), PACK(newSize, 0)); 
    return bp;
  }
  else{
    //
    // Case 4: Previous and Next Block is Free so merge into one big block
    //
    newSize = currBlockSize + nextBlockSize + prevBlockSize;
    PUT(HDRP(PREV_BLKP(bp)), PACK(newSize, 0));
    PUT(FTRP(PREV_BLKP(bp)), PACK(newSize, 0));
    return PREV_BLKP(bp);
  }
}

//
// mm_malloc - Allocate a block with at least size bytes of payload 
//
void *mm_malloc(uint32_t size) 
{
  uint32_t asize = size + DSIZE;

  if(size < 16){
    return NULL;      //confirm request is atleast 16 bytes (minumun block size given in README)
  }
  void* fitFound = find_fit(asize);

  if(fitFound == NULL){
    fitFound = extend_heap(asize/WSIZE);    //no fit found, so extend the heap to be able to fit request
  }
     //now that there IS space, place an allocated block in that new location
  place(fitFound,asize);
  return fitFound;

} 

//
//
// Practice problem 9.9
//
// place - Place block of asize bytes at start of free block bp 
//         and split if remainder would be at least minimum block size
//
static void place(void *bp, uint32_t asize)
{
  uint32_t blockSize = GET_SIZE(HDRP(bp));
  
  if(blockSize-asize >=16){
    PUT(HDRP(bp), PACK(asize,1));
    PUT(FTRP(bp), PACK(asize,1));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(asize-blockSize, 0));
    PUT(FTRP(NEXT_BLKP(bp)), PACK(asize-blockSize, 0));
    coalesce(NEXT_BLKP(bp));
  }
  else{
    PUT(HDRP(bp), PACK(asize,1));
    PUT(FTRP(bp), PACK(asize,1));
  }
  
}


//
// mm_realloc -- implemented for you
//
void *mm_realloc(void *ptr, uint32_t size)
{
  void *newp;
  uint32_t copySize;

  newp = mm_malloc(size);
  if (newp == NULL) {
    printf("ERROR: mm_malloc failed in mm_realloc\n");
    exit(1);
  }
  copySize = GET_SIZE(HDRP(ptr));
  if (size < copySize) {
    copySize = size;
  }
  memcpy(newp, ptr, copySize);
  mm_free(ptr);
  return newp;
}

//
// mm_checkheap - Check the heap for consistency 
//
void mm_checkheap(int verbose) 
{
  //
  // This provided implementation assumes you're using the structure
  // of the sample solution in the text. If not, omit this code
  // and provide your own mm_checkheap
  //
  void *bp = heap_listp;
  
  if (verbose) {
    printf("Heap (%p):\n", heap_listp);
  }

  if ((GET_SIZE(HDRP(heap_listp)) != DSIZE) || !GET_ALLOC(HDRP(heap_listp))) {
	printf("Bad prologue header\n");
  }
  checkblock(heap_listp);

  for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
    if (verbose)  {
      printblock(bp);
    }
    checkblock(bp);
  }
     
  if (verbose) {
    printblock(bp);
  }

  if ((GET_SIZE(HDRP(bp)) != 0) || !(GET_ALLOC(HDRP(bp)))) {
    printf("Bad epilogue header\n");
  }
}

static void printblock(void *bp) 
{
  uint32_t hsize, halloc, fsize, falloc;

  hsize = GET_SIZE(HDRP(bp));
  halloc = GET_ALLOC(HDRP(bp));  
  fsize = GET_SIZE(FTRP(bp));
  falloc = GET_ALLOC(FTRP(bp));  
    
  if (hsize == 0) {
    printf("%p: EOL\n", bp);
    return;
  }

  printf("%p: header: [%d:%c] footer: [%d:%c]\n",
	 bp, 
	 (int) hsize, (halloc ? 'a' : 'f'), 
	 (int) fsize, (falloc ? 'a' : 'f')); 
}

static void checkblock(void *bp) 
{
  if ((uintptr_t)bp % 8) {
    printf("Error: %p is not doubleword aligned\n", bp);
  }
  if (GET(HDRP(bp)) != GET(FTRP(bp))) {
    printf("Error: header does not match footer\n");
  }
}

