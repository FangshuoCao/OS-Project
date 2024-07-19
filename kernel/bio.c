// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13
#define BUFHASH(dev, blockno) ((((dev)<<27)|(blockno)) % NBUCKET)

struct {
  struct buf buf[NBUF];
  struct buf bufmap[NBUCKET]; //hashmap of buf linked list

  struct spinlock maplock[NBUCKET]; //lock per list
  struct spinlock eviclock; //lock for eviction
} bcache;

void
binit(void)
{
  //initialize lock per bucket
  for(int i = 0; i < NBUCKET; i++){
    initlock(&bcache.maplock[i], "bcache_maplock");
    bcache.bufmap[i].next = 0;
  }
  
  for(int i=0;i<NBUF;i++){
    struct buf *b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    b->lastuse = 0;
    b->refcnt = 0;
    b->next = bcache.bufmap[0].next;
    bcache.bufmap[0].next = b;
  }

  initlock(&bcache.eviclock, "bcache_eviction");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint key = BUFHASH(dev, blockno);

  acquire(&bcache.maplock[key]);

  // Is the block already cached?
  for(b = bcache.bufmap[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.maplock[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // since we need to search through each bucket
  //release maplock[key] first to prevent deadlock
  release(&bcache.maplock[key]);
  //since eviction will be needed, acquire eviction lock
  //to make search and eviction atomic
  acquire(&bcache.eviclock);

  //since we just released maplock[key], we need to check again whether block is cached
  //to prevent adding the same block to cache again
  for(b = bcache.bufmap[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      acquire(&bcache.maplock[key]); //for `refcnt++`
      b->refcnt++;
      release(&bcache.maplock[key]);
      release(&bcache.eviclock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  //still not cached
  //search for a victim to evict
  //then perform eviction
  struct buf *before_least = 0; 
  uint holding_bucket = -1;
  for(int i = 0; i < NBUCKET; i++){
    acquire(&bcache.maplock[i]);
    int newfound = 0;
    for(b = &bcache.bufmap[i]; b->next; b = b->next) {
      if(b->next->refcnt == 0 && (!before_least || b->next->lastuse < before_least->next->lastuse)) {
        before_least = b;
        newfound = 1;
      }
    }
    if(!newfound) {
      release(&bcache.maplock[i]);
    } else {
      if(holding_bucket != -1) release(&bcache.maplock[holding_bucket]);
      holding_bucket = i;
      // keep holding until finished eviction
    }
  }
  if(!before_least) {
    panic("bget: no buffers");
  }
  b = before_least->next;
  
  if(holding_bucket != key) {
    // remove the buf from it's original bucket
    before_least->next = b->next;
    release(&bcache.maplock[holding_bucket]);
    // rehash and add it to the target bucket
    acquire(&bcache.maplock[key]);
    b->next = bcache.bufmap[key].next;
    bcache.bufmap[key].next = b;
  }
  
  b->dev = dev;
  b->blockno = blockno;
  b->refcnt = 1;
  b->valid = 0;
  release(&bcache.maplock[key]);
  release(&bcache.eviclock);
  acquiresleep(&b->lock);
  return b;
}

// Return a locked buf with the contents of the indicated block
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint key = BUFHASH(b->dev, b->blockno);

  acquire(&bcache.maplock[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
     b->lastuse = ticks;
  }
  
  release(&bcache.maplock[key]);
}

void
bpin(struct buf *b) {
  uint key = BUFHASH(b->dev, b->blockno);
  acquire(&bcache.maplock[key]);
  b->refcnt++;
  release(&bcache.maplock[key]);
}

void
bunpin(struct buf *b) {
  uint key = BUFHASH(b->dev, b->blockno);
  acquire(&bcache.maplock[key]);
  b->refcnt--;
  release(&bcache.maplock[key]);
}


