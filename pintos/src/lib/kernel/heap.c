#include "heap.h"
#include "../debug.h"
#include <stdio.h>

static void
swap(heap_elem *a, heap_elem *b)
{
  heap_elem t = *a;
  *a = *b;
  *b = t;
}

/* Initializes HEAP as an empty heap. */
void 
heap_init (struct heap *heap, heap_less_func *less)
{
  ASSERT(heap != NULL);
  heap->size = 0;
  heap->less = less;
}

heap_elem
heap_top (struct heap *heap)
{
  ASSERT(heap != NULL && !heap_empty(heap));
  return heap->elems[1];
}

/* Push elem into heap. */
void heap_push (struct heap *heap, heap_elem elem)
{
  ASSERT(heap->size + 1 <= MAX_HEAP_SIZE);
  heap->elems[++heap->size] = elem;
  up_heap(heap, heap->size);
}

/* Pop top element from heap. */
heap_elem
heap_pop (struct heap* heap) {
  ASSERT(!heap_empty(heap));
  heap_elem top = heap_top(heap);

  swap(&heap->elems[1], &heap->elems[heap->size]);
  heap->size--;
  down_heap(heap, (size_t)1);

  return top;
}

size_t
heap_size (struct heap *heap)
{
  return heap->size;
}

bool
heap_empty (struct heap *heap)
{
  return heap->size == 0;
}

/* up_heap, down_heap
   Auxiliary functions restoring the heap property
*/
void
up_heap (struct heap *heap, size_t index)
{
  for (; index > 1; index >>= 1)
    {
      if (heap->less(heap->elems[index >> 1], heap->elems[index]))
        swap(&heap->elems[index >> 1], &heap->elems[index]);
      else
        break;
    }
}

void
down_heap(struct heap *heap, size_t index)
{
  for (size_t ch; (index << 1) <= heap->size; index = ch)
    {
      ch = index << 1;
      ch += (ch < heap->size && heap->less(heap->elems[ch], 
                                           heap->elems[ch | 1]));
      if (heap->less(heap->elems[index], heap->elems[ch]))
        swap(&heap->elems[index], &heap->elems[ch]);
      else
        break;
    }
}
