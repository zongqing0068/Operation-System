#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void filter(int p[2], int last_num){
    int prime, next_num;
    close(p[1]);  // 关闭写端
    read(p[0], &prime, sizeof(int));  // 读取第一个数，一定为素数
    printf("prime %d\n", prime);
    int not_end = read(p[0], &next_num, sizeof(int));  // not_end表示是否读完
    if(not_end){
        if(last_num < prime * prime){  
    // 若最后一个数字小于第一个素数的平方，则表示这一轮不会再筛掉任何合数，即剩下的均为素数
            do{
                printf("prime %d\n", next_num);  // 输出剩余的所有数
            } while (read(p[0], &next_num, sizeof(int)));
            exit(0);
        } else{  // 否则需进行下一轮的筛选
            int new_p[2], last_p[2];  // last_p管道用来传输上一轮写入的最后一个数字
            // new_p[0] = dup(p[0]);
            // new_p[1] = dup(p[1]);
            if (pipe(new_p) < 0 || pipe(last_p) < 0){
                printf("Pipe Error!\n");
                exit(-1);
            }
            int pid;
            if ((pid = fork()) < 0){
                printf("Fork Error!\n");
                exit(-1);
            }
            if (pid == 0){
                // 子进程
                close(last_p[1]);  // 关闭写端
                read(last_p[0], &last_num, sizeof(int));  // 读取上一轮的最后一个数
                close(last_p[0]);  // 关闭读端
                filter(new_p, last_num);  // 进行下一轮的筛选
            } else{
                // 父进程
                close(new_p[0]);  // 关闭新管道的读端
                if(next_num % prime){
                    write(new_p[1], &next_num, sizeof(int));
                }
                while (read(p[0], &next_num, sizeof(int)))
                {
                    if(next_num % prime){  // 若不是第一个数的倍数，则继续写入下一轮
                        write(new_p[1], &next_num, sizeof(int));
                    }
                }
                close(p[0]);  // 关闭原管道的读端
                close(new_p[1]);  // 关闭新管道的写端
                close(last_p[0]);  // 关闭用于传输最后一个数的管道的读端
                write(last_p[1], &next_num, sizeof(int));  // 传输该轮的最后一个数
                close(last_p[1]);  // 关闭用于传输最后一个数的管道的写端
                wait(0);
            }
        }
        
    }
    exit(0);
}


int main(int argc,char* argv[]){

    int p[2];
    int pid;
    int last_num = 35;

    if (pipe(p) < 0){
        printf("Pipe Error!\n");
        exit(-1);
    }
    if ((pid = fork()) < 0){
        printf("Fork Error!\n");
        exit(-1);
    }
    
    if (pid == 0){
        // 子进程
        filter(p, last_num);
    } else{
        // 父进程
        close(p[0]);  // 关闭读端
        for(int i = 2; i <= last_num; i++){
            write(p[1], &i, sizeof(int));
        }
        close(p[1]);  // 关闭写端
        wait(0);
    }
    exit(0);

}
