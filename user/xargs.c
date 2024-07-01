#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

/*
    key point about xargs and |
    when you run: cmdA argA | xargs cmdB argB
    you may think that this is a whole command with argc == 6, 
    but actually it is two separate command
    the shell will first execute cmdA with argA, and redirect its output
    to the stdin of cmdB, due to the '|'
    then it will call xargs.
    thus for this example the argc for xargs is only 3 (xargs, cmdB, argB)
*/

void run(char *cmd, char *args[]){
    if(fork() == 0){
        //child
        exec(cmd, args);
        //if exec() success, child will not return
        fprintf(1, "exec %s failed\n", cmd);
        exit(1);
    }
}

int main(int argc, char *argv[]){
    char buf[2048];
    char *p = buf, *p_end = buf;  //start and end of the current string
    char *argList[128];     //list of final arguments to be used for cmdB each time
    char **p_argv = argList;    //pointer to the origin arguments from argv in final arg list

    //add the original arguments of cmdB to the final argument list
    for(int i = 1; i < argc; i++){
        *p_argv = argv[i];  //copy each argument from argv to final arg list
        p_argv++;   
    }

    char **p_stdin = p_argv;  //arguments from stdin starts after the last argument in argv
    //add the output of cmdA to the final argument list
    while(read(0, p, 1) != 0){    //read from stdin
        if(*p == ' ' || *p == '\n'){    //end of an argument
            *p = '\0';  //null terminate the argument
            *(p_stdin++) = p_end;   //add argument to the final list
            p_end = p + 1;  //update p_end to the start of the next argument
        
            if(*p == '\n'){ //if a line ends
                *p_stdin = 0;  //mark the end of the list of arguments for cmdB
                run(argv[1], argList);  //run cmdB for this line
                p_stdin = p_argv;   //reset pointer to the next position of the last original argument from argv
            }
        }
        p++;    //prepare to read next character
    }

    if(p_stdin != p_argv){  //if the last line is not empty
        //deal with the last remaining argument
        *p = '\0';
        *(p_stdin++) = p_end;
        *p_stdin = 0;
        run(argv[1], argList);
    }   
    
    while(wait(0) != -1){}; //you need to wait for each child to finish
    exit(0);
}

