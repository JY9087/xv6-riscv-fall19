// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include <assert.h>

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  char *page_start;
  uint8 *rc;
} kmem;



void
kinit()
{
  initlock(&kmem.lock, "kmem");
  //assert(PHYSTOP % PGSIZE == 0);
  //reference count数组
  kmem.rc = (uint8 *)end;
  //分配rc数组的内存
  //rc数组相当于元信息
  //为每一页(页大小4K=2^12)分配一个reference count
  uint64 rc_bytes = ((PHYSTOP - PGROUNDUP((uint64)end)) >> 12) * sizeof(uint8);
  printf("kalloc supports %d 4K pages\n", rc_bytes / sizeof(uint8));
  //实际数据从元信息之后开始
  kmem.page_start = (char *)(end + rc_bytes);
  freerange(kmem.page_start, (void *)PHYSTOP);
}


// pa_start: kernel代码数据映射之后的地址
// pa_end: KERNBASE+128M后的地址PHYSTOP
void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}



// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < kmem.page_start || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);
  
  r = (struct run*)pa;

  uint64 idx = ((uint64)pa - (uint64)kmem.page_start) >> 12;
  //rc直接置零，加入到空闲列表
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  kmem.rc[idx] = 0;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void * kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
    //reference count 置1
    uint64 idx = ((uint64)r - (uint64)kmem.page_start) >> 12;
    kmem.rc[idx] = 1;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

//copy on write

uint8
kborrow(void *pa) {
  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < kmem.page_start || (uint64)pa >= PHYSTOP)
    panic("kborrow");
  uint8 r;
  uint64 idx = ((uint64)pa - (uint64)kmem.page_start) >> 12;
  acquire(&kmem.lock);
  //有人(fork)引用了。reference count ++
  kmem.rc[idx] += 1;
  r = kmem.rc[idx];
  release(&kmem.lock);

  return r;
}

void
kdrop(void *pa) {
  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < kmem.page_start || (uint64)pa >= PHYSTOP)
    panic("kdrop");
  int do_free = 0;
  uint64 idx = ((uint64)pa - (uint64)kmem.page_start) >> 12;

  acquire(&kmem.lock);
  //一个引用解除了
  kmem.rc[idx] -= 1;
  //physical page is freed when the last PTE reference to it goes away
  if (kmem.rc[idx] == 0)
    do_free = 1;
  release(&kmem.lock);

  if (do_free)
    kfree(pa);
}