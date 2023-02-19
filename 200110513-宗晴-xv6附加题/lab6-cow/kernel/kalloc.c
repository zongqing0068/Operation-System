// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// 物理地址p对应的物理页号
#define PAGE_NUM(p) ((p)-KERNBASE)/PGSIZE
// 最大的物理页数
#define MAX_PAGE_NUM PAGE_NUM(PHYSTOP)

struct spinlock cow_lock; // cow_count数组的锁
int cow_count[MAX_PAGE_NUM]; // 从KERNBASE到PHYSTOP之间每个物理页被引用的次数

// 通过物理地址获得该物理页被引用的次数
#define PA2COUNT(p) cow_count[PAGE_NUM((uint64)(p))]

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&cow_lock, "cow");  // 初始化cow锁
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
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
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&cow_lock);
  // 只有当该物理页面的被引用次数小于等于0时才被释放
  if(--PA2COUNT(pa) <= 0) {
    
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  release(&cow_lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    PA2COUNT(r) = 1;  // 将该物理页的引用次数初始化为1，此时还未创建子进程，因此无需加锁
  }
    
  return (void*)r;
}

void
cow_add(void *pa)
{
  acquire(&cow_lock);
  PA2COUNT(pa)++;  // 引用次数加一
  release(&cow_lock);
}

void *
cow_copy(void *pa)
{
  acquire(&cow_lock);

  if(PA2COUNT(pa) <= 1) {
    // 当被引用次数减小至小于等于1时，直接返回，无需复制
    release(&cow_lock);
    return pa;
  }

  // 分配独立的内存页，并进行复制
  uint64 newpa = (uint64)kalloc();
  if(newpa == 0) {
    release(&cow_lock);
    printf("cow: failed to kalloc\n");
    return 0;
  }
  memmove((void*)newpa, (void*)pa, PGSIZE);

  // 物理页pa的引用次数减一
  PA2COUNT(pa)--;

  release(&cow_lock);
  return (void*)newpa;
}
