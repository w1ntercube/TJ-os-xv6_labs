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

struct {
  struct spinlock lock; // 自旋锁
  struct run *freelist; // 空闲链表
} kmem[NCPU];

void
kinit()
{
  char buf[10]; // 初始化每个cpu锁
  for (int i = 0; i < NCPU; i++)
  {
    snprintf(buf, 10, "kmem_CPU%d", i); // 格式化锁的名称
    initlock(&kmem[i].lock, buf); // 初始化锁
  }
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

// Free the page of physical memory pointed at by pa,
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

  // 关闭中断，获取当前CPU的ID
  push_off();
  int cpu = cpuid();
  pop_off();
  // 获取当前CPU的锁，并将页面添加到空闲链表
  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  // 关闭中断，获取当前CPUID
  push_off();
  int cpu = cpuid();
  pop_off();

  // 获取当前CPU的锁，并尝试从空闲链表中分配页面
  acquire(&kmem[cpu].lock);
  r = kmem[cpu].freelist;
  if(r)
    kmem[cpu].freelist = r->next; // 成功分配，将页面从空闲链表中移除
  else // 从其他CPU窃取页面
  {
    struct run* tmp;
    for (int i = 0; i < NCPU; ++i)
    {
      if (i == cpu) continue; // 跳过当前CPU
      acquire(&kmem[i].lock);
      tmp = kmem[i].freelist;
      if (tmp == 0) { // 当前CPU无空闲页面，释放，继续检查下一个CPU
        release(&kmem[i].lock);
        continue;
      } else {
        for (int j = 0; j < 1024; j++) {
          // 窃取最多1024个页面
          if (tmp->next)
            tmp = tmp->next;
          else
            break;
        }
        kmem[cpu].freelist = kmem[i].freelist;
        kmem[i].freelist = tmp->next;
        tmp->next = 0;
        release(&kmem[i].lock);
        break;
      }
    }
    r = kmem[cpu].freelist;
    if (r)
      kmem[cpu].freelist = r->next;
  }
  release(&kmem[cpu].lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}