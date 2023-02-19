#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/param.h"

void paramSplit(char *buf, char *params[], int argc){
  int begin = 0;  // 用于存储每个参数的开始位置
  int pnum = 0;
  int i = 0;
  int len = strlen(buf);
  while(i < len){
        while(i < len && buf[i] != ' ') i++;  // 按空格切分
        while(i < len && buf[i] == ' ') buf[i++] = '\0'; // 将空格换成字符串结束符
        if(argc - 1 + pnum >= MAXARG){
          printf("xargs: params are too much!\n");
          exit(-1);
        }
        params[argc - 1 + pnum] = buf + begin;  // params指针数组指向切分后的参数
        begin = i;
        pnum++;  // num记录参数个数
    }
  if(argc - 1 + pnum >= MAXARG){
    printf("xargs: params are too much!\n");
    exit(-1);
  }
  params[argc - 1 + pnum] = 0;
}

int main(int argc, char *argv[])
{
  if(argc < 2){
    printf("xargs: need at least 2 params!\n");
    exit(-1);
  }
  if(argc > MAXARG + 1){
    printf("xargs: params are too much!\n");
    exit(-1);
  }

  char *params[MAXARG];
  for(int i = 1; i < argc; i++){
    params[i-1] = argv[i];  // 将包括命令在内的第二个及以后的参数地址复制到参数列表中
  }

  char buf[512] = "\0";
  int pid;
  while (strcmp(gets(buf, 512), "\0"))
  {
    buf[strlen(buf)-1] = '\0';  // 去掉最末位读入的"\n"
    paramSplit(buf, params, argc);  // 将参数按空格切分
    if ((pid = fork()) < 0) {
      printf("Fork Error!\n");
      exit(-1);
    }
    if(pid == 0){  // 子进程
      if(exec(argv[1], params) == -1) {
        printf("xargs: exec failed!");
        exit(-1);
      }
      exit(0);
    } else wait(0);  // 父进程
  }
  
  exit(0);
}
