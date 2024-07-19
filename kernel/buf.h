struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;    //device
  uint blockno; //block number
  struct sleeplock lock;  //protects reads and writes of the block’s buffered content
  uint refcnt;  //buffer in use?
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};

