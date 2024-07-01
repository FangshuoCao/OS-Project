#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void
find(char *path, char *target)
{
    char buf[512], *p;
    int fd;
    struct dirent de; //directory entry struct
    struct stat st; //file status struct

    if((fd = open(path, 0)) < 0){
        fprintf(2, "ls: cannot open %s\n", path);
        return;
    }

    //fstat retrieves info from the inode a fild descriptor refers to
    //here fstat() fills the struct st with infomation about fd
    if(fstat(fd, &st) < 0){
        fprintf(2, "ls: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch(st.type){
    case T_FILE:
        // if the end of the path is "/target", then we find a file, print it
		if(strcmp(path+strlen(path)-strlen(target), target) == 0) {
			printf("%s\n", path);
		}
		break;

    case T_DIR:
        if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
            printf("ls: path too long\n");
            break;
        }

        strcpy(buf, path);
        p = buf+strlen(buf);
        *p++ = '/';   //append a '/' to the end of the path
        
        //read directory entries one by one
        //when the end of the directory is reached, read() will return 0
        while(read(fd, &de, sizeof(de)) == sizeof(de)){ 
            if(de.inum == 0)  //inode number of 0 means this is an unused slot
                continue;

            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            if(stat(buf, &st) < 0){
                printf("ls: cannot stat %s\n", buf);
                continue;
            }
            //make sure you dont recurse into "/." and "/.."
            if(strcmp(buf+strlen(buf)-2, "/.") != 0 && strcmp(buf+strlen(buf)-3, "/..") != 0) {
                find(buf, target);  //use recursion to find in sub directory
            }
        }
        break;
    }
    close(fd);
}

int
main(int argc, char *argv[]){
    if(argc < 3){
        fprintf(2, "usage: find <path> <target>\n");
        exit(1);
    }
    char target[512];
    target[0] = '/';
    strcpy(target + 1, argv[2]);
    find(argv[1], target);
    exit(0);
}