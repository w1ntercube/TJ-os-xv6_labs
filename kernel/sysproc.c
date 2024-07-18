#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

int
sys_pgaccess(void)
{
  //接收用户空间传过来的三个参数
  uint64 buf;//虚拟地址
  int size ;
  uint64 abits;
  argaddr(0,&buf);
  argint(1,&size);
  argaddr(2,&abits);
  //这里是第四个提示，需要创建一个临时的缓冲区
  int abitss = 0;
  //获取当前进程
  struct proc *p = myproc();
  for(int i = 0;i<size;i++){
    int va = buf + i * PGSIZE;
    int abit  = 0;
    //从用户空间传过来的是虚拟地址，判断这个地址是否被访问过需要去查页表
    //使用walk，通过虚拟地址查找页表项。
    pte_t *pte = walk(p->pagetable,va,0);
    if(pte == 0)
      return 0;
    //检查对应的标志位
    if((*pte & PTE_A) != 0){
      *pte = *pte & (~PTE_A);//清零 pte_a！！！
      abit = 1;
    }
    //将结果保存到缓冲区
    abitss = abitss | abit << i;
  }
  //将结果复制回用户空间
  copyout(p->pagetable,abits,(char *)&abitss,sizeof(abitss));
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
