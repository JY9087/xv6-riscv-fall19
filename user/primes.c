#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int arr[40];
    int i = 2;
    for(i=2;i<=35;i++){
        arr[i] = i;
    }
    int tmp = 2;
    int flag = 1;
    int fd[2];
    int pid;

    while(flag == 1){
        for(i=2;i<=35;i++){
            if(arr[i] != 0){
                printf("prime %d\n",arr[i]);
                tmp = arr[i];
                arr[i] = 0;
                break;
            }
            if(i == 35){
                flag = 0;
            }
        }

        if(flag == 1){
            for(i=2;i<=35;i++){
                if(arr[i] != 0 && arr[i]%tmp == 0){
                    arr[i] = 0;
                }
            }
            pipe(fd);
            pid = fork();
            if(pid != 0){
                close(fd[0]);
                //Write
                write(fd[1],arr,35*sizeof(int));
                close(fd[1]);

                wait();
                flag = 0;
            }
            else{
                close(fd[1]);
                read(fd[0],arr,35*sizeof(int));
            }
        }

    }

	exit();
}
