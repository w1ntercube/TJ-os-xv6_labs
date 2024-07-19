#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
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

  backtrace(); // 调用
  argint(0, &n);
  if(n < 0)
    n = 0;
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

uint64 sys_sigalarm(void)
{
  struct proc *p;
  int ticks;
  uint64 handler;

  // 从用户态获取第一个参数（报警间隔），存储在 ticks 变量中
  argint(0, &ticks);
  // 从用户态获取第二个参数（处理函数指针），存储在 handler 变量中
  argaddr(1, &handler);
  // 获取当前进程的进程控制块
  p = myproc();

  // 设置当前进程的报警间隔
  p->alarm_ddl = ticks;
  // 初始化当前报警计数为 0
  p->alarm_cur = 0;
  // 设置当前进程的报警处理函数
  p->alarm_handler = (void (*)(void))handler;

  return 0;
}

uint64 sys_sigreturn(void)
{
  struct proc *p;

  // 获取当前进程的进程控制块
  p = myproc();

  // 将保存的 trapframe 恢复到当前进程的 trapframe
  memmove(p->trapframe, &p->alarm_tf, sizeof(struct trapframe));

  // 标记当前不在 sigalarm 处理中
  p->in_sigalarm = 0;

  // 返回中断前的 a0 寄存器的值
  return p->trapframe->a0;
}
