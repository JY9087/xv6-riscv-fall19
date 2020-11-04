#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *path, char *filename) {
    char buf[512], *p;
    //file description 文件描述符
    int fd;
    struct dirent de;
    struct stat st;

    if((fd = open(path, 0)) < 0){
        fprintf(2, "ls: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0){
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
    }

    switch(st.type){
    case T_FILE:
    {
        break;
    }
    case T_DIR:
    {
        if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
            printf("ls: path too long\n");
            break;
        }

        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';

        while (read(fd, &de, sizeof(de)) == sizeof(de)) {
            if (de.inum == 0)
                continue;
            if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                continue;
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            if (stat(buf, &st) < 0) {
                fprintf(2, "find: cannot stat %s\n", buf);
                continue;
            }
            //递归搜索
            if(st.type == T_DIR){
              find(buf, filename);
            }
            //找到文件
            else if (st.type == T_FILE && strcmp(de.name, filename) == 0) {
              printf("%s\n", buf);
            }
        }
    }

    }
    close(fd);
}

int main(int argc, char *argv[]){
  if (argc < 3) {
    fprintf(2, "Usage: find path filename\n");
    exit();
  }
  find(argv[1], argv[2]);
  exit();
}
