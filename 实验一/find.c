#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void
find(char *path, char *file)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){  // 打开路径
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){  // 存储文件信息
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_FILE:  // 表示第一个参数不是文件夹名
    printf("find: the first param should be dirname\n");
    return;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("find: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';  // 加上分隔符
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0 || !strcmp(de.name, ".") || !strcmp(de.name, ".."))  // 避免递归进入"."和".."
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;  // 加上结束符
      if(stat(buf, &st) < 0){
        printf("find: cannot stat %s\n", buf);
        continue;
      }
      switch(st.type){
        case T_FILE:
            if(!strcmp(de.name, file)) printf("%s\n", buf);  // 成功找到文件
            break;
        case T_DIR:
            find(buf, file);  // 递归查找当前文件夹
            break;
      }
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  if(argc < 3){
    printf("find: need 3 params!\n");
    exit(-1);
  }
  find(argv[1], argv[2]);  // 第一个参数为路径，第二个参数为待查找文件名
  exit(0);
}
