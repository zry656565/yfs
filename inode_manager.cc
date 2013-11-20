#include "inode_manager.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

// disk layer -----------------------------------------

disk::disk()
{
  bzero(blocks, sizeof(blocks));
}

void
disk::read_block(blockid_t id, char *buf)
{
  if (id < 0 || id >= BLOCK_NUM || buf == NULL)
    return;

  memcpy(buf, blocks[id], BLOCK_SIZE);
}

void
disk::write_block(blockid_t id, const char *buf)
{
  if (id < 0 || id >= BLOCK_NUM || buf == NULL)
    return;

  memcpy(blocks[id], buf, BLOCK_SIZE);
}

// block layer -----------------------------------------

// Allocate a free disk block.
blockid_t
block_manager::alloc_block()
{
  /*
   * your lab1 code goes here.
   * note: you should mark the corresponding bit in block bitmap when alloc.
   * you need to think about which block you can start to be allocated.
   */
  
  blockid_t id;
  char buf[BLOCK_SIZE];
  bool flag = false; //if flag == true, a free block is found;
  while (flag == false){
	id = 1.0*(BLOCK_NUM - FILE_BEGIN - 1)*rand()/RAND_MAX + FILE_BEGIN;
	bzero(buf, sizeof(buf));
	d->read_block(BBLOCK(id), buf);
	unsigned int i = (id % BPB) / 8;
	unsigned int j = (id % BPB) % 8;
	//DEBUG:
	//printf("+++ id: %d, i: %d, j: %d, ", id, i, j);
	if (((1 << (7-j)) & buf[i]) == 0) {
	  buf[i] += (1 << (7-j));
	  d->write_block(BBLOCK(id), buf);
	  flag = true;
	}
  }
  
  return id;
}

void
block_manager::free_block(uint32_t id)
{
  /* 
   * your lab1 code goes here.
   * note: you should unmark the corresponding bit in the block bitmap when free.
   */
   
   char buf[BLOCK_SIZE];
   bzero(buf, sizeof(buf));
   d->read_block(BBLOCK(id), buf);
   uint32_t i = id % BPB;
   uint32_t j = i / BLOCK_SIZE;
   uint32_t k = 7 - (i % BLOCK_SIZE);
   buf[j] -= (1 << k);
   d->write_block(BBLOCK(id), buf);
  
  return;
}

// The layout of disk should be like this:
// |<-sb->|<-free block bitmap->|<-inode table->|<-data->|
block_manager::block_manager()
{
  d = new disk();

  // format the disk
  sb.size = BLOCK_SIZE * BLOCK_NUM;
  sb.nblocks = BLOCK_NUM;
  sb.ninodes = INODE_NUM;

}

void
block_manager::read_block(uint32_t id, char *buf)
{
  d->read_block(id, buf);
}

void
block_manager::write_block(uint32_t id, const char *buf)
{
  d->write_block(id, buf);
}

// inode layer -----------------------------------------

inode_manager::inode_manager()
{
  bm = new block_manager();
  uint32_t root_dir = alloc_inode(extent_protocol::T_DIR);
  if (root_dir != 1) {
    printf("\tim: error! alloc first inode %d, should be 1\n", root_dir);
    exit(0);
  }
}

/* Create a new file.
 * Return its inum. */
uint32_t
inode_manager::alloc_inode(uint32_t type)
{
  /* 
   * your lab1 code goes here.
   * note: the normal inode block should begin from the 2nd inode block.
   * the 1st is used for root_dir, see inode_manager::inode_manager().
   */
  
  struct inode* ino;
  ino = (struct inode*)malloc(sizeof(struct inode));
  ino->type = type;
  ino->size = 0;
  ino->atime = (unsigned int)time(0);
  ino->mtime = (unsigned int)time(0);
  ino->ctime = (unsigned int)time(0);
  for (uint32_t i = 1; i <= INODE_NUM; i++) {
    if (inode_empty(i)) {
	  put_inode(i, ino);
	  return i;
	}
  }
  return 0;
}

bool
inode_manager::inode_empty(uint32_t inum)
{
  struct inode *ino_disk;
  char buf[BLOCK_SIZE];

  if (inum < 0 || inum >= INODE_NUM) {
    printf("\tim: inum out of range\n");
    return false;
  }

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);

  ino_disk = (struct inode*)buf + inum%IPB;
  if (ino_disk->type == 0) {
    return true;
  }

  return false;
}

void
inode_manager::free_inode(uint32_t inum)
{
  /* 
   * your lab1 code goes here.
   * note: you need to check if the inode is already a freed one;
   * if not, clear it, and remember to write back to disk.
   */
   
  struct inode* ino;
  if (!inode_empty(inum)){
    ino = get_inode(inum);
    ino->type = 0;
    put_inode(inum, ino);
  }

  return;
}


/* Return an inode structure by inum, NULL otherwise.
 * Caller should release the memory. */
struct inode* 
inode_manager::get_inode(uint32_t inum)
{
  struct inode *ino, *ino_disk;
  char buf[BLOCK_SIZE];

  printf("\tim: get_inode %d\n", inum);

  if (inum < 0 || inum >= INODE_NUM) {
    printf("\tim: inum out of range\n");
    return NULL;
  }

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  // printf("%s:%d\n", __FILE__, __LINE__);

  ino_disk = (struct inode*)buf + inum%IPB;
//DEBUG
//printf("+++ ino->size: %d, ino->atime: %d\n", ino_disk->size, ino_disk->atime);
  if (ino_disk->type == 0) {
    printf("\tim: inode not exist\n");
    return NULL;
  }

  ino = (struct inode*)malloc(sizeof(struct inode));
  ino_disk->atime = time(0);
  *ino = *ino_disk;
  
  bm->write_block(IBLOCK(inum, bm->sb.nblocks), buf);

  return ino;
}

void
inode_manager::put_inode(uint32_t inum, struct inode *ino)
{
  char buf[BLOCK_SIZE];
  struct inode *ino_disk;

  printf("\tim: put_inode %d\n", inum);
  if (ino == NULL)
    return;

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  ino_disk = (struct inode*)buf + inum%IPB;
  *ino_disk = *ino;
  ino_disk->mtime = time(0);
  ino_disk->ctime = time(0);
  bm->write_block(IBLOCK(inum, bm->sb.nblocks), buf);
}

#define MIN(a,b) ((a)<(b) ? (a) : (b))

/* Get all the data of a file by inum. 
 * Return alloced data, should be freed by caller. */
void
inode_manager::read_file(uint32_t inum, char **buf_out, int *size)
{
  /*
   * your lab1 code goes here.
   * note: read blocks related to inode number inum,
   * and copy them to buf_Out
   */
  
  struct inode *ino = get_inode(inum);
  
  *size = 0;
  char buf[BLOCK_SIZE];
  *buf_out = (char *)malloc(ino->size+1);
  uint32_t cpy_size = BLOCK_SIZE;
  for (uint32_t i = 0; i < NDIRECT; i++) {
    if (ino->size - *size <= 0) {
	  return;
	}
	bzero(buf, sizeof(buf));
	bm->read_block(ino->blocks[i], buf);
	if (ino->size - *size < BLOCK_SIZE) {
	  cpy_size = ino->size - *size;
	}
	memcpy(*buf_out + *size, buf, cpy_size);
	*size += cpy_size;
  }
  
  blockid_t blocks[NINDIRECT];
  if (ino->size - *size > 0) {
    bzero(buf, sizeof(buf));
    bm->read_block(ino->blocks[NDIRECT], buf);
    memcpy(blocks, buf, BLOCK_SIZE);
	//deal with indirect blocks
    for (uint32_t i = 0; i < NINDIRECT; i++){
	  if (ino->size - *size <= 0) {
	    return;
	  }
	  bzero(buf, sizeof(buf));
      bm->read_block(blocks[i], buf);
	  if (ino->size - *size < BLOCK_SIZE) {
	    cpy_size = ino->size - *size;
	  }
	  memcpy(*buf_out + *size, buf, cpy_size);
	  *size += cpy_size;
    }
  }
  
  return;
}

/* alloc/free blocks if needed */
void
inode_manager::write_file(uint32_t inum, const char *buf, int size)
{
  /*
   * your lab1 code goes here.
   * note: write buf to blocks of inode inum.
   * you need to consider the situation when the size of buf 
   * is larger or smaller than the size of original inode
   */
   
  if ((unsigned int)size > MAXFILE*BLOCK_SIZE) {
    printf("Write buffer is too much.");
	return;
  }
  
  struct inode *ino = get_inode(inum);
  blockid_t blocks[NINDIRECT];
  char buffer[BLOCK_SIZE];
  //DEBUG
  //printf("+++ Write:%s \n+++ Size: %d\n+++ ino->size: %d\n", buf, size, ino->size);
  
  if (ino->size == 0 && size > 0) {
    ino->blocks[0] = bm->alloc_block();
	
    // if (inum == 2) {
	  // printf("+++ ino->blocks[0] = %d\n", ino->blocks[0]);
	// }
	//DEBUG
	//printf("+++ Alloc: ino->blocks[0] = %d\n", ino->blocks[0]);
  }
  
  if (ino->size < (unsigned int)size) { //alloc
    if (ino->size/BLOCK_SIZE < 32) {
      unsigned int directMax = (size/BLOCK_SIZE > 32 ? 32 : size/BLOCK_SIZE);
	  for (unsigned int i = ino->size/BLOCK_SIZE + 1; i <= directMax; i++) {
	    ino->blocks[i] = bm->alloc_block();
	  }
	}
    if (size/BLOCK_SIZE > 32) {
	  unsigned int indirectMin = (ino->size/BLOCK_SIZE > 32 ? ino->size/BLOCK_SIZE - 32 : 0);
	  if (ino->size/BLOCK_SIZE <= 32) {
	    ino->blocks[NDIRECT] = bm->alloc_block();
	  } else {
	    bzero(buffer, sizeof(buffer));
	    bm->read_block(ino->blocks[NDIRECT], buffer);
        memcpy(blocks, buffer, BLOCK_SIZE);
	  }
	  for (unsigned int i = indirectMin; i <= (unsigned int)size/BLOCK_SIZE; i++) {
		blocks[i] = bm->alloc_block();
	  }
	  memcpy(buffer, blocks, BLOCK_SIZE);
	  bm->write_block(ino->blocks[NDIRECT], buffer);
	}
  }
  else { //free
    if (size/BLOCK_SIZE < 32) {
      unsigned int directMax = (ino->size/BLOCK_SIZE > 32 ? 32 : ino->size/BLOCK_SIZE);
	  for (unsigned int i = size/BLOCK_SIZE + 1; i <= directMax; i++) {
	     bm->free_block(ino->blocks[i]);
	  }
	}
    if (ino->size/BLOCK_SIZE > 32) {
	  unsigned int indirectMin = (size/BLOCK_SIZE > 32 ? size/BLOCK_SIZE - 32 : 0);
	  bzero(buffer, sizeof(buffer));
	  bm->read_block(ino->blocks[NDIRECT], buffer);
      memcpy(blocks, buffer, BLOCK_SIZE);
	  for (unsigned int i = indirectMin; i <= ino->size/BLOCK_SIZE; i++) {
		bm->free_block(blocks[i]);
	  }
	  if (size/BLOCK_SIZE <= 32) {
	    bm->free_block(ino->blocks[NDIRECT]);
	  }
	}
  }
  
  int currentSize = 0;
  uint32_t cpy_size = BLOCK_SIZE;
  for (uint32_t i = 0; i < NDIRECT; i++) {
    if (size - currentSize <= 0) {
	  break;
	}
	if (size - currentSize < BLOCK_SIZE) {
	  cpy_size = size - currentSize;
	}
	//DEBUG:
	//printf("+++ cpy_size: %d\n", cpy_size);
	memcpy(buffer, buf + currentSize, cpy_size);
	bm->write_block(ino->blocks[i], buffer);
	currentSize += cpy_size;
  }
  
  if (size - currentSize > 0) {
	//deal with indirect blocks
    for (uint32_t i = 0; i < NINDIRECT; i++){
	  if (size - currentSize <= 0) {
	    break;
	  }
	  if (size - currentSize < BLOCK_SIZE) {
	    cpy_size = size - currentSize;
	  }
	  memcpy(buffer, buf + currentSize, cpy_size);
      bm->write_block(blocks[i], buffer);
	  currentSize += cpy_size;
    }
  }

  ino->size = size;
  //TODO: 修改更新时间
  put_inode(inum, ino);
  
  return;
}

void
inode_manager::getattr(uint32_t inum, extent_protocol::attr &a)
{
  /*
   * your lab1 code goes here.
   * note: get the attributes of inode inum.
   * you can refer to "struct attr" in extent_protocol.h
   */
  
  
  struct inode *ino;
  char buf[BLOCK_SIZE];

  if (inum < 0 || inum >= INODE_NUM) {
    printf("\tim: inum out of range\n");
    return;
  }

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  ino = (struct inode*)buf + inum%IPB;

  a.type = ino->type;
  a.size = ino->size;
  a.atime = ino->atime;
  a.mtime = ino->mtime;
  a.ctime = ino->ctime;
  
  return;
}

void
inode_manager::remove_file(uint32_t inum)
{
  /*
   * your lab1 code goes here
   * note: you need to consider about both the data block and inode of the file
   */

  struct inode* ino = get_inode(inum);
  if (ino == NULL) {
    return;
  }
  
  int rest_size = ino->size;
  for (uint32_t i = 0; i < NDIRECT; i++) {
    if (rest_size <= 0) {
	  break;
	}
	bm->free_block(ino->blocks[i]);
	rest_size -= BLOCK_SIZE;
  }
  
  blockid_t blocks[NINDIRECT];
  char buf[BLOCK_SIZE];
  if (rest_size > 0) {
    bm->read_block(ino->blocks[NDIRECT], buf);
    memcpy(blocks, buf, BLOCK_SIZE);
	//deal with indirect blocks
    for (uint32_t i = 0; i < NINDIRECT; i++){
	  if (rest_size <= 0) {
	    break;
	  }
      bm->free_block(blocks[i]);
	  rest_size -= BLOCK_SIZE;
    }
  }
  
  free_inode(inum); 
  
  return;
}
