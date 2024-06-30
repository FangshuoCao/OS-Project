#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

char*
strcpy(char *s, const char *t)
{
  //copy t to s
  char *os;

  os = s;
  //loop continues until '\0' is copied from t to s
  //because '\0' is equivalent to 0
  while((*s++ = *t++) != 0)
    ;
  return os;  //return pointer to the original starting point of destination string s
}

int
strcmp(const char *p, const char *q)
{
  //also check if *p is '\0'
  while(*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
  //return 0 if equal, positive if p > q, negative if p < q
}

uint
strlen(const char *s)
{
  int n;
  //s[n] is true as long as s[n] is not '\0'
  for(n = 0; s[n]; n++)
    ;
  return n;
}

//memset initialize a block of memory to a specific value
void*
memset(void *dst, int c, uint n)
{
  //cdst essentially cover the area you want to change as an array
  char *cdst = (char *) dst;
  int i;
  for(i = 0; i < n; i++){
    cdst[i] = c;
  }
  return dst;
}

//serach for the first occurance of c in the string s
//return a pointer to that position
char*
strchr(const char *s, char c)
{
  for(; *s; s++)
    if(*s == c)
      return (char*)s;
  return 0;
}

//reading a line of input from standard input(file descriptor 0)
char*
gets(char *buf, int max)
{
  int i, cc;
  char c;

  for(i=0; i+1 < max; ){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    buf[i++] = c;
    if(c == '\n' || c == '\r')
      break;
  }
  buf[i] = '\0';
  return buf;
}

//retrieve info about a file and store it in a 'struct stat'
int
stat(const char *n, struct stat *st)
{
  int fd;
  int r;

  fd = open(n, O_RDONLY);
  if(fd < 0)
    return -1;
  r = fstat(fd, st);  //use fstat system call to get status of the file
  close(fd);
  return r;
}

//convert a string of digits into an int
//atoi means "ASCII to Integer"
int
atoi(const char *s)
{
  int n;

  n = 0;
  while('0' <= *s && *s <= '9')
    n = n*10 + *s++ - '0';
  return n;
}

//copies n byte from vsrc to vdst
void*
memmove(void *vdst, const void *vsrc, int n)
{
  char *dst;
  const char *src;

  dst = vdst;
  src = vsrc;
  if (src > dst) {  //src and des doesn't overlap
    while(n-- > 0)
      *dst++ = *src++;
  } else {  //they overlap
    //then starts copying from the end where the data is not yet overwritten
    dst += n;
    src += n;
    while(n-- > 0)
      *--dst = *--src;
  }
  return vdst;
}

//compares the first n byte of the memory area s1 and s2
int
memcmp(const void *s1, const void *s2, uint n)
{
  const char *p1 = s1, *p2 = s2;
  while (n-- > 0) {
    if (*p1 != *p2) {
      return *p1 - *p2;
    }
    p1++;
    p2++;
  }
  return 0;
}

//why do you still need this when you already have memmove?
void *
memcpy(void *dst, const void *src, uint n)
{
  return memmove(dst, src, n);
}
