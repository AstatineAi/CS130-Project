#ifndef __LIB_KERNEL_HEAP_H
#define __LIB_KERNEL_HEAP_H

/* Heap
   Max heap implemented as binary heap.
   Used in alarm clock and priority scheduling.
   Basically imitating struct list.
*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Maximum heap size 
   The document does not mention whether there is an upper 
   limit on the total number of threads. So maybe 2048 is
   too much.
*/
#define MAX_HEAP_SIZE 2048

typedef void *heap_elem;

/* Compares the value of two heap elements A and B
   Returns true if A is less than B, or
   false if A is greater than or equal to B. */
typedef bool heap_less_func (const heap_elem *a,
                             const heap_elem *b);

/* Heap. */

struct heap
  {
    size_t size;                     /* Heap size. */
    heap_elem elems[MAX_HEAP_SIZE];  /* Heap elements. */
    heap_less_func *less;            /* Heap compare function. */
  };

/* Heap initialization. */
void heap_init (struct heap *, heap_less_func *);

/* Heap elements. */
heap_elem heap_top (struct heap *);

/* Heap push & pop. */
void heap_push (struct heap *, heap_elem);
heap_elem heap_pop (struct heap *);

/* Heap properties. */
size_t heap_size (struct heap *);
bool heap_empty (struct heap *);

/* Auxiliary functions */
void up_heap(struct heap *, size_t);
void down_heap(struct heap *, size_t);

#endif
