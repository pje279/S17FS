#include "backend.h"
#include <backend.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

/***************Functions***************/

/**********************************************************/

bool remove_files_file_descriptors(S17FS_t *const fs, const inode_ptr_t inode_number)
{
    if (fs)
    {
        for (int i = 0; i < DESCRIPTOR_MAX; i++)
        {
            if (fs->fd_table.fd_inode[i] == inode_number)
            {
                fs->fd_table.fd_inode[i] = 0;
                fs->fd_table.fd_pos[i] = 0;
                bitmap_reset(fs->fd_table.fd_status, i);
            } //End 
        } //End 

        return true;
    } //End 
    return false;
} //End 

/**********************************************************/

bool read_inode(const S17FS_t *fs, void *data, const inode_ptr_t inode_number)
{
    if (fs && data)
    {
        inode_t buffer[INODES_PER_BLOCK];
        if (block_store_read(fs->bs, INODE_TO_BLOCK(inode_number), buffer))
        {
            memcpy(data, &buffer[INODE_INNER_IDX(inode_number)], sizeof(inode_t));
            return true;
        }
    }
    return false;
}


/**********************************************************/

bool fd_valid(const S17FS_t *const fs, int fd)
{
    return fs && fd >= 0 && fd < DESCRIPTOR_MAX && bitmap_test(fs->fd_table.fd_status, fd);
}

/**********************************************************/

bool initialize_indirect_block(S17FS_t* fs, const block_ptr_t block)
{
    if (fs && block > 32)
    {
        data_block_t buffer;

        if (block_store_read(fs->bs, block, buffer))
        {
            uint8_t init_val = 0;
            for (size_t i = 0; i < BLOCK_SIZE/sizeof(uint8_t); i++)
            {
                buffer[i] = init_val;
            } //End 
            //memset(buffer, 0, BLOCK_SIZE);
            return block_store_write(fs->bs, block, buffer);
        } //End 
    } //End 

    return false;
} //End 

/**********************************************************/

file_record_t* get_dir_contents(S17FS_t* fs, const block_ptr_t block)
{
    if (fs)
    {
        file_record_t* dir_contents = (file_record_t *)malloc(sizeof(data_block_t));
        if (dir_contents)
        {
            data_block_t buffer;

            if (block_store_read(fs->bs, block, buffer))
            {
                memcpy(dir_contents, buffer, BLOCK_SIZE);

                return dir_contents;
            } //End 
            free(dir_contents);
            return NULL;
        } //End 
        return NULL;
    } //End 

    return NULL;
} //End 

/**********************************************************/

inode_t* get_root_dir(S17FS_t* fs)
{
    //void* root_block = (data_block_t *)malloc(sizeof(data_block_t));
    inode_t* root_block = (inode_t *)malloc(sizeof(inode_t));
    if (root_block == NULL)
    {
        return NULL;
    } //End 

    inode_t buffer[INODES_PER_BLOCK];

    if (block_store_read(fs->bs, 0, buffer) <= 0)
    {
        free(root_block);
        return NULL;
    } //End 

    memcpy(root_block, buffer, sizeof(inode_t));

    return (inode_t *)root_block;
} //End 

/**********************************************************/

inode_t* get_dir(S17FS_t* fs, const inode_ptr_t inode_num)
{
    //void* inode_block = (data_block_t *)malloc(sizeof(data_block_t));
    inode_t* dir = (inode_t *)malloc(sizeof(inode_t));
    if (dir == NULL)
    {
        return NULL;
    } //End 

    inode_t buffer[INODES_PER_BLOCK];

    //Determine the inode block, and offset
    size_t block = inode_num / (BLOCK_SIZE / sizeof(inode_t)) + 1; //The inode blocks are blocks 1-32, thus the + 1
    size_t offset = inode_num % (BLOCK_SIZE / sizeof(inode_t));

    //printf("\nget_dir: inum = %u -- block = %zu -- offset = %zu\n", inode_num, block, offset);

    if (block_store_read(fs->bs, block, buffer) <= 0)
    {
        free(dir);
        return NULL;
    } //End 

    //Copy over the specific inode from the block of inodes
    memcpy(dir, &buffer[offset], sizeof(inode_t));

    return dir;
    //return (inode_t *)inode_block;
} //End 

/**********************************************************/

bool write_record(S17FS_t *fs, const void *data, const block_ptr_t block_num, const uint8_t offset)
{
    if (fs && data)
    {
        //printf("\nwrite_record: block = %u -- offset = %u\n", block_num, offset);

        ///*
        data_block_t buffer;
        if (block_store_read(fs->bs, block_num, buffer))
        {
            memcpy(&buffer[offset * sizeof(file_record_t)], data, sizeof(file_record_t));
            return block_store_write(fs->bs, block_num, buffer);
        } //End 
        // */
        //return true;
    } //End 
    return false;
} //End 

/**********************************************************/

inode_t* get_inode(S17FS_t *fs, const inode_ptr_t inode_number)
{
    if (fs)
    {
        inode_t buffer[INODES_PER_BLOCK];

        //Determine the inode block, and offset
        size_t block = inode_number / (BLOCK_SIZE / sizeof(inode_t)) + 1; //The inode blocks are blocks 1-32, thus the + 1
        size_t offset = inode_number % (BLOCK_SIZE / sizeof(inode_t)); 

        //printf("get_inode: inum = %u -- block = %zu -- offset = %zu\n", inode_number, block, offset);

        if (block_store_read(fs->bs, block, buffer)) 
        {
            //printf("\nbuffer[offset]: self_inum = %u - data_ptrs[0] = %u\n", buffer[offset].mdata.self_inode_num, buffer[offset].data_ptrs[0]);
            inode_t* inode = (inode_t *)malloc(sizeof(inode_t));
            if (inode)
            {
                memcpy(inode, &(buffer[offset]), sizeof(inode_t));
                //printf("inode[offset]: self_inum = %u - data_ptrs[0] = %u\n", inode->mdata.self_inode_num, inode->data_ptrs[0]);
                return inode;
            } //End 
        } //End 
    } //End 

    return NULL;
} //End 

/**********************************************************/

bool write_inode(S17FS_t *fs, const void *data, const inode_ptr_t inode_number)
{
    if (fs && data)
    {
        inode_t buffer[INODES_PER_BLOCK];

        //Determine the inode block, and offset
        size_t block = inode_number / (BLOCK_SIZE / sizeof(inode_t)) + 1; //The inode blocks are blocks 1-32, thus the + 1
        size_t offset = inode_number % (BLOCK_SIZE / sizeof(inode_t)); 

        //printf("write_inode: inum = %u -- block = %zu -- offset = %zu -- record_count = %u\n", inode_number, block, offset, ((inode_t *)data)->mdata.record_count);
        ///*
        if (block_store_read(fs->bs, block, buffer)) 
        {
            memcpy(&(buffer[offset]), data, sizeof(inode_t));
            return block_store_write(fs->bs, block, buffer);
        }
        //*/
        //return true;
    }
    return false;
}

/**********************************************************/

bool write_root_inode(S17FS_t *fs, const void *data, const inode_ptr_t inode_number)
{
    if (fs && data) {
        inode_t buffer[INODES_PER_BLOCK];

        //Determine the inode block, and offset
        //size_t block = inode_number / (BLOCK_SIZE / sizeof(inode_t)); //The inode blocks are blocks 1-32, thus the + 1
        //size_t offset = inode_number % (BLOCK_SIZE / sizeof(inode_t)); 
        if (inode_number)
        {
        }

        if (block_store_read(fs->bs, 0, buffer)) 
        {
            memcpy(buffer, data, sizeof(inode_t));
            return block_store_write(fs->bs, 0, buffer);
        }
    }
    return false;
}

/**********************************************************/

bool write_S17FS_to_block_store(S17FS_t *fs)
{
    if (fs)
    {
        inode_t buffer[INODES_PER_BLOCK];

        if (block_store_read(fs->bs, 0, buffer)) 
        {
            memcpy(&buffer[1], bitmap_export(fs->inode_bitmap), bitmap_get_bytes(fs->inode_bitmap));
            return block_store_write(fs->bs, 0, buffer);
        }
    }
    return false;
} //End 

/**********************************************************/

bool load_S17FS(S17FS_t *fs)
{
    if (fs)
    {
        inode_t buffer[INODES_PER_BLOCK];

        if (block_store_read(fs->bs, 0, buffer)) 
        {
            //memcpy(&buffer[1], bitmap_export(fs->inode_bitmap), bitmap_get_bytes(fs->inode_bitmap));
            //fs->inode_bitmap = bitmap_overlay(INODE_TOTAL);
            fs->inode_bitmap = bitmap_import(INODE_TOTAL, &buffer[1]);

            if (fs->inode_bitmap)
            {
                return true;
            } //End 
            //return block_store_write(fs->bs, 0, buffer);
        } //End 
    } //End 
    return false;
} //End 

/**********************************************************/

S17FS_t *ready_file(const char *path, const bool format) {
    S17FS_t *fs = (S17FS_t *) calloc(1, sizeof(S17FS_t));
    if (fs)
    {
        fs->origin = (char *)malloc(strlen(path) + 1);
        if (fs->origin)
        {
            memcpy(fs->origin, path, strlen(path)+1);
        } //End 
        //printf("\nOrigin: %s\n", fs->origin);

        if (format)
        {


            fs->bs = block_store_create(path);

            if (fs->bs)
            {
                bool valid = true;
                for (int i = INODE_BLOCK_OFFSET; i < (DATA_BLOCK_OFFSET+2) && valid; ++i)
                {
                    //printf("i: %d\n", i);
                    valid &= block_store_request(fs->bs, i);
                } //End 

                if (valid)
                {
                    uint32_t right_now = time(NULL);
                    inode_t root_inode = {
                        {1, 0, 0, right_now, right_now, 0, FS_DIRECTORY, {0}},
                        {DATA_BLOCK_OFFSET+1, 0, 0, 0, 0, 0, 0, 0}};
                    valid &= write_root_inode(fs, &root_inode, 0);
                    //bitmap_set(fs->bs, 0);
                } //End 

                if (!valid)
                {
                    block_store_destroy(fs->bs);
                    fs->bs = NULL;
                } //End 
            } //End 


            fs->inode_bitmap = bitmap_create(INODE_TOTAL);

            if (fs->inode_bitmap)
            {
                bitmap_format(fs->inode_bitmap, 0);
                bitmap_set(fs->inode_bitmap, 0);
            } //End
        } //End 
        else
        {
            fs->bs = block_store_open(path);
            //fs->bs = block_store_deserialize(path);
            if (!load_S17FS(fs))
            {
                block_store_destroy(fs->bs);
                fs->bs = NULL;
            } //End 
        } //End else

        if (fs->bs)
        {
            fs->fd_table.fd_status = bitmap_create(DESCRIPTOR_MAX);
            if (fs->fd_table.fd_status)
            {
                return fs;
            } //End 
        } //End 
        free(fs);
    } //End 

    return NULL;
}
