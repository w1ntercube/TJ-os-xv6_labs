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

#define NBUCKET 13      // 哈希表的桶数量
#define HASH(blockno) (blockno % NBUCKET)

struct {
  struct spinlock lock;   // 用于缓冲区分配和大小记录
  struct buf buf[NBUF];   // 缓冲区数组
  int size;              
  struct buf buckets[NBUCKET];  // 哈希表的桶
  struct spinlock locks[NBUCKET];   // 每个桶的锁
  struct spinlock hashlock;     // 哈希表的全局锁
} bcache;

void binit(void) {
  int i;
  struct buf *b;

  bcache.size = 0;
  initlock(&bcache.lock, "bcache");
  initlock(&bcache.hashlock, "bcache_hash");

  // 初始化每个桶的锁
  for (i = 0; i < NBUCKET; ++i) {
    initlock(&bcache.locks[i], "bcache_bucket");
  }

  // 初始化所有缓冲区并将其加入哈希桶中
  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    initsleeplock(&b->lock, "buffer");
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf* bget(uint dev, uint blockno) {
  struct buf *b;
  int idx = HASH(blockno);  // 根据块号计算哈希值，确定对应的桶
  struct buf *pre, *minb = 0, *minpre;
  uint mintimestamp;
  int i;

  // 在哈希桶中查找缓冲区
  acquire(&bcache.locks[idx]);
  for (b = bcache.buckets[idx].next; b; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.locks[idx]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 如果缓冲区未缓存，检查是否有未使用的缓冲区可分配
  acquire(&bcache.lock);
  if (bcache.size < NBUF) {
    b = &bcache.buf[bcache.size++];
    release(&bcache.lock);
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    b->next = bcache.buckets[idx].next;
    bcache.buckets[idx].next = b;
    release(&bcache.locks[idx]);
    acquiresleep(&b->lock);
    return b;
  }
  release(&bcache.lock);
  release(&bcache.locks[idx]);

  // 基于时间戳寻找最近未使用的缓冲区
  acquire(&bcache.hashlock);
  for (i = 0; i < NBUCKET; ++i) {
    mintimestamp = -1;
    acquire(&bcache.locks[idx]);
    for (pre = &bcache.buckets[idx], b = pre->next; b; pre = b, b = b->next) {
      // 再次检查是否已缓存
      if (idx == HASH(blockno) && b->dev == dev && b->blockno == blockno) {
        b->refcnt++;
        release(&bcache.locks[idx]);
        release(&bcache.hashlock);
        acquiresleep(&b->lock);
        return b;
      }
      // 查找时间戳最小的未使用缓冲区
      if (b->refcnt == 0 && b->timestamp < mintimestamp) {
        minb = b;
        minpre = pre;
        mintimestamp = b->timestamp;
      }
    }
    // 找到未使用的缓冲区
    if (minb) {
      minb->dev = dev;
      minb->blockno = blockno;
      minb->valid = 0;
      minb->refcnt = 1;
      // 如果缓冲区在另一个桶中，将其移动到正确的桶
      if (idx != HASH(blockno)) {
        minpre->next = minb->next; // 移除缓冲区
        release(&bcache.locks[idx]);
        idx = HASH(blockno); // 计算正确的桶索引
        acquire(&bcache.locks[idx]);
        minb->next = bcache.buckets[idx].next; // 移动缓冲区到正确的桶
        bcache.buckets[idx].next = minb;
      }
      release(&bcache.locks[idx]);
      release(&bcache.hashlock);
      acquiresleep(&minb->lock);
      return minb;
    }
    release(&bcache.locks[idx]);
    if (++idx == NBUCKET) {
      idx = 0;
    }
  }
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
extern uint ticks;  // 引入系统时钟计数器

void brelse(struct buf *b) {
  int idx;
  if (!holdingsleep(&b->lock)) // 缓冲区没有被锁定，则报错
    panic("brelse");

  releasesleep(&b->lock);

  idx = HASH(b->blockno);
  acquire(&bcache.locks[idx]); // 锁定哈希表的对应桶
  b->refcnt--;  
  if (b->refcnt == 0) {
    b->timestamp = ticks;  // 更新最后使用时间戳
  }
  release(&bcache.locks[idx]); // 解锁哈希表的对应桶
}

void
bpin(struct buf *b) {
  // change the lock - lab8-2
  int idx = HASH(b->blockno);
  acquire(&bcache.locks[idx]);
  b->refcnt++;
  release(&bcache.locks[idx]);
}

void
bunpin(struct buf *b) {
  // change the lock - lab8-2
  int idx = HASH(b->blockno);
  acquire(&bcache.locks[idx]);
  b->refcnt--;
  release(&bcache.locks[idx]);
}


