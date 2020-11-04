#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    //[0] read [1] write
    //pipe1 parent->child pipe2 child->parent
    int fd1[2];
    int fd2[2];
    int pid;
    pipe(fd1);
    pipe(fd2);
    char str[10];


    pid = fork();
    //Child
    if(pid == 0){
        close(fd1[1]);
        close(fd2[0]);
    }
    //Father
    else{
        close(fd1[0]);
        close(fd2[1]);
    }

    //Child
    //直接read
    if(pid == 0){
        read(fd1[0],str,10);
        printf("%d: received ",getpid());
        printf("%s",str);
        write(fd2[1],"pong\n",5);
    }
    //Father
    else{
        write(fd1[1],"ping\n",5);
        read(fd2[0],str,10);
        printf("%d: received ",getpid());
        printf("%s",str);
    }
    exit();
}
