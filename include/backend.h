#ifndef _BACKEND_H__
#define _BACKEND_H__
#include "S17FS.h"
#include <block_store.h>
#include <bitmap.h>
#include <stdint.h>
#include <sys/types.h>

/***************Constants**************/

#define FS_FNAME_MAX (64)
#define DIR_REC_MAX (7)

#define FS_PATH_MAX (16322)
#define DESCRIPTOR_MAX (256)
#define BLOCK_SIZE (512)
#define INODE_BLOCK_TOTAL (32)
#define INODES_PER_BLOCK (BLOCK_SIZE / sizeof(inode_t))

#define INODE_TOTAL (((INODE_BLOCK_TOTAL) * (BLOCK_SIZE)) / sizeof(inode_t))
#define INODE_BLOCK_OFFSET (0)
#define DATA_BLOCK_OFFSET ((INODE_BLOCK_OFFSET) + (INODE_BLOCK_TOTAL))

#define ROOT_DIR_BLOCK (DATA_BLOCK_OFFSET)
#define INODE_PTR_TOTAL (8)
#define DIRECT_TOTAL (5)
#define INDIRECT_TOTAL 2*((BLOCK_SIZE) / sizeof(block_ptr_t))
#define DBL_INDIRECT_TOTAL ((INDIRECT_TOTAL) * (INDIRECT_TOTAL))
#define FILE_SIZE_MAX ((DIRECT_TOTAL + INDIRECT_TOTAL + DBL_INDIRECT_TOTAL) * BLOCK_SIZE)
#define DATA_BLOCK_MAX (65536)

#define INODE_TO_BLOCK(inode) (((inode)) + INODE_BLOCK_OFFSET)

#define INODE_INNER_IDX(inode) ((inode) &0x07)
#define INODE_INNER_OFFSET(inode) (INODE_INNER_IDX(inode) * sizeof(inode_t))

#define FILE_RECORD_POS(offset) (offset * sizeof(file_record_t))

#define DIRECT_PER_BLOCK (BLOCK_SIZE / sizeof(block_ptr_t))

#define BITMAP_BITS (DATA_BLOCK_MAX - ((DATA_BLOCK_MAX / 8) / BLOCK_SIZE))

/***************Structs & Typedefs**************/

typedef enum { DIRECT = 0, INDIRECT1 = 5, INDIRECT2 = 6, DBL_INDIRECT = 7 } START_PTR_t;

typedef uint8_t data_block_t[BLOCK_SIZE];  // c is weird
typedef uint16_t block_ptr_t;
typedef uint8_t inode_ptr_t;
typedef struct {
    uint32_t size;    // Probably all I'll use for directory file metadata
    uint32_t record_count;    
    uint32_t self_inode_num;  
    uint32_t a_time;  // access
    uint32_t m_time;  // modificatione INODE_TO_BLOCK(inode) (((inode)) + INODE_BLOCK_OFFSET)

    inode_ptr_t parent;  // SO NICE TO HAVE. You'll be so mad if you didn't think of it, too
    uint8_t type;
    uint8_t padding[26];
} mdata_t;

typedef struct {
    //char fname[FS_FNAME_MAX];
    mdata_t mdata;
    block_ptr_t data_ptrs[8];
} inode_t;

typedef struct {
    bitmap_t *fd_status;
    size_t fd_pos[DESCRIPTOR_MAX];
    inode_ptr_t fd_inode[DESCRIPTOR_MAX];
} fd_table_t;

struct S17FS {
    block_store_t *bs;
    fd_table_t fd_table;
    bitmap_t *inode_bitmap;
    char *origin;
};

/***************Function Prototypes**************/

bool remove_files_file_descriptors(S17FS_t *const fs, const inode_ptr_t inode_number);
bool read_inode(const S17FS_t *fs, void *data, const inode_ptr_t inode_number);
bool fd_valid(const S17FS_t *const fs, int fd);
bool initialize_indirect_block(S17FS_t *fs, const block_ptr_t block);
file_record_t* get_dir_contents(S17FS_t *fs, const block_ptr_t block);
inode_t* get_root_dir(S17FS_t *fs);
inode_t* get_dir(S17FS_t *fs, const inode_ptr_t inode_num);
bool write_record(S17FS_t *fs, const void *data, const block_ptr_t, const uint8_t offset);
inode_t* get_inode(S17FS_t *fs, const inode_ptr_t inode_number);
bool write_inode(S17FS_t *fs, const void *data, const inode_ptr_t inode_number);
bool write_root_inode(S17FS_t *fs, const void *data, const inode_ptr_t inode_number);
bool write_S17FS_to_block_store(S17FS_t *fs);
S17FS_t *ready_file(const char *path, const bool format);

#endif
