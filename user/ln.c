#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

//unit ln command
//create a hard link between two files
//a hard link is an additional name for an existing file on the file system
int
main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "Usage: ln old new\n");
    exit(1);
  }
  if(link(argv[1], argv[2]) < 0)
    fprintf(2, "link %s %s: failed\n", argv[1], argv[2]);
  exit(0);
}
