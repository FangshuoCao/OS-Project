#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void sieve(int pl[2]){
    //pl: left pipe
    close(pl[1]);   //we will never write to the left pipe, so close it
    
    int p;
    if(read(pl[0], &p, sizeof(p)) == 0) {   //if there is no more element to read
        close(pl[0]);
        exit(0);
    }
    printf("prime %d\n", p);

    int pr[2];  //create right pipe
    pipe(pr);

    if(fork() != 0){
        //parent
        close(pr[0]);   //current process will never read from right pipe
        int buf;
        while(read(pl[0], &buf, sizeof(buf)) > 0){
            if(buf % p != 0){
                write(pr[1], &buf, sizeof(buf));
            }
        }
        close(pr[1]); //close the write end after finish all writing
        wait(0);    //wait for child
        close(pl[0]);
        exit(0);
    }else{
        //child
        close(pl[0]);
        sieve(pr);
    }
}

int
main(int argc, char *argv[]){
    int pf[2];  //first pipe
    pipe(pf);

    if(fork() != 0){
        close(pf[0]);
        for(int i = 2; i <= 35; i++){
            write(pf[1], i, sizeof(i));
        }
        close(pf[1]);   //close the write end after writing all numbers
        wait(0);    //wait for child
    }else{
        close(pf[1]);
        sieve(pf);
        exit(0);
    }
    wait(0);
    exit(0);

}