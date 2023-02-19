// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

extern uint ticks;  // 时间戳，在kernel/trap.c中被不断更新

// struct {
//   struct spinlock lock;
//   struct buf buf[NBUF];

//   // Linked list of all buffers, through prev/next.
//   // Sorted by how recently the buffer was used.
//   // head.next is most recent, head.prev is least.
//   struct buf head;
// } bcache;


struct {
  // 全局锁，只在窃取其它桶的内存块时使用，并不会影响并发效率
  struct spinlock lock;  
  struct spinlock bucketlocks[NBUCKETS];  // 每个哈希桶的锁
  struct buf buf[NBUF];
  struct buf hashbucket[NBUCKETS]; //每个哈希桶对应一个链表
} bcache;

uint
hash(uint n)
{
  return n % NBUCKETS;
}

void
binit(void)
{
  struct buf *b;

  // 初始化全局锁
  initlock(&bcache.lock, "bcache");

  for(int i = 0; i < NBUCKETS; i++){
    // 初始化每个哈希桶的锁
    initlock(&bcache.bucketlocks[i], "bcache.bucket");

    // Create linked list of buffers
    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
  }

  // 初始化哈希链表，将缓存块均匀分配到每个桶里
  for(int i = 0; i < NBUF; i++){
      uint key = hash(i);
      b = &bcache.buf[i];
      b->next = bcache.hashbucket[key].next;
      b->prev = &bcache.hashbucket[key];
      initsleeplock(&b->lock, "buffer");
      bcache.hashbucket[key].next->prev = b;
      bcache.hashbucket[key].next = b;
      b->time = 0;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  // 获取对应哈希桶的锁
  uint key = hash(blockno);
  acquire(&bcache.bucketlocks[key]);

  // Is the block already cached?
  for(b = bcache.hashbucket[key].next; b != &bcache.hashbucket[key]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucketlocks[key]);  // 释放对应哈希桶的锁
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  struct buf *LRU_b = b;  // the least recently used (LRU) unused buffer
  uint LRU_time;  // the least recently used (LRU) time
  int first_unused_flag = 1;  // 判断是否查找到第一个unused buffer，用于给LRU_time赋初值
  // 先查找当前哈希桶内的。
  // 此时有时间戳，因此无需从后向前查找
  for(b = bcache.hashbucket[key].next; b != &bcache.hashbucket[key]; b = b->next){
    if(b->refcnt == 0) {
      if(first_unused_flag){
        first_unused_flag = 0;
        LRU_time = b->time;
        LRU_b = b;
      } else{
        if(b->time < LRU_time){
          LRU_time = b->time;
          LRU_b = b;
        }
      }
    }
  }
  if(!first_unused_flag){  // flag已被修改，说明已找到一个unused buffer
    LRU_b->dev = dev;
    LRU_b->blockno = blockno;
    LRU_b->valid = 0;
    LRU_b->refcnt = 1;
    LRU_b->time = ticks;
    release(&bcache.bucketlocks[key]);  // 释放对应哈希桶的锁
    acquiresleep(&LRU_b->lock);
    return LRU_b;
  }

  release(&bcache.bucketlocks[key]);  // 释放对应哈希桶的锁
  // Still not cached.
  // 获取全局锁，保证为未命中的缓存分配一个新的条目的操作是原子性的
  // 仅在挪用不同哈希桶之间的内存块时使用到了全局锁，因此并不影响其余进程的并行
  acquire(&bcache.lock);
  // 再次判断是否命中，两个进程并行时，防止为同一未命中的缓存分配新的条目的操作重复
  // 从而避免在一个桶里加入两个相同的缓存块
  acquire(&bcache.bucketlocks[key]);
  // Is the block already cached?
  for(b = bcache.hashbucket[key].next; b != &bcache.hashbucket[key]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucketlocks[key]);  // 释放对应哈希桶的锁
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bucketlocks[key]);  // 释放对应哈希桶的锁

  // 再查找其余哈希桶内的。
  int LRU_hash = key;  // LRU unused buffer所在的哈希桶
  for(int i = 0; i < NBUCKETS; i++){
    if(i == key) continue;
    acquire(&bcache.bucketlocks[i]);  // 获取该哈希桶的锁
    for(b = bcache.hashbucket[i].next; b != &bcache.hashbucket[i]; b = b->next){
      if(b->refcnt == 0) {
        if(first_unused_flag){
          first_unused_flag = 0;
          LRU_time = b->time;
          LRU_b = b;
          LRU_hash = i;
        } else{
          if(b->time < LRU_time){
            LRU_time = b->time;
            LRU_b = b;
            LRU_hash = i;
          }
        }
      }
    }
    release(&bcache.bucketlocks[i]);    // 释放该哈希桶的锁
  }

  // 查看是否找到空闲缓存块
  if(!first_unused_flag){  // flag已被修改，说明已找到一个unused buffer
    acquire(&bcache.bucketlocks[LRU_hash]);  // 获取LRU unused buffer所在的哈希桶的锁
    LRU_b->dev = dev;
    LRU_b->blockno = blockno;
    LRU_b->valid = 0;
    LRU_b->refcnt = 1;
    LRU_b->time = ticks;
    LRU_b->prev->next = LRU_b->next;  // 将该缓存块从当前哈希桶中移出
    LRU_b->next->prev = LRU_b->prev;
    release(&bcache.bucketlocks[LRU_hash]);  // 释放该哈希桶的锁

    acquire(&bcache.bucketlocks[key]);  // 获取key所在的哈希桶的锁
    bcache.hashbucket[key].next->prev = LRU_b;  // 将该缓存块移到key所在的哈希桶内
    LRU_b->next = bcache.hashbucket[key].next;
    LRU_b->prev = &bcache.hashbucket[key];
    bcache.hashbucket[key].next = LRU_b;
    release(&bcache.bucketlocks[key]);  // 释放key所在的哈希桶的锁

    release(&bcache.lock);  // 释放全局锁
    acquiresleep(&LRU_b->lock);
    return LRU_b;
  }

  release(&bcache.lock);  // 释放全局锁
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint key = hash(b->blockno);
  acquire(&bcache.bucketlocks[key]);

  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.

    // 有时间戳后便不需要将释放的缓存块移动到队头
    // b->next->prev = b->prev;
    // b->prev->next = b->next;
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    // bcache.head.next->prev = b;
    // bcache.head.next = b;

    // 修改时间戳
    b->time = ticks;
  }
  
  release(&bcache.bucketlocks[key]);
}

void
bpin(struct buf *b) {
  uint key = hash(b->blockno);
  acquire(&bcache.bucketlocks[key]);
  b->refcnt++;
  release(&bcache.bucketlocks[key]);
}

void
bunpin(struct buf *b) {
  uint key = hash(b->blockno);
  acquire(&bcache.bucketlocks[key]);
  b->refcnt--;
  release(&bcache.bucketlocks[key]);
}


