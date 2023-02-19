// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

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
struct kmem kmems[NCPU];// 使每个CPU核使用独立的链表

// NCPU大小为8，因此定义8个以kmem开头的锁名称
char *lock_names[8] = {"kmem0", "kmem1", "kmem2", "kmem3", "kmem4", "kmem5", "kmem6", "kmem7"};

void
kinit()
{
  for(int i = 0; i < NCPU; i++)
    initlock(&(kmems[i].lock), lock_names[i]);
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  // 在kfree中获取当前CPU序号，从而给所有运行freerange的CPU分配空闲的内存
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

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // 获取当前CPU序号，在调用cpuid时需要保证当前进程不可被中断
  push_off();
  int cpu_id = cpuid();
  pop_off();

  // 将对应的空闲页放入空闲列表中
  acquire(&kmems[cpu_id].lock);
  r->next = kmems[cpu_id].freelist;
  kmems[cpu_id].freelist = r;
  release(&kmems[cpu_id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  // 获取当前CPU序号，在调用cpuid时需要保证当前进程不可被中断
  push_off();
  int cpu_id = cpuid();
  pop_off();  

  acquire(&kmems[cpu_id].lock);
  r = kmems[cpu_id].freelist;
  if(r){
    kmems[cpu_id].freelist = r->next;
    release(&kmems[cpu_id].lock);
  }
  else{  // 此时需要从其他CPU的freelist中窃取内存块
    // 先释放当前CPU对应的锁，从而避免死锁
    release(&kmems[cpu_id].lock);
    for(int i = 0; i < NCPU; i++){
      if(i == cpu_id) continue;  // 如果是当前CPU则跳过
      acquire(&kmems[i].lock);
      r = kmems[i].freelist;  // 访问该CPU的freelist
      if(r){  // 窃取1页内存
        kmems[i].freelist = r->next;
        release(&kmems[i].lock);
        break;  // 窃取成功则停止
      } else release(&kmems[i].lock);
    }
  }
  
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
