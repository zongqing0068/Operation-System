#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main(int argc,char* argv[]){

    int p1[2], p2[2];
    int pid;
    char buffer[512];  // pipe缓冲区一般为4KB，即512字节
    char ping[] = "ping";
    char pong[] = "pong";

    if (pipe(p1) < 0 || pipe(p2) < 0) {
        printf("Pipe Error!\n");
        exit(-1);
    }
    if ((pid = fork()) < 0) {
        printf("Fork Error!\n");
        exit(-1);
    }
    
    if (pid == 0)
    {   // 子进程
        int pid_child = getpid();
        close(p1[1]); // 关闭写端
        read(p1[0], buffer, sizeof(buffer));
        close(p1[0]); // 读取完成，关闭读端
        printf("%d: received %s\n", pid_child, buffer);

        close(p2[0]); // 关闭读端
        write(p2[1], pong, sizeof(pong));
        close(p2[1]); // 写入完成，关闭写端

        exit(0);
    } else
    {  // 父进程
        int pid_parent = getpid();
        close(p1[0]); // 关闭读端
        write(p1[1], ping, sizeof(ping));
        close(p1[1]); // 写入完成，关闭写端

        close(p2[1]); // 关闭写端
        read(p2[0], buffer, sizeof(buffer));
        close(p2[0]); // 读取完成，关闭读端
        printf("%d: received %s\n", pid_parent, buffer);

        exit(0);
    }
}