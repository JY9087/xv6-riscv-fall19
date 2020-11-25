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
#include <stddef.h>

#define NBUCKETS 13

int Hash(int x){
    return x%NBUCKETS;
}

//13个
struct {
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];
  struct buf hashbucket[NBUCKETS];
} bcache;

//根据blockno进行Hash
//在装填Cache时根据序号放入Bucket
//选择一块Buffer : 根据Hash值+13*n


//head本身就是buf结构
//按我的理解来写链表
//head的next指向最后一个，初始化为自己;prev指向自己
//普通buffer的next指向NULL，prev指向上一个：也就是head的next

void
binit(void)
{
    int i;
    for(i=0;i<NBUCKETS;i++){
        initlock(&bcache.lock[i], "bcache");
        bcache.hashbucket[i].prev = &bcache.hashbucket[i];
        bcache.hashbucket[i].next = &bcache.hashbucket[i];
    }

    struct buf *b;
    // Create linked list of buffers
    for(b = bcache.buf; b < bcache.buf+NBUF; b++){
        initsleeplock(&b->lock, "buffer");
    }


}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock[Hash(blockno)]);

  // Is the block already cached?
  //Cached
  //用Hash 遍历
    for(b = bcache.hashbucket[Hash(blockno)].next; b != &bcache.hashbucket[Hash(blockno)]; b = b->next){
        if(b->dev == dev && b->blockno == blockno){
          b->refcnt++;
          release(&bcache.lock[Hash(blockno)]);
          acquiresleep(&b->lock);
          return b;
        }
    }

  // Not cached; recycle an unused buffer.
  //这里不需要优化了。直接遍历吧。反正也不增加计数器
  //在这里要修改指针，加入bucket
  int i;
  for(i=0;i<NBUCKETS;i++){
    //虽然借用了其他人的Buffer,但逻辑上属于blockno
    for(b = &bcache.buf[Hash(blockno+i)];b<bcache.buf+NBUF;b+=NBUCKETS){
        if(b->refcnt == 0) {
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            b->refcnt = 1;

            b->prev = &bcache.hashbucket[Hash(blockno)];
            b->next = bcache.hashbucket[Hash(blockno)].next;
            bcache.hashbucket[Hash(blockno)].next->prev = b;
            bcache.hashbucket[Hash(blockno)].next = b;

            release(&bcache.lock[Hash(blockno)]);
            acquiresleep(&b->lock);
            return b;
        }
    }
  }

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
//virtio_disk_rw
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;
  b = bget(dev, blockno);
  //如果b是无效的：bget()返回了空白Buffer块
  if(!b->valid) {
    virtio_disk_rw(b->dev, b, 0);
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
  virtio_disk_rw(b->dev, b, 1);
}

// Release a locked buffer.
// Move to the head of the MRU list.
//Don't move.Make it disappear.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  //加锁。在释放时不能让其他进程占用
  acquire(&bcache.lock[Hash(b->blockno)]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;

    //可能我要进行一些额外操作,比如修改valid之类的
    //不过既然refcnt == 0 , 那下次它应该会被带走的
  }
  release(&bcache.lock[Hash(b->blockno)]);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock[Hash(b->blockno)]);
  b->refcnt++;
  release(&bcache.lock[Hash(b->blockno)]);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock[Hash(b->blockno)]);
  b->refcnt--;
  release(&bcache.lock[Hash(b->blockno)]);
}


