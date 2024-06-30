#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[]){
    int p2c[2], c2p[2];
    pipe(p2c);  //parent to child
    pipe(c2p);  //child to parent

    if(fork() > 0){
        close(p2c[0]);  //close read end of p2c
        close(c2p[1]);  //close write end of c2p

        write(p2c[1], "a", 1);
        char buf;
        read(c2p[0], &buf, 1);
        fprintf(1, "%d: received pong\n", getpid());

        wait(0);    //parent needs to call wait()
    }else{
        //child process
        close(p2c[1]);
        close(c2p[0]);

        char buf;
        read(p2c[0], &buf, 1);
        fprintf(1, "%d: received ping\n", getpid());
        write(c2p[1], "a", 1);
    }

    exit(0);
}