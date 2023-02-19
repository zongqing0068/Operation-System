#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
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
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
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

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
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

  if(argint(0, &pid) < 0)
    return -1;
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



uint64
sys_trace(void)
{
  int n;  // 表示待追踪的系统调用的编号
  if(argint(0, &n) < 0)  // 从寄存器a0中读取该编号
    return -1;
  myproc() -> mask = n;  // 将PCB中的mask值设置为该编号
  return 0;
}

uint64
sys_sysinfo(void)
{
  uint64 p_info;
  if(argaddr(0, &p_info) < 0)  // 获取指向sysinfo结构体的指针
    return -1;

  struct sysinfo info;
  info.freemem = sizeof_freemem();  // 计算剩余的内存空间
  info.nproc = numof_nproc();  // 计算空闲进程数量
  info.freefd = numof_freefd();  // 计算可用文件描述符数量

  struct proc *p = myproc(); 

   // 将内核态的info结构体，结合进程的页表，写到进程内存空间内的p_info指针指向的地址处。
  if(copyout(p->pagetable, p_info, (char *)&info, sizeof(info)) < 0)
    return -1;

  return 0;
}