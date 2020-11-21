#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define MAXARGS 10


static char buf[100];
//static char pipeArgv[MAXARGS][100];

int pipeFlag;

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(-1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}


struct cmd {
  int type;
};

struct unifiedcmd{
    int type;

    int execFlag;
    char *argv[MAXARGS];
    char *eargv[MAXARGS];

    //输入重定向
    int inRedirFlag;
    char *infile;
    char *inefile;
    int inmode;
    int infd;

    //输出重定向
    int outRedirFlag;
    char *outfile;
    char *outefile;
    int outmode;
    int outfd;
};


struct unifiedcmd ucmd;
struct unifiedcmd ucmd_pipe;


struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};


struct unifiedcmd*
execcmd(void)
{
    struct unifiedcmd *cmd;
    if(pipeFlag == 0){
        cmd = &ucmd;
    }
    else{
        cmd = &ucmd_pipe;
    }
    cmd->execFlag = 1;
    cmd->type = EXEC;
    return cmd;
}



//重定向cmd
struct unifiedcmd*
inredircmd(struct unifiedcmd *cmd , char *file, char *efile, int mode, int fd)
{
    cmd->inRedirFlag = 1;
    cmd->type = REDIR;
    cmd->infile = file;
    cmd->inefile = efile;
    cmd->inmode = mode;
    cmd->infd = fd;
    return cmd;
}

struct unifiedcmd*
outredircmd(struct unifiedcmd *cmd , char *file, char *efile, int mode, int fd)
{
    cmd->outRedirFlag = 1;
    cmd->type = REDIR;
    cmd->outfile = file;
    cmd->outefile = efile;
    cmd->outmode = mode;
    cmd->outfd = fd;
    return cmd;
}


//空白字符
char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
getcmd(char *buf, int nbuf)
{
    //以@作为提示符
    fprintf(2, "@ ");
    memset(buf, 0, nbuf);
    //输入字符串
    gets(buf, nbuf);
    //没有命令
    if(buf[0] == 0) // EOF
    return -1;
    return 0;
}



//peek基本上只让 ps 跳过空白。如果*s属于tok则进行特殊处理
//peek的返回值:如果空白后的第一个字符是token,则返回非0
int
peek(char **ps, char *es, char *toks)
{

    char *s;

    //s是指针。修改s , 会真的修改 char* ps
    s = *ps;

    //跳过空白字符
    while(s < es && strchr(whitespace, *s))
        s++;
    *ps = s;

    return *s && strchr(toks, *s);
}


//判断是token或是字符串参数
int
gettoken(char **ps, char *es, char **q, char **eq)
{

    char *s;
    int ret;

    //对s的修改会实际影响到char* ps
    s = *ps;

    //跳过空白字符
    while(s < es && strchr(whitespace, *s))
        s++;

    if(q)
        *q = s;

    //返回值默认是ps的第1个字符
    ret = *s;

    //此时s应该已经是重定向符<>的位置了
    switch(*s){
    case 0:
        break;
    case '|':
    case '<':
        s++;
        break;
    case '>':
        s++;
        if(*s == '>'){
          ret = '+';
          s++;
        }
        break;
    default:
        ret = 'a';
        //在这之前，经过了那么多case，都没有break.所以这是个参数
        //如果找到空白和特殊字符，则返回非0
        //找不到symbol或空白字符时，s一直++。来到空白字符为止
        //所以end q是空白字符

        //停止条件有问题
        while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s)){
           s++;
        }

        break;
    }


    if(eq)
        *eq = s;

    //这东西相当合理
    /*
    if(pipeFlag == 1){
        fprintf(2, "length %d   eq char %c int %d  q char %c int %d\n" , *eq - *q , **eq , *eq-buf, **q, *q-buf);
    }
    */

    //跳过空白字符
    while(s < es && strchr(whitespace, *s))
        s++;
    *ps = s;
    return ret;
}

struct unifiedcmd *parsecmd(char*);
struct unifiedcmd *parseline(char**, char*);
struct unifiedcmd *parsepipe(char**, char*);
struct unifiedcmd *parseexec(char**, char*);
struct unifiedcmd *nulterminate(struct unifiedcmd*);



struct unifiedcmd*
parseredirs(struct unifiedcmd *cmd, char **ps, char *es)
{
    int tok;
    char *q, *eq;

    while(peek(ps, es, "<>")){
        //tok返回了符号token
        tok = gettoken(ps, es, 0, 0);

        //返回文件file
        if(gettoken(ps, es, &q, &eq) != 'a')
            panic("missing file for redirection");

        //处理读/写
        // >  >>  相同处理
        switch(tok){
        case '<':
          cmd = inredircmd(cmd,q, eq, O_RDONLY, 0);
          break;
        case '>':
          cmd = outredircmd(cmd,q, eq, O_WRONLY|O_CREATE, 1);
          break;
        case '+':  // >>
          cmd = outredircmd(cmd,q, eq, O_WRONLY|O_CREATE, 1);
          break;
        }
    }

    return cmd;
}


struct unifiedcmd*
parseexec(char **ps, char *es)
{
    char *q, *eq;
    int tok, argc;
    struct unifiedcmd *ret;

    //生成一个execcmd
    //在这里分道扬镳，进行pipe和后续修改
    ret = execcmd();

    argc = 0;
    ret = parseredirs(ret , ps, es);

    //这个地方有问题
    //检测到 | 则当前命令结束
    while(!peek(ps, es, "|")){
        //tok = 0 = \0 字符串结束
        if((tok=gettoken(ps, es, &q, &eq)) == 0)
            break;

        if(tok != 'a')
            panic("syntax");
        ret->argv[argc] = q;
        ret->eargv[argc] = eq;

        //fprintf(2, "pipe %d argv[%d] %s\n", pipeFlag , argc , ret->argv[argc]);

        argc++;
        if(argc >= MAXARGS)
            panic("too many args");
        ret = parseredirs(ret , ps, es);
    }
    //最后一个参数设为0
    ret->argv[argc] = 0;
    ret->eargv[argc] = 0;
    return ret;
}

struct unifiedcmd*
parsepipe(char **ps, char *es)
{
  struct unifiedcmd *cmd;

  cmd = parseexec(ps, es);

  //如果有|则递归执行当前函数
  //右侧是另一个管道
  //检测到 | 会使parseexec结束，此时ps停在 | 的位置
  //用gettoken拿到 | ,执行下一个命令
  if(peek(ps, es, "|")){
    gettoken(ps, es, 0, 0);
    pipeFlag = 1;
    cmd = parsepipe(ps, es);
  }

  return cmd;
}


struct unifiedcmd*
parseline(char **ps, char *es)
{
    struct unifiedcmd *cmd;

    //处理命令
    cmd = parsepipe(ps, es);

    return cmd;
}


//null
//terminate
//e 开头的都是陪衬
//把空格变成/0
struct unifiedcmd*
nulterminate(struct unifiedcmd *cmd)
{

  int i;
  for(i=0; ucmd.argv[i]; i++)
      *(ucmd.eargv[i]) = 0;

    if(ucmd.inRedirFlag == 1){
      *(ucmd.inefile) = 0;
    }

    if(ucmd.outRedirFlag == 1){
      *(ucmd.outefile) = 0;
    }


  if(pipeFlag == 1){
    for(i=0; ucmd_pipe.argv[i]; i++)
    *(ucmd_pipe.eargv[i]) = 0;

    if(ucmd_pipe.inRedirFlag == 1){
      *(ucmd_pipe.inefile) = 0;
    }

    if(ucmd_pipe.outRedirFlag == 1){
      *(ucmd_pipe.outefile) = 0;
    }

  }


  if(cmd == 0)
    return 0;

  return cmd;
}

struct unifiedcmd*
parsecmd(char *s)
{

    char *es;
    struct unifiedcmd *cmd;

    es = s + strlen(s);

    //分析一行命令得到 command 类型
    //传递引用
    //parseline可能会递归
    cmd = parseline(&s, es);

    //去除空白字符
    //对buf进行操作，与cmd无关
    peek(&s, es, "");
    if(s != es){
        fprintf(2, "leftovers: %s\n", s);
        panic("syntax");
    }

    nulterminate(cmd);

    return cmd;
}



// Execute cmd.  Never returns.
//在runcmd之前，空格变成了0
//runcmd需要参数。默认传递ucmd，有需要就传递 ucmd_pipe
//fork完全保留。在第二个的时候进行flag的处理

int p[2];

void runcmdpipe(){
    if(ucmd_pipe.inRedirFlag == 1){
            ucmd_pipe.inRedirFlag = 0;
            close(ucmd_pipe.infd);
            //试图打开并重定向
            //打开失败则错误处理
            if(open(ucmd_pipe.infile, ucmd_pipe.inmode) < 0){
              fprintf(2, "open %s failed\n", ucmd_pipe.infile);
              exit(-1);
        }
        //递归调用。执行真正的cmd
        runcmdpipe();
        goto endrun;
        }

        if(ucmd_pipe.outRedirFlag == 1){
            ucmd_pipe.outRedirFlag = 0;

            close(ucmd_pipe.outfd);
            //试图打开并重定向
            //打开失败则错误处理

            //这个open实际上是有问题的
            if(open(ucmd_pipe.outfile, ucmd_pipe.outmode) < 0){
              fprintf(2, "open %s failed\n", ucmd_pipe.outfile);
              exit(-1);
            }
            //递归调用。执行真正的cmd
            //inflie没有成功
            runcmdpipe();
            goto endrun;
        }



        //命令处理失败
        if(ucmd_pipe.execFlag == 1){
            ucmd_pipe.execFlag = 0;
            if(ucmd.argv[0] == 0)
              exit(-1);

            //执行不会返回了
            exec(ucmd_pipe.argv[0], ucmd_pipe.argv);
            //如果exec成功了，函数是不会返回的
            fprintf(2, "pipe exec %s has failed\n",ucmd_pipe.argv[0]);

        }
    endrun :
  exit(0);
}


void
runcmd()
{
    //两个run cmd
    if(pipeFlag == 1){
        pipeFlag = 0;
        if(pipe(p) < 0)
            panic("pipe");

        //第一条命令输出到第二条命令
        if(fork1() == 0){
            //关闭标准输出1
            close(1);
            //复制文件描述符到最小的可用fd , 也就是1
            dup(p[1]);
            //关闭子程序继承的p fd
            close(p[0]);
            close(p[1]);
            //递归执行cmd
            runcmd();
        }

        //另一个子进程
        //这次是修改输入
        if(fork1() == 0){
            close(0);
            dup(p[0]);
            close(p[0]);
            close(p[1]);
            runcmdpipe();
        }
        //关闭p fd 并等待进程结束
        close(p[0]);
        close(p[1]);
        wait(0);
        wait(0);
        goto endrun;

    }

        if(ucmd.inRedirFlag == 1){
            ucmd.inRedirFlag = 0;
            close(ucmd.infd);
            //试图打开并重定向
            //打开失败则错误处理
            if(open(ucmd.infile, ucmd.inmode) < 0){
              fprintf(2, "open %s failed\n", ucmd.infile);
              exit(-1);
        }
        //递归调用。执行真正的cmd
        runcmd();
        goto endrun;
        }

        if(ucmd.outRedirFlag == 1){
        ucmd.outRedirFlag = 0;

        close(ucmd.outfd);
        //试图打开并重定向
        //打开失败则错误处理

        //这个open实际上是有问题的
        if(open(ucmd.outfile, ucmd.outmode) < 0){
          fprintf(2, "open %s failed\n", ucmd.outfile);
          exit(-1);
        }
        //递归调用。执行真正的cmd
        //inflie没有成功
        runcmd();
        goto endrun;
        }


        //重定向后的exec有问题
        //argv[]的处理有问题
        if(ucmd.execFlag == 1){
        ucmd.execFlag = 0;
        if(ucmd.argv[0] == 0)
          exit(-1);
        //实际执行函数。参数列表为argv
        exec(ucmd.argv[0], ucmd.argv);
        //如果exec成功了，函数是不会返回的
        fprintf(2, "origin exec %s has failed\n", ucmd.argv[0]);
        }


    endrun :
  exit(0);
}




int main(void)
{
    // Read and run input commands.
    while(getcmd(buf, sizeof(buf)) >= 0){
    //特判cd命令
    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
    // Chdir must be called by the parent, not the child.
        buf[strlen(buf)-1] = 0;  // chop \n 去掉\n改为\0

        //chdir函数改变目录ChangeDirectory
        //以char*作为参数。所以把\n去掉改为\0
        if(chdir(buf+3) < 0)
            fprintf(2, "cannot cd %s\n", buf+3);
        //等待下一条命令
        continue;
    }
    pipeFlag = 0;
    if(fork1() == 0){
        parsecmd(buf);
        runcmd();
        }
        wait(0);
    }
    exit(0);
}
