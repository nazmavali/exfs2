/* exfs2.h - Header file for ExFS2 File System */
#ifndef EXFS2_H
#define EXFS2_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

/* Constants */
#define SEGMENT_SIZE (1024 * 1024)  /* 1MB segment size */
#define BLOCK_SIZE 4096            /* 4KB block size */
#define MAX_FILENAME 256
#define MAX_PATH 1024

/* Segment types */
#define INODE_SEGMENT 0
#define DATA_SEGMENT 1

/* Inode types */
#define INODE_FREE 0
#define INODE_FILE 1
#define INODE_DIR 2

#define ROOT_DIR_INODE 0
#define MAX_DIRECT_BLOCKS 1017

#define INODE_SEG_PREFIX "inode_seg_"
#define DATA_SEG_PREFIX "data_seg_"

/* Structures */
typedef struct {
    int type;                    /* 0: free, 1: file, 2: directory */
    size_t size;                 /* size in bytes */
    int num_direct;              /* number of direct blocks in use */
    int direct_blocks[MAX_DIRECT_BLOCKS]; /* direct block pointers */
    int indirect_block;          /* single indirect block pointer */
    int double_indirect_block;   /* double indirect block pointer */
    int triple_indirect_block;   /* triple indirect block pointer */
} inode_t;

typedef struct {
    char name[MAX_FILENAME];     /* filename */
    int inode_num;               /* inode number (-1 if free entry) */
} dir_entry_t;

/* Basic segment operations */
FILE* open_segment(int segment_number, int segment_type, const char* mode);
int create_new_segment(int segment_number, int segment_type);

/* Bitmap operations */
int read_bitmap(FILE* fp, uint8_t* bitmap, int size);
int write_bitmap(FILE* fp, uint8_t* bitmap, int size);
int find_free_bit(uint8_t* bitmap, int num_bits);
void set_bit(uint8_t* bitmap, int bit);
void clear_bit(uint8_t* bitmap, int bit);

/* Inode operations */
int allocate_inode();
int read_inode(int inode_num, inode_t* out_inode);
int write_inode(int inode_num, inode_t* in_inode);
int free_inode(int inode_num);

/* Block operations */
int allocate_block();
int read_block(int block_id, void* buffer);
int write_block(int block_id, void* buffer);
int free_block(int block_id);

/* Directory operations */
#define DIR_ENTRIES_PER_BLOCK ((unsigned int)(BLOCK_SIZE / sizeof(dir_entry_t)))
int load_directory_entries(int block_id, dir_entry_t* entries);
int save_directory_entries(int block_id, dir_entry_t* entries);
int find_entry_in_dir(inode_t* dir_inode, const char* name);
int add_entry_to_dir(inode_t* dir_inode, int dir_inode_num, const char* name, int child_inode_num);

/* Main command functions */
void exfs2_init();
void exfs2_add(const char* exfs2_path, const char* local_file);
void exfs2_list();
void exfs2_remove(const char* exfs2_path);
void exfs2_extract(const char* exfs2_path);
void exfs2_debug(const char* exfs2_path);

/* Utility functions */
int split_path(const char* path, char parts[][MAX_FILENAME], int* count);
int create_directories_for_path(const char* path);

#endif /* EXFS2_H */