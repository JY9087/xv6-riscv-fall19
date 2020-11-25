// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include <stddef.h>

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem{
  struct spinlock lock;
  struct run *freelist;
};

struct kmem kmems[NCPU];

void
kinit()
{
    int i;
    for(i=0;i<NCPU;i++){
        initlock(&(kmems[i].lock), "kmem");
        kmems[i].freelist = NULL;
    }
    freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
    char *p;
    //PGROUNDUP宏确保内存空间4K对齐
    p = (char*)PGROUNDUP((uint64)pa_start);
    //PGSIZE宏是页面大小
    for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
        kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)


void
kfree(void *pa)
{
    push_off();
    int i = cpuid();
    pop_off();

    struct run *r;

    if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    //pa是指向页面开头的指针
    r = (struct run*)pa;

    //上锁
    acquire(&kmems[i].lock);
    //链表。继承当前值，指向下一个
    r->next = kmems[i].freelist;
    kmems[i].freelist = r;
    //解锁
    release(&kmems[i].lock);

}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
    push_off();
    int i = cpuid();
    pop_off();

    struct run *r;

    //调用函数。计数器。确保互斥使用
    acquire(&kmems[i].lock);

    r = kmems[i].freelist;
    //r != Null
    if(r){
        kmems[i].freelist = r->next;
        release(&kmems[i].lock);
    }
    //在steal对同一个锁进行调用?
    //没有空间就去找其他的Steal
    //会引起额外锁竞争
    else{
        //立刻解锁
        release(&kmems[i].lock);
        int j;
        //使用mod
        for(j=i+1;j<i+NCPU;j++){
            acquire(&kmems[j%NCPU].lock);

            r = kmems[j%NCPU].freelist;
            //r != Null
            if(r){
                kmems[j%NCPU].freelist = r->next;
                release(&kmems[j%NCPU].lock);
                goto steal;
            }
            release(&kmems[j%NCPU].lock);
        }
    }

    steal:

    //解锁后填充垃圾
    //清空原数据。返回空Page
    if(r)
        // fill with junk
        memset((char*)r, 5, PGSIZE);
    return (void*)r;
}
