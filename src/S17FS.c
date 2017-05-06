/***************Includes***************/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "S17FS.h"
#include "block_store.h"
#include "bitmap.h"
#include "dyn_array.h"
#include "backend.h"

/***************Constants***************/

#define FS_NAME_MAX 64
#define FS_BLOCK_SIZE 512 //Bytes
#define FS_NUM_BLOCKS 65536 //2^16 Blocks
#define FS_INODE_TABLE_SIZE 16384 //2^14 Bytes - 32 Data blocks worth
#define FS_NUM_DIR_ENTRIES 7 //7 directory/file entries per directory
#define FS_NUM_FILE_DESCRIPTORS 256
#define FS_NUM_DIR_PTRS 5
#define FS_NUM_INDIR_PTRS 2

/***************Global Variables***************/

int overhead = 0;

/***************Functions***************/

S17FS_t *fs_format(const char *path)
{
    if(path == NULL)
    {
        return false;
    } //End if(path == NULL)
    else if(path[0] == '\0')
    {
        return false;
    } //End else if(path[0] == '\0')
    else
    {
        return ready_file(path, true);
    } //End else

} //End S17FS_t *fs_format(const char *path)

/***************************************************/

S17FS_t *fs_mount(const char *path)
{
    if(path == NULL)
    {
        return false;
    } //End  if(path == NULL)
    else if(path[0] == '\0')
    {
        return false;
    } //End else if(path[0] == '\0')
    else
    {
        return ready_file(path, false);
    } //End else

} //End S17FS_t *fs_mount(const char *path)

/***************************************************/

int fs_unmount(S17FS_t *fs)
{
    if (fs)
    {
        write_S17FS_to_block_store(fs);
        //block_store_serialize(fs->bs, fs->origin);

        block_store_destroy(fs->bs);
        bitmap_destroy(fs->fd_table.fd_status);
        bitmap_destroy(fs->inode_bitmap);
        free(fs->origin);
        free(fs);
        return 0;
    } //End if (fs)
    else
    {
        return -1;
    } //End else

} //End int fs_unmount(S17FS_t *fs)

/***************************************************/

int fs_create(S17FS_t *fs, const char *path, file_t type)
{
    //printf("----------------------------------------------\n"); 
    //printf("Path: %s\n", path);

    //Check that the parameters are valid
    if (fs == NULL || path == NULL  || (strcmp(path, "") == 0) || (type != FS_REGULAR && type != FS_DIRECTORY) || strlen(path) >= FS_NAME_MAX || path[0] != '/' || path[strlen(path)-1] == '/')
    {
        return -1;
    } //End if (fs == NULL || path == NULL  || (strcmp(path, "") == 0) || (type != FS_REGULAR && type != FS_DIRECTORY) || strlen(path) >= FS_NAME_MAX || path[0] != '/' || path[strlen(path)-1] == '/')

    //Get the root directory
    inode_t* root = get_root_dir(fs);
    //printf("\nroot->data_ptrs[0] == %u", root->data_ptrs[0]);

    if (root == NULL || (root->data_ptrs[0] <= 32 && root->data_ptrs[0] > 0))
    {
        //printf("\nroot == %p", root);
        //printf("\nroot->data_ptrs[0] == %u", root->data_ptrs[0]);
        //printf("\nReturn 1\n");
        free(root);
        return -1;
    } //End if (root == NULL || (root->data_ptrs[0] <= 32 && root->data_ptrs[0] > 0))


    //Get the contents of the root directory
    void* dir_contents = get_dir_contents(fs, root->data_ptrs[0]);
    if (dir_contents == NULL)
    {
        //printf("\nReturn 2\n");
        free(root);
        return -1;
    } //End if (dir_contents == NULL)

    //Determine how long the path is
    int path_depth = 0;
    for (size_t i = 0; i < strlen(path); i++)
    {
        if (path[i] == '/')
        {
            path_depth++;
        } //End if (path[i] == '/')
    } //End for (size_t i = 0; i < strlen(path); i++)
    //printf("Path depth: %d", path_depth);

    //Tokenize the path and determine if the path exists
    char* path_copy = (char *)malloc(sizeof(char)*64);
    memcpy(path_copy, path, strlen(path)+1);

    char* token = NULL;
    const char delims[2] = "/";
    token = strtok(path_copy, delims);

    inode_t* cur_dir_inode = NULL;
    if (path_depth == 1)
    {
        //printf("\n1: cur_dir_inode == root");
        cur_dir_inode = root;
    } //End if (path_depth == 1)

    int cur_depth = 1;
    int next_rec_found = 0;
    int j = 0;

    //printf("\n");
    //printf("1:Dir: %s \n",((file_record_t *)dir_contents)[j].name);

    while (token != NULL && path_depth > 1)
    {
        //printf("/%s", token);
        if(cur_depth == path_depth)
        {
            //Found where the new record should go
            break;
        } //End if(cur_depth == path_depth)

        next_rec_found = 0;

        //Compare the path with the contents of the current directory
        for (j = 0; j < DIR_REC_MAX; j++)
        {
            if (strcmp(((file_record_t *)dir_contents)[j].name, token) == 0 && ((file_record_t *)dir_contents)[j].type == FS_DIRECTORY)
            {
                //The current path was found
                //printf("I happened!\n");
                next_rec_found = 1;

                //Get the data blocks of the next part of the path
                free(cur_dir_inode);
                cur_dir_inode = get_dir(fs, ((file_record_t *)dir_contents)[j].inode_num);

                if (cur_dir_inode == NULL || (cur_dir_inode->data_ptrs[0] <= 32 && cur_dir_inode->data_ptrs[0] > 0))
                {
                    //printf("\nReturn 4: cur_dir-inode->data_ptrs[0] = %u\n", cur_dir_inode->data_ptrs[0]);
                    free(root);
                    free(dir_contents);
                    free(path_copy);
                    free(cur_dir_inode);
                    return -1;
                } //End if (cur_dir_inode == NULL || (cur_dir_inode->data_ptrs[0] <= 32 && cur_dir_inode->data_ptrs[0] > 0))

                if (block_store_read(fs->bs, cur_dir_inode->data_ptrs[0], dir_contents) <= 0)
                {
                    //printf("\nReturn 5\n");
                    free(root);
                    free(dir_contents);
                    free(path_copy);
                    free(cur_dir_inode);
                    return -1;
                } //End if (block_store_read(fs->bs, cur_dir_inode->data_ptrs[0], dir_contents) <= 0)
            } //End if (strcmp(((file_record_t *)dir_contents)[j].name, token) == 0 && ((file_record_t *)dir_contents)[j].type == FS_DIRECTORY)
        } //End for (j = 0; j < DIR_REC_MAX; j++)

        if (next_rec_found == 0)
        {
            //printf("\nReturn 6\n");
            //Directory wasn't found
            free(root);
            free(dir_contents);
            free(path_copy);
            free(cur_dir_inode);
            return -1;
        } //End if (next_rec_found == 0)

        token = strtok(NULL, delims);
        cur_depth++;
    } //End 
    //printf("\n");
    //printf("2:Dir: %s\n",((file_record_t *)dir_contents)[j].name);

    //printf("\nDir: ");
    //Check if the record already exists in the target directory
    for (int i = 0; i < DIR_REC_MAX; i++)
    {
        //printf("%s - ", ((file_record_t *)dir_contents)[i].name);
        if (strcmp(((file_record_t *)dir_contents)[i].name, token) == 0)
        {
            //printf("\nReturn 7\n");
            //Record already exists in the target directory
            free(root);
            free(dir_contents);
            free(path_copy);
            if (path_depth > 1)
            {
                free(cur_dir_inode);
            } //End if (path_depth > 1)
            return -1;
        } //End if (strcmp(((file_record_t *)dir_contents)[i].name, token) == 0)
    } //End for (int i = 0; i < DIR_REC_MAX; i++)
    //printf("\n3:Dir: %s ",((file_record_t *)dir_contents)[j].name);

    //printf("\nAfter checking for duplicates, token = %s\n", token);

    //Found the target directory, and the record doesn't already exist, awesome
    //Get and check for a new inode number
    size_t new_inode_num = bitmap_ffz(fs->inode_bitmap);
    //printf("new_inode_num == %zu\n", new_inode_num);
    if (new_inode_num > 255)
    {
        free(root);
        free(dir_contents);
        free(path_copy);
        if (path_depth > 1)
        {
            free(cur_dir_inode);
        } //End if (path_depth > 1)
        return -1;
    } //End if (new_inode_num > 255)
    //printf("\n\nNew inum: %zu", new_inode_num);
    //printf("\ndir_contents.inode_num == %u\n", ((file_record_t *)dir_contents)[j].inode_num);

    //Create a new inode for the new record
    uint32_t right_now = time(NULL);
    inode_t new_inode = {
        {0, 0, new_inode_num, right_now, right_now, ((file_record_t *)dir_contents)[j].inode_num, type, {0}},
        {0, 0, 0, 0, 0, 0, 0, 0}};

    //Find an empty data block for the new record if it is a directory
    if (type == FS_DIRECTORY)
    {
        //Get the new block number
        size_t new_data_block_num = -1;
        new_data_block_num = block_store_allocate(fs->bs);

        //printf("Data block num: %zu\n", new_data_block_num);

        if (new_data_block_num <= INODE_BLOCK_TOTAL || new_data_block_num == SIZE_MAX)
        {
            //printf("\nReturn 8\nnew data block num = %zu\n", new_data_block_num);
            //Something went wrong allocating a new data block
            free(root);
            free(dir_contents);
            free(path_copy);
            if (path_depth > 1)
            {
                free(cur_dir_inode);
            } //End if (path_depth > 1)
            return -1;
        } //End if (new_data_block_num <= INODES_BLOCK_TOTAL || new_data_block_num == SIZE_MAX)

        new_inode.data_ptrs[0] = new_data_block_num;
    } //End if (type == FS_DIRECTORY)

    //Check if their is space for a new record
    //printf("\tDir count = %u\n", cur_dir_inode->mdata.record_count);
    if (cur_dir_inode->mdata.record_count < DIR_REC_MAX)
    {
        //Yay, make the new records
        file_record_t new_record;
        new_record.inode_num = new_inode_num;
        new_record.record_count = 0;
        memcpy(&(new_record.name), token, strlen(token)+1);
        new_record.type = type;

        //printf("\nnew record: inum = %u - record count = %u - name = %s - type = %d\n", new_record.inode_num, new_record.record_count, new_record.name, new_record.type);
        //printf("\ncur_dir_inode->record_count = %u\n", cur_dir_inode->mdata.record_count);
        //printf("\nDir: %s record count before write: %u ",((file_record_t *)dir_contents)[j].name, cur_dir_inode->mdata.record_count);

        //Write it back to the block_store
        if (write_record(fs, &new_record, cur_dir_inode->data_ptrs[0], cur_dir_inode->mdata.record_count) == false)
        {
            //printf("\nReturn 9\n");
            free(root);
            free(dir_contents);
            free(path_copy);
            if (path_depth > 1)
            {
                free(cur_dir_inode);
            } //End if (path_depth > 1)
            return -1;
        } //End if (write_record(fs, &new_record, cur_dir_inode->data_ptrs[0], cur_dir_inode->mdata.record_count) == false)

        //printf("\nDir: %s record count before: %u ",((file_record_t *)dir_contents)[j].name, cur_dir_inode->mdata.record_count);
        //Added a record
        cur_dir_inode->mdata.record_count++;
        //printf("after %u\n", cur_dir_inode->mdata.record_count);

        //Are we in root?
        if (path_depth == 1)
        {
            //printf("write_root_inode\n");
            //We're in root
            //Write back the root inode to the block_store
            if (write_root_inode(fs, cur_dir_inode, cur_dir_inode->mdata.self_inode_num) == false)
            {
                //printf("\nReturn 10\n");
                free(root);
                free(dir_contents);
                free(path_copy);
                if (path_depth > 1)
                {
                    free(cur_dir_inode);
                } //End if (path_depth > 1)
                return -1;
            } //End if (write_root_inode(fs, cur_dir_inode, cur_dir_inode->mdata.self_inode_num) == false)
        } //End if (path_depth == 1)
        else 
        {
            //printf("write_inode\n");
            //Not in root
            //Write the updated inode back to block_store
            if (write_inode(fs, cur_dir_inode, cur_dir_inode->mdata.self_inode_num) == false)
            {
                //printf("\nReturn 11\n");
                free(root);
                free(dir_contents);
                free(path_copy);
                if (path_depth > 1)
                {
                    free(cur_dir_inode);
                } //End if (path_depth > 1)
                return -1;
            } //End if (write_inode(fs, cur_dir_inode, cur_dir_inode->mdata.self_inode_num) == false)
        } //End else

        //Write the new inode back to the block_store
        if (write_inode(fs, &new_inode, new_inode_num) == false)
        {
            //printf("\nReturn 12\n");
            free(root);
            free(dir_contents);
            free(path_copy);
            if (path_depth > 1)
            {
                free(cur_dir_inode);
            } //End 
            return -1;
        } //End 

        bitmap_set(fs->inode_bitmap, new_inode_num);
    } //End 
    else
    {
        //printf("\nReturn 13\n");
        free(root);
        free(dir_contents);
        free(path_copy);
        if (path_depth > 1)
        {
            free(cur_dir_inode);
        } //End 
        return -1;
    } //End else

    //printf("root->data_ptrs[0] == %u\n", root->data_ptrs[0]);

    //Clean up everything, and return happily
    free(root);
    free(dir_contents);
    free(path_copy);
    if (path_depth > 1)
    {
        free(cur_dir_inode);
    } //End 

    return 0;
} //End int fs_create(S17FS_t *fs, const char *path, file_t type)

/***************************************************/

int fs_open(S17FS_t *fs, const char *path)
{
    //Check that the parameters are valid
    if (fs == NULL || path == NULL  || (strcmp(path, "") == 0) || strlen(path) >= FS_NAME_MAX || path[0] != '/' || path[strlen(path)-1] == '/' || (strlen(path) == 1))
    {
        return -1;
    } //End
    //printf("\nPath: %s\n", path);

    //Get the root directory
    inode_t* root = get_root_dir(fs);
    //printf("\nroot->data_ptrs[0] == %u", root->data_ptrs[0]);

    if (root == NULL || (root->data_ptrs[0] <= 32 && root->data_ptrs[0] > 0))
    {
        //printf("\nroot == %p", root);
        //printf("\nroot->data_ptrs[0] == %u", root->data_ptrs[0]);
        //printf("fs_open: Return 1\n");
        free(root);
        return -1;
    } //End 

    //Get the contents of the root directory
    //void* dir_contents = (file_record_t *)malloc(sizeof(data_block_t));
    file_record_t* dir_contents = get_dir_contents(fs, root->data_ptrs[0]);
    if (dir_contents == NULL)
    {
        //printf("fs_open: Return 2\n");
        free(root);
        return -1;
    } //End 

    //Determine how long the path is
    int path_depth = 0;
    for (size_t i = 0; i < strlen(path); i++)
    {
        //printf("%c", path[i]);
        if (path[i] == '/')
        {
            path_depth++;
        } //End 
    } //End 

    //Tokenize the path and determine if the path exists
    char* path_copy = (char *)malloc(sizeof(char)*64);
    memcpy(path_copy, path, strlen(path)+1);

    char* token = NULL;
    const char delims[2] = "/";
    token = strtok(path_copy, delims);

    inode_t* cur_dir_inode = NULL;
    if (path_depth == 1)
    {
        cur_dir_inode = root;
    } //End 

    int cur_depth = 1;
    int next_rec_found = 0;
    int j = 0;

    //printf("\n%s", token);
    while (token != NULL && path_depth > 1)
    {
        //printf("/%s", token);
        if(cur_depth == path_depth)
        {
            //Found where the new record should go
            break;
        } //End 

        next_rec_found = 0;

        for (j = 0; j < DIR_REC_MAX; j++)
        {
            if (strcmp(dir_contents[j].name, token) == 0 && dir_contents[j].type == FS_DIRECTORY)
            {
                //The current path was found
                //printf("\nI happened!\n");
                next_rec_found = 1;

                //Get the data blocks of the next part of the path
                free(cur_dir_inode);
                cur_dir_inode = get_dir(fs, dir_contents[j].inode_num);

                if (cur_dir_inode == NULL || (cur_dir_inode->data_ptrs[0] <= 32 && cur_dir_inode->data_ptrs[0] > 0))
                {
                    //printf("\nReturn 4: cur_dir-inode->data_ptrs[0] = %u\n", cur_dir_inode->data_ptrs[0]);

                    free(root);
                    free(dir_contents);
                    free(path_copy);
                    free(cur_dir_inode);
                    return -1;
                } //End

                if (block_store_read(fs->bs, cur_dir_inode->data_ptrs[0], dir_contents) <= 0)
                {
                    //printf("\nReturn 5\n");

                    free(root);
                    free(dir_contents);
                    free(path_copy);
                    free(cur_dir_inode);
                    return -1;
                } //End 

            } //End 
        } //End

        if (next_rec_found == 0)
        {
            //printf("\nReturn 6\n");
            //Directory wasn't found
            free(root);
            free(dir_contents);
            free(path_copy);
            free(cur_dir_inode);
            return -1;
        } //End 

        token = strtok(NULL, delims);
        cur_depth++;
    } //End 

    //Search for the record in the target directory
    for (int i = 0; i < DIR_REC_MAX; i++)
    {
        //printf("\ndir_contents[%d].name = %s - token = %s\ndir_contents[%d].inum = %u\n", i, dir_contents[i].name, token, i, dir_contents[i].inode_num);
        if (strcmp(dir_contents[i].name, token) == 0 && dir_contents[i].type == FS_REGULAR)
        {
            //printf("\nReturn 7\n");
            //Found the record
            //Check if there is space for a new file descriptor
            size_t fd = bitmap_ffz(fs->fd_table.fd_status);

            if (fd != SIZE_MAX)
            {
                bitmap_set(fs->fd_table.fd_status, fd);
                fs->fd_table.fd_inode[fd] = dir_contents[i].inode_num;
                fs->fd_table.fd_pos[fd] = 0;

                free(root);
                free(dir_contents);
                free(path_copy);
                if (path_depth > 1)
                {
                    free(cur_dir_inode);
                } //End

                return fd;
            } //End 
        } //End 
    } //End

    free(root);
    free(dir_contents);
    free(path_copy);
    if (path_depth > 1)
    {
        free(cur_dir_inode);
    } //End 

    //printf("\nReturn 8: file not found\n");

    return -1;
} //End 

/***************************************************/

int fs_close(S17FS_t *fs, int fd)
{
    //Check that the parameters are valid
    if (fs == NULL || fd < 0 || fd >= DESCRIPTOR_MAX )
    {
        return -1;
    } //End if (fs == NULL || fd < 0)

    //Reset the bit for the given file descriptor
    if (bitmap_test(fs->fd_table.fd_status, fd))
    {
        //printf("Passed the bitmap_test\n");
        bitmap_reset(fs->fd_table.fd_status, fd);

        //Reset the given file descriptors position
        fs->fd_table.fd_pos[fd] = 0;

        //Reset the the inode the given file descriptor is associated with
        fs->fd_table.fd_inode[fd] = 0;

        return 0;
    } //End 

    return -1;
} //End 

/***************************************************/

off_t fs_seek(S17FS_t *fs, int fd, off_t offset, seek_t whence)
{
    if (fs && fd_valid(fs, fd))
    {
        inode_t file_inode;
        if (read_inode(fs, &file_inode, fs->fd_table.fd_inode[fd]))
        {
            size_t *position = fs->fd_table.fd_pos + fd;
            uint32_t *size   = &file_inode.mdata.size;
            off_t resulting_pos;
            switch (whence)
            {
                case FS_SEEK_SET:
                    if (offset <= 0)
                    {
                        *position     = 0;
                        resulting_pos = 0;
                    }   
                    else if (offset > (off_t) *size)
                    {
                        *position     = *size;
                        resulting_pos = *size;
                    }
                    else
                    {
                        *position     = offset;
                        resulting_pos = *position;
                    }
                    break;
                case FS_SEEK_CUR:
                    if (offset >= ((off_t) *size - (off_t) *position))
                    {
                        *position     = *size;
                        resulting_pos = *size;
                    }
                    else if (-offset >= (off_t) *position)
                    {
                        *position     = 0;
                        resulting_pos = 0;
                    }
                    else
                    {
                        *position += offset;
                        resulting_pos = *position;
                    } //End else
                    break;
                case FS_SEEK_END:
                    if (offset >= 0)
                    {
                        *position     = *size;
                        resulting_pos = *size;
                    }
                    else if ((offset + (off_t) *size) < 0)
                    {
                        *position     = 0;
                        resulting_pos = 0;
                    }
                    else
                    {
                        *position += offset;
                        resulting_pos = *position;
                    }
                    break;
                default:
                    resulting_pos = -1;
                    break;
            }
            return resulting_pos;
        }
    }
    return -1;
} //End 

/***************************************************/

ssize_t fs_read(S17FS_t *fs, int fd, void *dst, size_t nbyte)
{
    //Check that the parameters are valid
    if (fs == NULL || fd < 0 || dst == NULL)
    {
        //printf("fs_read: Return 1\n");
        return -1;
    } //End 

    if (!bitmap_test(fs->fd_table.fd_status, fd))
    {
        //printf("Return 2\n");
        return -1;
    } //End

    //Declare variables
    bool finished = false;
    bool initial_bytes_read = false;
    size_t total_bytes_read = 0;
    data_block_t buffer;
    uint16_t indirect_block[256];
    uint16_t dbl_indirect_block[256];
    inode_t* fd_inode = get_inode(fs, fs->fd_table.fd_inode[fd]);

    if (fd_inode == NULL)
    {
        //printf("Return 3\n");
        return -1;
    } //End 

    START_PTR_t start_index = DIRECT;

    size_t cur_pos = fs->fd_table.fd_pos[fd];
    size_t starting_block = cur_pos / BLOCK_SIZE;
    size_t indir_index = 0;
    size_t dbl_indir_index1 = 0; 
    size_t dbl_indir_index2 = 0;

    ///printf("cur_pos = %zu - starting_block = %zu\n", cur_pos, starting_block);

    //Determine which pointer the write should start in
    if (starting_block < 5)
    {
        start_index = starting_block;
    } //End 
    else if (starting_block >= 5 && starting_block < 261)
    {
        //First indirect pointer
        indir_index = (starting_block - (DIRECT_TOTAL)) % DIRECT_PER_BLOCK;
        start_index = INDIRECT1;
    } //End 
    else if (starting_block >= 261 && starting_block < 517)
    {
        //Second indirect pointer
        indir_index = (starting_block - (DIRECT_TOTAL + DIRECT_PER_BLOCK)) % DIRECT_PER_BLOCK;
        start_index = INDIRECT2;
    } //End 
    else
    {
        //Double indirect pointer
        dbl_indir_index1 = (starting_block - (DIRECT_TOTAL + INDIRECT_TOTAL)) / DIRECT_PER_BLOCK;
        dbl_indir_index2 = (starting_block - (DIRECT_TOTAL + INDIRECT_TOTAL)) % DIRECT_PER_BLOCK;
        start_index = DBL_INDIRECT;
    } //End else

    //Caclulate how many blocks are needed, as well as figure out depending on 
    //the starting position of the file descriptor how much the first and last
    //blocks needed should be read
    uint32_t num_blocks_needed = 1 + (uint32_t)((nbyte-1)/BLOCK_SIZE);
    uint32_t num_blocks_used = 0;
    uint32_t initial_bytes = BLOCK_SIZE - (cur_pos - BLOCK_SIZE * starting_block);  
    uint32_t remainder_bytes = (nbyte - initial_bytes) % BLOCK_SIZE;

    if (initial_bytes == 512)
    {
        initial_bytes = 1 + ((nbyte - 1) % BLOCK_SIZE);
        remainder_bytes = 0;
    } //End

    //printf("num_blocks_needed = %u - num_blocks_used = %u - initial_bytes = %u - remainder_bytes = %u\n", num_blocks_needed, num_blocks_used, initial_bytes, remainder_bytes);

    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t k = 0;

    for (i = start_index; i < INODE_PTR_TOTAL && total_bytes_read < nbyte && !finished && num_blocks_used <= num_blocks_needed; i++)
    {
        //printf("\ti = %u\n", i);
        if (i < 5)
        {
            //Direct Pointers
            if (fd_inode->data_ptrs[i] < 33 || fd_inode->data_ptrs[i] >= BITMAP_BITS)
            {
                //printf("\tdirect: fd_inode->data_ptrs[%u] = %u\n", i, fd_inode->data_ptrs[i]);

                fs->fd_table.fd_pos[fd] += total_bytes_read;
                write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                //printf("fs_write: Return 4\nnew data block num = %zu\n", new_data_block_num);
                //Something went wrong allocating a new data block
                free(fd_inode);
                return total_bytes_read;
            } //End if (fd_inode->data_ptrs[i] < 33)

            if (block_store_read(fs->bs, fd_inode->data_ptrs[i], buffer))
            {   
                if (!initial_bytes_read && cur_pos == 0)
                {
                    memcpy(dst, buffer, initial_bytes);
                    total_bytes_read += initial_bytes;
                    initial_bytes_read= true;
                    num_blocks_used++;
                } //End 
                else if (!initial_bytes_read && cur_pos > 0)
                {
                    memcpy(dst, &(buffer[BLOCK_SIZE - initial_bytes]), initial_bytes);
                    total_bytes_read += initial_bytes;
                    initial_bytes_read = true;
                    num_blocks_used++;
                } //End 
                else if (num_blocks_used == num_blocks_needed && remainder_bytes % BLOCK_SIZE != 0)
                {
                    memcpy(&(((uint8_t *)dst)[total_bytes_read]), buffer, remainder_bytes);
                    total_bytes_read += remainder_bytes;
                    //printf("\n1: total_bytes_read = %zu\n", total_bytes_read);
                    num_blocks_used++;
                }
                else
                {
                    memcpy(&(((uint8_t *)dst)[total_bytes_read]), buffer, BLOCK_SIZE);
                    total_bytes_read += BLOCK_SIZE;
                    //printf("\n2: total_bytes_read = %zu\n", total_bytes_read);
                    num_blocks_used++;

                    if (total_bytes_read == nbyte)
                    {
                        //printf("1: I happened------------------------------------------------------------------------\n");
                        finished = true;
                    } //End
                } //End else
            } //End if (block_store_read(fs->bs, fd_inode->data_ptrs[i], buffer))
            else
            {
                fs->fd_table.fd_pos[fd] += total_bytes_read;
                write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                free(fd_inode);
                return total_bytes_read;
            } //End else
        } //End if (i < 5)
        else if (i < 7)
        {
            //Indirect pointers
            if (fd_inode->data_ptrs[i] < 33 || fd_inode->data_ptrs[i] >= BITMAP_BITS)
            {
                //printf("\tindirect: fd_inode->data_ptrs[%u] = %u\n", i, fd_inode->data_ptrs[i]);

                fs->fd_table.fd_pos[fd] += total_bytes_read;
                write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                //printf("fs_write: Return 5\nnew data block num = %zu\n", new_data_block_num);
                //Something went wrong allocating a new data block
                free(fd_inode);
                return total_bytes_read;
            } //End if (fd_inode->data_ptrs[i] < 33)

            if (block_store_read(fs->bs, fd_inode->data_ptrs[i], indirect_block))
            {
                //printf("\tindir: before loop: j =  %u, indir_index = %zu\n", j, indir_index);
                if (!total_bytes_read)
                {
                    j = indir_index;
                } //End 
                else
                {
                    j = 0;
                } //End else

                for (; j < BLOCK_SIZE/sizeof(block_ptr_t) && total_bytes_read < nbyte && !finished && num_blocks_used <= num_blocks_needed; j++)
                {
                    //printf("i = %u : j = %u total_bytes_read = %zu\n", i, j, total_bytes_read);
                    if (indirect_block[j] < 33 || indirect_block[j] >= BITMAP_BITS)
                    {
                        //printf("\tindirect: indirect_block[%u] = %u\n", j, indirect_block[j]);
                        fs->fd_table.fd_pos[fd] += total_bytes_read;
                        write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                        //printf("fs_write: Return 6\nnew data block num = %zu\n", new_data_block_num);
                        //Something went wrong allocating a new data block
                        free(fd_inode);
                        return total_bytes_read;
                    } //End 

                    if (block_store_read(fs->bs, indirect_block[j], buffer))
                    {   
                        if (!initial_bytes_read && cur_pos == 0)
                        {
                            finished = true;
                            memcpy(dst, buffer, initial_bytes);
                            total_bytes_read += initial_bytes;
                            initial_bytes_read = true;
                            num_blocks_used++;
                        } //End 
                        else if (!initial_bytes_read && cur_pos > 0)
                        {
                            memcpy(dst, &(buffer[BLOCK_SIZE - initial_bytes]), initial_bytes);
                            total_bytes_read += initial_bytes;
                            initial_bytes_read = true;
                            num_blocks_used++;
                        } //End 
                        else if (num_blocks_used == num_blocks_needed && remainder_bytes % BLOCK_SIZE != 0)
                        {
                            memcpy(&(((uint8_t *)dst)[total_bytes_read]), buffer, remainder_bytes);
                            total_bytes_read += remainder_bytes;
                            finished = true;
                            //printf("\n1: total_bytes_read = %zu\n", total_bytes_read);
                            num_blocks_used++;
                        }
                        else
                        {
                            memcpy(&(((uint8_t *)dst)[total_bytes_read]), buffer, BLOCK_SIZE);
                            total_bytes_read += BLOCK_SIZE;
                            num_blocks_used++;

                            if (total_bytes_read == nbyte)
                            {
                                //printf("1: I happened------------------------------------------------------------------------\n");
                                finished = true;
                            } //End 
                            //printf("\n2: total_bytes_read = %zu\n", total_bytes_read);
                        } //End else
                    } //End 
                    else
                    {
                        fs->fd_table.fd_pos[fd] += total_bytes_read;
                        fd_inode->mdata.size += total_bytes_read;
                        write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                        free(fd_inode);
                        return total_bytes_read;
                    } //End else

                    if (finished)
                    {
                        //printf("2: I happened------------------------------------------------------------------------\n");
                        break;
                    } //End 
                } //End for (uint32_t j = 0; j < num_blocks_needed - i && j < BLOCK_SIZE/sizeof(block_ptr_t) && !finished; j++)
                //printf("\tindirect: after indirect: j = %u\n", j);
            } //End
        } //End
        else
        {
            //Double indirect pointer
            if (fd_inode->data_ptrs[i] < 33 || fd_inode->data_ptrs[i] >= BITMAP_BITS)
            {
                //printf("\tdbl_indir: fd_inode->data_ptrs[%u] = %u\n", i, fd_inode->data_ptrs[i]);
                fs->fd_table.fd_pos[fd] += total_bytes_read;
                write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                //printf("fs_write: Return 7\nnew data block num = %zu\n", new_data_block_num);
                //Something went wrong allocating a new data block
                free(fd_inode);
                return total_bytes_read; 
            } //End if (fd_inode->data_ptrs[i] < 33)

            if (block_store_read(fs->bs, fd_inode->data_ptrs[i], indirect_block))
            {
                //printf("\tdbl_indir: before loop: j =  %u, dbl_indir_inddx1 = %zu\n", j, dbl_indir_index1); 
                if (!total_bytes_read)
                {
                    j = dbl_indir_index1;
                } //End 
                else if (total_bytes_read && !dbl_indir_index1)
                {
                    j = 0;
                } //End else

                for (; j < BLOCK_SIZE/sizeof(block_ptr_t) && total_bytes_read < nbyte && !finished && num_blocks_used <= num_blocks_needed; j++)
                {
                    if (indirect_block[j] < 33 || indirect_block[j] >= BITMAP_BITS)
                    {
                        //printf("\tdbl_indir: indirect_block[%u] = %u\n", j, indirect_block[j]);
                        fs->fd_table.fd_pos[fd] += total_bytes_read;
                        write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                        //printf("fs_write: Return 8\nnew data block num = %zu\n", new_data_block_num);
                        //Something went wrong allocating a new data block
                        free(fd_inode);
                        return total_bytes_read; 
                    } //End

                    if (block_store_read(fs->bs, indirect_block[j], dbl_indirect_block))
                    {
                        //printf("\tdbl_indir: before loop: k =  %u, dbl_indir_inddx2 = %zu\n", k, dbl_indir_index2);
                        if (!total_bytes_read)
                        {
                            k = dbl_indir_index2;
                        } //End 
                        else
                        {
                            k = 0;
                        } //End else

                        for (; k < BLOCK_SIZE/sizeof(block_ptr_t) && total_bytes_read < nbyte && !finished && num_blocks_used <= num_blocks_needed; k++)
                        {
                            if (dbl_indirect_block[k] < 33 || dbl_indirect_block[k] >= BITMAP_BITS)
                            {
                                //printf("\tdbl_indir: j = %u - dbl_indirect_block[%u] = %u\n", j, k, dbl_indirect_block[k]);
                                fs->fd_table.fd_pos[fd] += total_bytes_read;
                                fd_inode->mdata.size += total_bytes_read;
                                write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                                //printf("fs_write: Return 9\nnew data block num = %zu\n", new_data_block_num);
                                //Something went wrong allocating a new data block
                                free(fd_inode);
                                return total_bytes_read;
                            } //End if (indirect_block[j] < 33)

                            if (block_store_read(fs->bs, dbl_indirect_block[k], buffer))
                            {
                                if (!initial_bytes_read && cur_pos == 0)
                                {
                                    memcpy(dst, buffer, initial_bytes);
                                    total_bytes_read += initial_bytes;
                                    initial_bytes_read = true;
                                    num_blocks_used++;
                                } //End 
                                else if (!initial_bytes_read && cur_pos > 0)
                                {
                                    memcpy(dst, &(buffer[BLOCK_SIZE - initial_bytes]), initial_bytes);
                                    total_bytes_read += initial_bytes;
                                    initial_bytes_read = true;
                                    num_blocks_used++;
                                } //End 
                                else if (num_blocks_used == num_blocks_needed && remainder_bytes % BLOCK_SIZE != 0)
                                {
                                    memcpy(&(((uint8_t *)dst)[total_bytes_read]), buffer, remainder_bytes);
                                    total_bytes_read += remainder_bytes;
                                    finished = true;
                                    //printf("\n1: total_bytes_read = %zu\n", total_bytes_read);
                                    num_blocks_used++;
                                }
                                else
                                {
                                    memcpy(&(((uint8_t *)dst)[total_bytes_read]), buffer, BLOCK_SIZE);
                                    total_bytes_read += BLOCK_SIZE;
                                    num_blocks_used++;

                                    if (total_bytes_read == nbyte)
                                    {
                                        //printf("1: I happened-----------------------------------\n");
                                        finished = true;
                                    } //End 
                                    //printf("\n2: total_bytes_read = %zu\n", total_bytes_read);
                                } //End else
                            } //End 
                            else
                            {
                                fs->fd_table.fd_pos[fd] += total_bytes_read;
                                fd_inode->mdata.size = total_bytes_read;
                                write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                                free(fd_inode);
                                return total_bytes_read;
                            } //End else

                            if (finished)
                            {
                                finished = true;
                                //printf("2: I happened------------------------------------------------------------------------\n");
                                break;
                            } //End
                        } //End for (k = dbl_indir_index2; k < BLOCK_SIZE/sizeof(block_ptr_t) && total_bytes_read < nbyte && !finished && num_blocks_used <= num_blocks_needed; k++)
                        //printf("\tdbl_indirect: after dbl_indirect: k = %u\n", k);
                    } //End 
                    else
                    {
                        fs->fd_table.fd_pos[fd] += total_bytes_read;
                        fd_inode->mdata.size += total_bytes_read;
                        write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                        free(fd_inode);
                        return total_bytes_read;
                    } //End else

                    if (finished)
                    {
                        //printf("2: I happened------------------------------------------------------------------------\n");
                        break;
                    } //End
                } //End for (j = dbl_indir_index1; j < BLOCK_SIZE/sizeof(block_ptr_t) && total_bytes_read < nbyte && !finished && num_blocks_used <= num_blocks_needed; j++)
                //printf("\tdbl_indirect: after indirect: j = %u\n", j);
            } //End if (block_store_read(fs->bs, fd_inode->data_ptrs[i], indirect_block)) 


        } //End else

        if (total_bytes_read == nbyte || finished)
        {
            //printf("3: I happened------------------------------------------------------------------------\n");
            break;
        } //End 
    } //End for (int i = 0; i < num_blocks_needed; i++)

    //printf("i = %u\n", i);
    fs->fd_table.fd_pos[fd] += total_bytes_read;
    write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
    free(fd_inode);

    //printf("total_bytes_read = %zu\n", total_bytes_read);

    return total_bytes_read;
} //End 

/***************************************************/

ssize_t fs_write(S17FS_t *fs, int fd, const void *src, size_t nbyte)
{
    //Check that the parameters are valid
    if (fs == NULL || fd < 0 || src == NULL)
    {
        //printf("fs_write: Return 1\n");
        return -1;
    } //End 

    if (!bitmap_test(fs->fd_table.fd_status, fd))
    {
        //printf("fs_write: Return 2\n");
        return -1;
    } //End 

    //Declare variables
    bool finished = false;
    bool initial_bytes_written = false;
    size_t total_bytes_written = 0;
    data_block_t buffer;
    uint16_t indirect_block[256];
    uint16_t dbl_indirect_block[256];
    inode_t* fd_inode = get_inode(fs, fs->fd_table.fd_inode[fd]);

    if (fd_inode == NULL)
    {
        //printf("fs_write: Return 3\n");
        return -1;
    } //End 

    START_PTR_t start_index = DIRECT;

    size_t cur_pos = fs->fd_table.fd_pos[fd];
    size_t starting_block = cur_pos / BLOCK_SIZE;
    size_t indir_index = 0;
    size_t dbl_indir_index1 = 0; 
    size_t dbl_indir_index2 = 0;

    //printf("cur_pos = %zu\n", cur_pos);

    //Determine which pointer the write should start in
    if (starting_block < 5)
    {
        start_index = starting_block;
    } //End 
    else if (starting_block >= 5 && starting_block < 261)
    {
        //First indirect pointer
        indir_index = (starting_block - (DIRECT_TOTAL)) % DIRECT_PER_BLOCK;
        start_index = INDIRECT1;
    } //End 
    else if (starting_block >= 261 && starting_block < 517)
    {
        //Second indirect pointer
        indir_index = (starting_block - (DIRECT_TOTAL + DIRECT_PER_BLOCK)) % DIRECT_PER_BLOCK;
        start_index = INDIRECT2;
    } //End 
    else
    {
        //Double indirect pointer
        dbl_indir_index1 = (starting_block - (DIRECT_TOTAL + INDIRECT_TOTAL)) / DIRECT_PER_BLOCK;
        dbl_indir_index2 = (starting_block - (DIRECT_TOTAL + INDIRECT_TOTAL)) % DIRECT_PER_BLOCK;
        start_index = DBL_INDIRECT;
    } //End else

    //Caclulate how many blocks are needed, as well as figure out depending on 
    //the starting position of the file descriptor how much the first and last
    //blocks needed should be written
    uint32_t num_blocks_needed = 1 + (uint32_t)((nbyte-1)/BLOCK_SIZE);
    uint32_t num_blocks_used = 0;
    uint32_t initial_bytes = BLOCK_SIZE - (cur_pos - BLOCK_SIZE * starting_block);  
    uint32_t remainder_bytes = (nbyte - initial_bytes) % BLOCK_SIZE;

    if (initial_bytes == 512)
    {
        initial_bytes = 1 + ((nbyte - 1) % BLOCK_SIZE);
        remainder_bytes = 0;
    } //End 

    //printf("\nnum_blocks_needed = %u\n", num_blocks_needed);

    uint32_t i = 0;
    uint32_t j = 0;
    uint32_t k = 0;
    for (i = start_index; i < INODE_PTR_TOTAL && total_bytes_written < nbyte && !finished && num_blocks_used <= num_blocks_needed; i++)
    {
        //printf("i = %u\n", i);
        if (i < 5)
        {
            //Direct Pointers
            if (fd_inode->data_ptrs[i] < 33 || fd_inode->data_ptrs[i] >= BITMAP_BITS)
            {
                //printf("\tdirect: fd_inode->data_ptrs[%u] = %u\n", i, fd_inode->data_ptrs[i]);
                size_t new_data_block_num = -1;
                new_data_block_num = block_store_allocate(fs->bs);

                //printf("\tData block num: %zu\n", new_data_block_num);

                if (new_data_block_num <= 32 || new_data_block_num == SIZE_MAX || new_data_block_num >= BITMAP_BITS)
                {
                    fs->fd_table.fd_pos[fd] += total_bytes_written;
                    fd_inode->mdata.size += total_bytes_written;
                    write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                    //printf("fs_write: Return 4\nnew data block num = %zu\n", new_data_block_num);
                    //Something went wrong allocating a new data block
                    free(fd_inode);
                    return total_bytes_written;
                } //End 

                fd_inode->data_ptrs[i] = new_data_block_num;
            } //End if (fd_inode->data_ptrs[i] < 33)

            if (block_store_read(fs->bs, fd_inode->data_ptrs[i], buffer))
            {   
                if (!initial_bytes_written && cur_pos == 0)
                {
                    memcpy(buffer, src, initial_bytes);
                    total_bytes_written += initial_bytes;
                    initial_bytes_written = true;
                    num_blocks_used++;
                } //End 
                else if (!initial_bytes_written && cur_pos > 0)
                {
                    memcpy(&(buffer[BLOCK_SIZE - initial_bytes]), src, initial_bytes);
                    total_bytes_written += initial_bytes;
                    initial_bytes_written = true;
                    num_blocks_used++;
                } //End 
                else if (num_blocks_used == num_blocks_needed && remainder_bytes % BLOCK_SIZE != 0)
                {
                    memcpy(buffer, &(((uint8_t *)src)[total_bytes_written]), remainder_bytes);
                    total_bytes_written += remainder_bytes;
                    //printf("\n1: total_bytes_written = %zu\n", total_bytes_written);
                    num_blocks_used++;
                }
                else
                {
                    memcpy(buffer, &(((uint8_t *)src)[total_bytes_written]), BLOCK_SIZE);
                    total_bytes_written += BLOCK_SIZE;
                    //printf("\n2: total_bytes_written = %zu\n", total_bytes_written);
                    num_blocks_used++;

                    if (total_bytes_written == nbyte)
                    {
                        //printf("1: I happened------------------------------------------------------------------------\n");
                        finished = true;
                    } //End
                } //End else

                if (!block_store_write(fs->bs, fd_inode->data_ptrs[i], buffer))
                {
                    fs->fd_table.fd_pos[fd] += total_bytes_written;
                    fd_inode->mdata.size += total_bytes_written;
                    write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                    free(fd_inode);
                    return total_bytes_written;
                } //End else
            } //End if (block_store_read(fs->bs, fd_inode->data_ptrs[i], buffer))
            else
            {
                fs->fd_table.fd_pos[fd] += total_bytes_written;
                fd_inode->mdata.size += total_bytes_written;
                write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                free(fd_inode);
                return total_bytes_written;
            } //End else
        } //End if (i < 5)
        else if (i < 7)
        {
            //Indirect pointers
            if (fd_inode->data_ptrs[i] < 33 || fd_inode->data_ptrs[i] >= BITMAP_BITS)
            {
                //printf("\tindirect: fd_inode->data_ptrs[%u] = %u\n", i, fd_inode->data_ptrs[i]);
                size_t new_data_block_num = -1; 
                new_data_block_num = block_store_allocate(fs->bs);

                //printf("\tData block num: %zu\n", new_data_block_num);

                if (new_data_block_num <= 32 || new_data_block_num == SIZE_MAX || new_data_block_num >= BITMAP_BITS)
                {
                    fs->fd_table.fd_pos[fd] += total_bytes_written;
                    fd_inode->mdata.size += total_bytes_written;
                    write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                    //printf("fs_write: Return 5\nnew data block num = %zu\n", new_data_block_num);
                    //Something went wrong allocating a new data block
                    free(fd_inode);
                    return total_bytes_written;
                } //End 

                overhead++;
                fd_inode->data_ptrs[i] = new_data_block_num;
                initialize_indirect_block(fs, fd_inode->data_ptrs[i]);
            } //End if (fd_inode->data_ptrs[i] < 33)

            if (block_store_read(fs->bs, fd_inode->data_ptrs[i], indirect_block))
            {
                //printf("\tindir: before loop: j =  %u, indir_index = %zu\n", j, indir_index);
                if (!total_bytes_written)
                {
                    j = indir_index;
                } //End 
                else
                {
                    j = 0;
                } //End else

                for (; j < BLOCK_SIZE/sizeof(block_ptr_t) && total_bytes_written < nbyte && !finished && num_blocks_used <= num_blocks_needed; j++)
                {
                    //printf("i = %u : j = %u total_bytes_written = %zu\n", i, j, total_bytes_written);
                    if (indirect_block[j] < 33 || indirect_block[j] >= BITMAP_BITS)
                    {
                        //printf("\tindirect: indirect_block[%u] = %u\n", j, indirect_block[j]);
                        size_t new_data_block_num = -1;
                        new_data_block_num = block_store_allocate(fs->bs);
                        //printf("\tData block num: %zu\n", new_data_block_num);

                        if (new_data_block_num <= 32 || new_data_block_num == SIZE_MAX || new_data_block_num >= BITMAP_BITS)
                        {
                            fs->fd_table.fd_pos[fd] += total_bytes_written;
                            fd_inode->mdata.size += total_bytes_written;
                            write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                            //printf("fs_write: Return 6\nnew data block num = %zu\n", new_data_block_num);
                            //Something went wrong allocating a new data block
                            free(fd_inode);
                            return total_bytes_written;
                        } //End

                        indirect_block[j] = new_data_block_num;

                        if (!block_store_write(fs->bs, fd_inode->data_ptrs[i], indirect_block))
                        {
                            fs->fd_table.fd_pos[fd] += total_bytes_written;
                            fd_inode->mdata.size = total_bytes_written;
                            write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                            free(fd_inode);
                            return total_bytes_written;
                        } //End
                    } //End 


                    if (block_store_read(fs->bs, indirect_block[j], buffer))
                    {   
                        if (!initial_bytes_written && cur_pos == 0)
                        {
                            finished = true;
                            memcpy(buffer, src, initial_bytes);
                            total_bytes_written += initial_bytes;
                            initial_bytes_written = true;
                            num_blocks_used++;
                        } //End 
                        else if (!initial_bytes_written && cur_pos > 0)
                        {
                            memcpy(&(buffer[BLOCK_SIZE - initial_bytes]), src, initial_bytes);
                            total_bytes_written += initial_bytes;
                            initial_bytes_written = true;
                            num_blocks_used++;
                        } //End 
                        else if (num_blocks_used == num_blocks_needed && remainder_bytes % BLOCK_SIZE != 0)
                        {
                            memcpy(buffer, &(((uint8_t *)src)[total_bytes_written]), remainder_bytes);
                            total_bytes_written += remainder_bytes;
                            finished = true;
                            //printf("\n1: total_bytes_written = %zu\n", total_bytes_written);
                            num_blocks_used++;
                        }
                        else
                        {
                            memcpy(buffer, &(((uint8_t *)src)[total_bytes_written]), BLOCK_SIZE);
                            total_bytes_written += BLOCK_SIZE;
                            num_blocks_used++;

                            if (total_bytes_written == nbyte)
                            {
                                //printf("1: I happened------------------------------------------------------------------------\n");
                                finished = true;
                            } //End 
                            //printf("\n2: total_bytes_written = %zu\n", total_bytes_written);
                        } //End else

                        //if (!block_store_write(fs->bs, fd_inode->data_ptrs[i], buffer))
                        if (!block_store_write(fs->bs, indirect_block[j], buffer))
                        {
                            fs->fd_table.fd_pos[fd] += total_bytes_written;
                            fd_inode->mdata.size += total_bytes_written;
                            write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                            free(fd_inode);
                            return total_bytes_written;
                        } //End else
                    } //End 
                    else
                    {
                        fs->fd_table.fd_pos[fd] += total_bytes_written;
                        fd_inode->mdata.size += total_bytes_written;
                        write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                        free(fd_inode);
                        return total_bytes_written;
                    } //End else

                    if (finished)
                    {
                        //printf("2: I happened------------------------------------------------------------------------\n");
                        break;
                    } //End 
                } //End for (uint32_t j = 0; j < num_blocks_needed - i && j < BLOCK_SIZE/sizeof(block_ptr_t) && !finished; j++)
                //printf("\tindirect: after indirect: j = %u\n", j);
            } //End
        } //End
        else
        {
            //Double indirect pointer
            //printf("\nHey you made it to the double indirect pointers\n\n");
            if (fd_inode->data_ptrs[i] < 33 || fd_inode->data_ptrs[i] >= BITMAP_BITS)
            {
                //printf("\tdbl_indir: fd_inode->data_ptrs[%u] = %u\n", i, fd_inode->data_ptrs[i]);
                size_t new_data_block_num = -1;
                new_data_block_num = block_store_allocate(fs->bs);

                //printf("\tData block num: %zu\n", new_data_block_num);

                if (new_data_block_num <= 32 || new_data_block_num == SIZE_MAX || new_data_block_num >= BITMAP_BITS)
                {
                    fs->fd_table.fd_pos[fd] += total_bytes_written;
                    fd_inode->mdata.size += total_bytes_written;
                    write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                    //printf("fs_write: Return 7\nnew data block num = %zu\n", new_data_block_num);
                    //Something went wrong allocating a new data block
                    free(fd_inode);
                    return total_bytes_written;
                } //End 

                overhead++;
                fd_inode->data_ptrs[i] = new_data_block_num;
                initialize_indirect_block(fs, fd_inode->data_ptrs[i]);
            } //End if (fd_inode->data_ptrs[i] < 33)

            if (block_store_read(fs->bs, fd_inode->data_ptrs[i], indirect_block))
            {
                //printf("\tdbl_indir: before loop: j =  %u, dbl_indir_inddx1 = %zu\n", j, dbl_indir_index1); 
                if (!total_bytes_written)
                {
                    j = dbl_indir_index1;
                } //End 
                else if (total_bytes_written && !dbl_indir_index1)
                {
                    j = 0;
                } //End else

                for (; j < BLOCK_SIZE/sizeof(block_ptr_t) && total_bytes_written < nbyte && !finished && num_blocks_used <= num_blocks_needed; j++)
                {
                    if (indirect_block[j] < 33 || indirect_block[j] >= BITMAP_BITS)
                    {
                        //printf("\tdbl_indir: indirect_block[%u] = %u\n", j, indirect_block[j]);
                        size_t new_data_block_num = -1;
                        new_data_block_num = block_store_allocate(fs->bs);

                        //printf("\tData block num: %zu\n", new_data_block_num);

                        if (new_data_block_num <= 32 || new_data_block_num == SIZE_MAX || new_data_block_num >= BITMAP_BITS)
                        {
                            fs->fd_table.fd_pos[fd] += total_bytes_written;
                            fd_inode->mdata.size += total_bytes_written;
                            write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                            //printf("fs_write: Return 8\nnew data block num = %zu\n", new_data_block_num);
                            //Something went wrong allocating a new data block
                            free(fd_inode);
                            return total_bytes_written;
                        } //End

                        overhead++;
                        indirect_block[j] = new_data_block_num;
                        initialize_indirect_block(fs, indirect_block[j]);

                        if (!block_store_write(fs->bs, fd_inode->data_ptrs[i], indirect_block))
                        {
                            fs->fd_table.fd_pos[fd] += total_bytes_written;
                            fd_inode->mdata.size += total_bytes_written;
                            write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                            free(fd_inode);
                            return total_bytes_written;
                        } //End 
                    } //End

                    if (block_store_read(fs->bs, indirect_block[j], dbl_indirect_block))
                    {
                        //printf("\tdbl_indir: before loop: k =  %u, dbl_indir_inddx2 = %zu\n", k, dbl_indir_index2);
                        if (!total_bytes_written)
                        {
                            k = dbl_indir_index2;
                        } //End 
                        else
                        {
                            k = 0;
                        } //End else

                        for (; k < BLOCK_SIZE/sizeof(block_ptr_t) && total_bytes_written < nbyte && !finished && num_blocks_used <= num_blocks_needed; k++)
                        {
                            if (dbl_indirect_block[k] < 33 || dbl_indirect_block[k] >= BITMAP_BITS)
                            {
                                //printf("\tdbl_indir: j = %u - dbl_indirect_block[%u] = %u\n", j, k, dbl_indirect_block[k]);
                                size_t new_data_block_num = -1;
                                new_data_block_num = block_store_allocate(fs->bs);
                                //printf("\tData block num: %zu\n", new_data_block_num);

                                if (new_data_block_num <= 32 || new_data_block_num == SIZE_MAX || new_data_block_num >= BITMAP_BITS)
                                {
                                    fs->fd_table.fd_pos[fd] += total_bytes_written;
                                    fd_inode->mdata.size += total_bytes_written;
                                    write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                                    //printf("fs_write: Return 9\nnew data block num = %zu\n", new_data_block_num);
                                    //Something went wrong allocating a new data block
                                    free(fd_inode);
                                    return total_bytes_written;
                                } //End

                                dbl_indirect_block[k] = new_data_block_num; 

                                if (!block_store_write(fs->bs, indirect_block[j], dbl_indirect_block))
                                {
                                    fs->fd_table.fd_pos[fd] += total_bytes_written;
                                    fd_inode->mdata.size += total_bytes_written;
                                    write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                                    //printf("fs_write: Return 10\nnew data block num = %zu\n", new_data_block_num);
                                    //Something went wrong allocating a new data block
                                    free(fd_inode);
                                    return total_bytes_written;
                                } //End if (!block_store_write(fs->bs, indirect_block[j], dbl_indirect_block))
                            } //End if (indirect_block[j] < 33)

                            if (block_store_read(fs->bs, dbl_indirect_block[k], buffer))
                            {
                                if (!initial_bytes_written && cur_pos == 0)
                                {
                                    memcpy(buffer, src, initial_bytes);
                                    total_bytes_written += initial_bytes;
                                    initial_bytes_written = true;
                                    num_blocks_used++;
                                } //End 
                                else if (!initial_bytes_written && cur_pos > 0)
                                {
                                    memcpy(&(buffer[BLOCK_SIZE - initial_bytes]), src, initial_bytes);
                                    total_bytes_written += initial_bytes;
                                    initial_bytes_written = true;
                                    num_blocks_used++;
                                } //End 
                                else if (num_blocks_used == num_blocks_needed && remainder_bytes % BLOCK_SIZE != 0)
                                {
                                    memcpy(buffer, &(((uint8_t *)src)[total_bytes_written]), remainder_bytes);
                                    total_bytes_written += remainder_bytes;
                                    finished = true;
                                    //printf("\n1: total_bytes_written = %zu\n", total_bytes_written);
                                    num_blocks_used++;
                                }
                                else
                                {
                                    memcpy(buffer, &(((uint8_t *)src)[total_bytes_written]), BLOCK_SIZE);
                                    memcpy(buffer, src, BLOCK_SIZE);
                                    total_bytes_written += BLOCK_SIZE;
                                    num_blocks_used++;

                                    if (total_bytes_written == nbyte)
                                    {
                                        //printf("1: I happened-----------------------------------\n");
                                        finished = true;
                                    } //End 
                                    //printf("\n2: total_bytes_written = %zu\n", total_bytes_written);
                                } //End else

                                //if (!block_store_write(fs->bs, indirect_block[j], buffer))
                                if (!block_store_write(fs->bs, dbl_indirect_block[k], buffer))
                                {
                                    fs->fd_table.fd_pos[fd] += total_bytes_written;
                                    fd_inode->mdata.size += total_bytes_written;
                                    write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                                    free(fd_inode);
                                    return total_bytes_written;
                                } //End else
                            } //End 
                            else
                            {
                                fs->fd_table.fd_pos[fd] += total_bytes_written;
                                fd_inode->mdata.size = total_bytes_written;
                                write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                                free(fd_inode);
                                return total_bytes_written;
                            } //End else

                            if (finished /*|| cur_pos + total_bytes_written >= 33398272*//*33529344 33396736*/)
                            {
                                finished = true;
                                //printf("2: I happened------------------------------------------------------------------------\n");
                                break;
                            } //End
                        } //End for (k = dbl_indir_index2; k < BLOCK_SIZE/sizeof(block_ptr_t) && total_bytes_written < nbyte && !finished && num_blocks_used <= num_blocks_needed; k++)
                        //printf("\tdbl_indirect: after dbl_indirect: k = %u\n", k);
                    } //End 
                    else
                    {
                        fs->fd_table.fd_pos[fd] += total_bytes_written;
                        fd_inode->mdata.size += total_bytes_written;
                        write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
                        free(fd_inode);
                        return total_bytes_written;
                    } //End else

                    if (finished)
                    {
                        //printf("2: I happened------------------------------------------------------------------------\n");
                        break;
                    } //End
                } //End for (j = dbl_indir_index1; j < BLOCK_SIZE/sizeof(block_ptr_t) && total_bytes_written < nbyte && !finished && num_blocks_used <= num_blocks_needed; j++)
                //printf("\tdbl_indirect: after indirect: j = %u\n", j);
            } //End if (block_store_read(fs->bs, fd_inode->data_ptrs[i], indirect_block)) 


        } //End else

        if (total_bytes_written == nbyte || finished)
        {
            //printf("3: I happened------------------------------------------------------------------------\n");
            break;
        } //End 
    } //End for (int i = 0; i < num_blocks_needed; i++)

    //printf("i = %u\n", i);
    fs->fd_table.fd_pos[fd] += total_bytes_written;
    fd_inode->mdata.size += total_bytes_written;
    write_inode(fs, fd_inode, fd_inode->mdata.self_inode_num);
    free(fd_inode);

    //printf("total_bytes_written = %zu\n", total_bytes_written);
    //printf("AFTER: overhead = %d\n", overhead);
    return total_bytes_written;
} //End 

/***************************************************/

int fs_remove(S17FS_t *fs, const char *path)
{
    //Check that the parameters are valid
    if (fs == NULL || path == NULL  || (strcmp(path, "") == 0) || strlen(path) >= FS_NAME_MAX || path[0] != '/' || path[strlen(path)-1] == '/' || (strlen(path) == 1))
    {
        //printf("fs_remove: Return 0\n");
        return -1;
    } //End
    //printf("\nPath: %s\n", path);

    //Get the root directory
    inode_t* root = get_root_dir(fs);
    //printf("\nroot->data_ptrs[0] == %u", root->data_ptrs[0]);

    if (root == NULL || (root->data_ptrs[0] <= 32 && root->data_ptrs[0] > 0))
    {
        //printf("\nroot == %p", root);
        //printf("\nroot->data_ptrs[0] == %u", root->data_ptrs[0]);
        //printf("fs_remove: Return 1\n");
        free(root);
        return -1;
    } //End 

    //Get the contents of the root directory
    //void* dir_contents = (file_record_t *)malloc(sizeof(data_block_t));
    file_record_t* dir_contents = get_dir_contents(fs, root->data_ptrs[0]);
    if (dir_contents == NULL)
    {
        //printf("fs_remove: Return 2\n");
        free(root);
        return -1;
    } //End 

    //Determine how long the path is
    int path_depth = 0;
    for (size_t i = 0; i < strlen(path); i++)
    {
        //printf("%c", path[i]);
        if (path[i] == '/')
        {
            path_depth++;
        } //End 
    } //End 

    //Tokenize the path and determine if the path exists
    char* path_copy = (char *)malloc(sizeof(char)*64);
    memcpy(path_copy, path, strlen(path)+1);

    char* token = NULL;
    const char delims[2] = "/";
    token = strtok(path_copy, delims);

    inode_t* cur_dir_inode = NULL;
    if (path_depth == 1)
    {
        cur_dir_inode = root;
    } //End 

    int cur_depth = 1;
    int next_rec_found = 0;
    int j = 0;

    //printf("\n%s", token);
    while (token != NULL && path_depth > 1)
    {
        //printf("/%s", token);
        if(cur_depth == path_depth)
        {
            //Found where the new record should go
            break;
        } //End 

        next_rec_found = 0;

        for (j = 0; j < DIR_REC_MAX; j++)
        {
            if (strcmp(dir_contents[j].name, token) == 0 && dir_contents[j].type == FS_DIRECTORY)
            {
                //The current path was found
                //printf("\nI happened!\n");
                next_rec_found = 1;

                //Get the data blocks of the next part of the path
                free(cur_dir_inode);
                cur_dir_inode = get_dir(fs, dir_contents[j].inode_num);

                if (cur_dir_inode == NULL || (cur_dir_inode->data_ptrs[0] <= 32 && cur_dir_inode->data_ptrs[0] > 0))
                {
                    //printf("fs_remove: Return 4: cur_dir-inode->data_ptrs[0] = %u\n", cur_dir_inode->data_ptrs[0]);
                    free(root);
                    free(dir_contents);
                    free(path_copy);
                    free(cur_dir_inode);
                    return -1;
                } //End

                if (block_store_read(fs->bs, cur_dir_inode->data_ptrs[0], dir_contents) <= 0)
                {
                    //printf("fs_remove: Return 5\n");
                    free(root);
                    free(dir_contents);
                    free(path_copy);
                    free(cur_dir_inode);
                    return -1;
                } //End 

            } //End 
        } //End

        if (next_rec_found == 0)
        {
            //printf("fs_remove: Return 6\n");
            //Directory wasn't found
            free(root);
            free(dir_contents);
            free(path_copy);
            free(cur_dir_inode);
            return -1;
        } //End 

        token = strtok(NULL, delims);
        cur_depth++;
    } //End 

    //Search for the record in the target directory
    for (int i = 0; i < DIR_REC_MAX; i++)
    {
        //printf("\ndir_contents[%d].name = %s - token = %s\ndir_contents[%d].inum = %u - *.type = %u - *.record_count = %u\n", i, dir_contents[i].name, token, i, dir_contents[i].inode_num, dir_contents[i].type, dir_contents[i].record_count);
        //printf("Results of strcmp = %d\n\tstr**%s**\n\t**%s**\n", strcmp(dir_contents[i].name, token), dir_contents[i].name, token);
        if (strcmp(dir_contents[i].name, token) == 0 /*&& ((dir_contents[i].record_count == 0 && dir_contents[i].type == FS_DIRECTORY) || dir_contents[i].type == FS_REGULAR)*/)
        {
            inode_t* dir_inode = get_inode(fs, dir_contents[i].inode_num);
            if (dir_contents[i].type == FS_DIRECTORY)
            {
                if (!dir_inode)
                {
                    break;
                } //End 

                //printf("fs_remove: dir_inode->mdata.record_count = %u\n", dir_inode->mdata.record_count);

                if (dir_inode->mdata.record_count > 0)
                {
                    free(dir_inode);
                    break;
                } //End
            } //End 

            //printf("fs_remove: Return 7 - SUCCESS\n");
            //Found the record, need to remove it from the directory, and remove any related file descriptors
            char* empty_name = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
            //dir_contents[i].name[0] = dir_contents[i].name[1] = dir_contents[i].name[2] = dir_contents[i].name[3] = '\0';
            memcpy(dir_contents[i].name, empty_name, 64);
            dir_contents[i].type = -1;
            bitmap_reset(fs->inode_bitmap, dir_contents[i].inode_num);
            cur_dir_inode->mdata.record_count -= 1;
            //dir_inode->mdata.record_count -= 1;
            write_inode(fs, cur_dir_inode, cur_dir_inode->mdata.self_inode_num);
            free(dir_inode);
            remove_files_file_descriptors(fs, dir_contents[i].inode_num);
            dir_contents[i].inode_num = 0;

            free(root);
            free(dir_contents);
            free(path_copy);
            if (path_depth > 1)
            {
                free(cur_dir_inode);
            } //End

            return 0;
        } //End 
    } //End

    free(root);
    free(dir_contents);
    free(path_copy);
    if (path_depth > 1)
    {
        free(cur_dir_inode);
    } //End 

    //printf("fs_remove: Return 8: file not found\n");

    return -1;
} //End 

/***************************************************/

dyn_array_t *fs_get_dir(S17FS_t *fs, const char *path)
{
    //Check that the parameters are valid
    if (fs == NULL || path == NULL  || (strcmp(path, "") == 0) || strlen(path) >= FS_NAME_MAX || path[0] != '/' || (path[strlen(path)-1] == '/' && strlen(path) > 1))
    {
        //printf("\nReturn 0\n");
        return NULL;
    } //End
    //printf("\nPath: %s\n", path);

    //Get the root directory
    inode_t* root = get_root_dir(fs);
    //printf("\nroot->data_ptrs[0] == %u", root->data_ptrs[0]);

    if (root == NULL || (root->data_ptrs[0] <= 32 && root->data_ptrs[0] > 0))
    {
        //printf("\nroot == %p", root);
        //printf("\nroot->data_ptrs[0] == %u", root->data_ptrs[0]);
        //printf("\nReturn 1\n");
        free(root);
        return NULL;
    } //End 

    //Get the contents of the root directory
    file_record_t* dir_contents = get_dir_contents(fs, root->data_ptrs[0]);
    if (dir_contents == NULL)
    {
        //printf("\nReturn 2\n");

        free(root);
        return NULL;
    } //End 

    //Determine how long the path is
    int path_depth = 1;
    for (size_t i = 0; i < strlen(path) && strlen(path) > 1; i++)
    {
        //printf("%c", path[i]);
        if (path[i] == '/')
        {
            path_depth++;
        } //End 
    } //End 

    //Tokenize the path and determine if the path exists
    char* path_copy = (char *)malloc(sizeof(char)*64);
    memcpy(path_copy, path, strlen(path)+1);

    char* token = NULL;
    const char delims[2] = "/";
    token = strtok(path_copy, delims);

    inode_t* cur_dir_inode = NULL;
    if (path_depth == 1)
    {
        //printf("\nPath: %s\nPath len == %d\n", path, path_depth);
        cur_dir_inode = root;
    } //End 

    int cur_depth = 1;
    int next_rec_found = 0;
    int j = 0;

    //printf("\nToken before: %s", token);
    while (token != NULL && path_depth > 1)
    {
        //printf("\nMade it into the directory traversing loop\n");
        //printf("/%s", token);
        if(cur_depth == path_depth+1)
        {
            //Found where the new record should go
            //printf("\ncur_depth == path_depth+1\n");
            break;
        } //End 

        next_rec_found = 0;

        for (j = 0; j < DIR_REC_MAX; j++)
        {
            if (strcmp(dir_contents[j].name, token) == 0 && dir_contents[j].type == FS_DIRECTORY)
            {
                //The current path was found
                //printf("\nI happened!\n");
                next_rec_found = 1;

                //Get the data blocks of the next part of the path
                free(cur_dir_inode);
                cur_dir_inode = get_dir(fs, dir_contents[j].inode_num);

                if (cur_dir_inode == NULL || (cur_dir_inode->data_ptrs[0] <= 32 && cur_dir_inode->data_ptrs[0] > 0))
                {
                    //printf("\nReturn 3: cur_dir-inode->data_ptrs[0] = %u\n", cur_dir_inode->data_ptrs[0]);

                    free(root);
                    free(dir_contents);
                    free(path_copy);
                    free(cur_dir_inode);
                    return NULL;
                } //End

                if (block_store_read(fs->bs, cur_dir_inode->data_ptrs[0], dir_contents) <= 0)
                {
                    //printf("\nReturn 4\n");

                    free(root);
                    free(dir_contents);
                    free(path_copy);
                    free(cur_dir_inode);
                    return NULL;
                } //End 

            } //End 
        } //End

        if (next_rec_found == 0)
        {
            //printf("\nReturn 5\n");
            //Directory wasn't found
            free(root);
            free(dir_contents);
            free(path_copy);
            free(cur_dir_inode);
            return NULL;
        } //End 

        token = strtok(NULL, delims);
        cur_depth++;
    } //End 
    //printf("\nToken after: %s", token);

    dyn_array_t* da = dyn_array_create(DIR_REC_MAX, sizeof(file_record_t), NULL);
    if (da)
    {
        //printf("\n");
        //Search for the record in the target directory
        for (size_t i = 0; i < DIR_REC_MAX && i < cur_dir_inode->mdata.record_count; i++)
        {
            //printf("Record: %s\n", dir_contents[i].name);
            dyn_array_push_front(da, &dir_contents[i]);
        } //End

        free(root);
        free(dir_contents);
        free(path_copy);
        if (path_depth > 1)
        {
            free(cur_dir_inode);
        } //End

        return da;
    } //End 



    free(root);
    free(dir_contents);
    free(path_copy);
    if (path_depth > 1)
    {
        free(cur_dir_inode);
    } //End 


    //printf("\nReturn 6\n");

    return NULL;
} //End 

/***************************************************/

int fs_move(S17FS_t *fs, const char *src, const char *dst)
{
    //Check that the parameters are valid
    if (fs == NULL || src == NULL || (strcmp(src, "") == 0) || dst == NULL || (strcmp(dst, "") == 0))
    {
        return -1;
    } //End if (fs == NULL || src == NULL || dst == NULL)

    return -1;
} //End 

/***************************************************/
