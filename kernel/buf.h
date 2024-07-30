struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
//  struct buf *prev; // LRU cache list - lab8-2
  struct buf *next;     // 哈希链表中的下一个缓冲区
  uchar data[BSIZE];
  uint timestamp;   // 最后使用时间戳
};
