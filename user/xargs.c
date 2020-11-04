#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"



int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "Usage: xargs command\n");
        exit();
    }

    char* xargv[MAXARG];
    int i;
    //不需要argv[0]=函数名
    for(i=0;i<argc-1;i++){
        xargv[i] = argv[i+1];
    }

    int status = 1;

    char argBuffer[1024];

    int argStart = 0;

    int bufferCount = 0;

    int xargvCount = argc - 1;

    char c ;

    while(status){
        argStart = 0;
        bufferCount = 0;
        xargvCount = argc - 1;
        while(1){
            //标准输入文件描述符：0
            status = read(0,&c,1);
            if(status == 0){
                exit();
            }
            //一个参数输入结束
            if(c == ' ' || c == '\n'){
                argBuffer[bufferCount] = '\0';
                bufferCount++;
                xargv[xargvCount] = &argBuffer[argStart];
                xargvCount++;
                //定位到下一个参数开始的位置
                argStart = bufferCount;

                //一行输入结束，开始执行
                if(c == '\n')
                    break;
            }
            else{
                argBuffer[bufferCount] = c;
                bufferCount++;
            }
        }

        //主进程等待下一个输入；子进程执行
        if(fork()==0){
            exec(xargv[0],xargv);
        }
        else{
            wait();
        }

    }

    exit();
}
