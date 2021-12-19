#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/types.h>
#include <fcntl.h>
#include "ext2.h"
#include <assert.h>
#include <asm/byteorder.h>
#include <string.h>

////////////////////////////////////////////////////////////////////////////////
// FUNCTION FORMAT
// int func(arguments);
// FUNCTION always returns error code, all output arguments return by pointers
////////////////////////////////////////////////////////////////////////////////

enum ERROR_CODES{
    E_SUCCESS  = 0,
    E_BADARGS  = -1,
    E_BADALLOC = -2,
    E_BADIO    = -3,
    E_ERROR    = -4,
};

#ifdef NODEBUG
#define Dprintf(args...) do {} while(0);
#else
#define Dprintf(args...) do {printf(args);} while(0);
#endif

typedef struct ext2_super_block super_block_t;
typedef struct ext2_inode inode_t;
typedef struct ext2_group_desc group_desc_t;
typedef struct ext2_dir_entry ext2_dir_entry;
typedef struct ext2_dir_entry_2 ext2_dir_entry_2;

typedef struct ext2_fs
{
    int            dev_fd;
    super_block_t* sb;
    unsigned int   revision;
    size_t         block_size;
    size_t         inode_size;
    size_t         blocks_per_group;
    size_t         inodes_per_group;
    size_t         num_inodes;
    size_t         num_blocks;
} ext2_fs_t;

int get_ext2_superblock(int dev_fd, super_block_t* sb)
{
    if (dev_fd < 0)
    {
        fprintf(stderr, "[get_ext2_superblock] Bad input device\n");
        return E_BADARGS;
    }

    if (sb == NULL)
    {
        fprintf(stderr, "[get_ext2_superblock] Bad input superblock structure pointer\n");
        return E_BADARGS;
    }

    // My reading function?
    errno = 0;
    ssize_t num_readed = pread(dev_fd, sb, sizeof(super_block_t), BOOT_RECORD);
    if (num_readed < 0)
    {
        perror("[get_ext2_superblock] Reading superblock failed\n");
        return E_BADIO;
    }

    uint16_t magic = __le16_to_cpu(sb->s_magic);
    if (magic != EXT2_SUPER_MAGIC)
    {
        fprintf(stderr,
                "[get_ext2_superblock] It's not ext2. Magic = %.4X\n", magic);
        return E_ERROR;
    }

    return E_SUCCESS;
}

// make it with cache?
static ssize_t read_block(size_t block_id, ext2_fs_t* fs, uint8_t* buff)
{
    assert(fs != NULL);
    assert(buff != NULL);
    assert(block_id < fs->num_blocks);

    errno = 0;
    ssize_t read = pread(fs->dev_fd, buff, fs->block_size,
                         block_id * fs->block_size);
    if (read < 0)
    {
        perror("[read_block] Reading block failed\n");
        return E_BADIO;
    }

    return read;
}

int get_ext2_inode(ext2_fs_t* fs, long long int inode_num, inode_t* ret_inode)
{
    if (fs == NULL)
    {
        fprintf(stderr, "[get_ext2_inode] Bad input fs pointer\n");
        return E_BADARGS;
    }

    if (ret_inode == NULL)
    {
        fprintf(stderr, "[get_ext2_inode] Bad input inode structure pointer\n");
        return E_BADARGS;
    }

    if (inode_num < 0 || inode_num > fs->num_inodes)
    {
        fprintf(stderr, "[get_ext2_inode] Bad input inode number\n");
        return E_BADARGS;
    }

    size_t block_size = fs->block_size;

    errno = 0;
    uint8_t* buff = (uint8_t*) malloc(block_size);
    if (buff == NULL)
    {
        perror("[get_ext2_inode] Allocating buff failed\n");
        return E_BADALLOC;
    }

    size_t cur_desc_entry_id = 2;
    if (block_size >= 2048)
        cur_desc_entry_id = 1;

    size_t db_desc_per_block = block_size / sizeof(group_desc_t);
    size_t inodes_per_block  = block_size / sizeof(inode_t);

    size_t desc_bg_num = (inode_num - 1) / fs->inodes_per_group;
    size_t local_inode_num = (inode_num - 1) % fs->inodes_per_group;
    size_t desc_bg_id  = cur_desc_entry_id + desc_bg_num / db_desc_per_block;
    size_t desc_bg_pos_in_id = desc_bg_num % db_desc_per_block;

    ssize_t read = read_block(desc_bg_id, fs, buff);
    if (read < 0)
    {
        fprintf(stderr, "[get_ext2_inode] %ld: "
                        "reading block %lu group table failed\n",
                        read, desc_bg_id);
        free(buff);
        return E_BADALLOC;
    }

    group_desc_t cur_bg = ((group_desc_t*)buff)[desc_bg_pos_in_id];
    size_t inode_id = __le32_to_cpu(cur_bg.bg_inode_table) +
                      local_inode_num / inodes_per_block;
    size_t inode_pos_in_id = local_inode_num % inodes_per_block;

    read = read_block(inode_id, fs, buff);
    if (read < 0)
    {
        fprintf(stderr, "[get_ext2_inode] %ld: "
                        "reading inode by id %lu failed\n", read, inode_id);
        free(buff);
        return E_BADALLOC;
    }

    memcpy(ret_inode, &((inode_t*)buff)[inode_pos_in_id], sizeof(inode_t));

    free(buff);
    return E_SUCCESS;
}

static int parse_dir_block(uint8_t* buff, size_t block_size)
{
    assert(buff != NULL);

    uint32_t cur_pos = 0;
    ext2_dir_entry temp;

    ////////////////////////////////////////////////////////////////////////////
    // len must be no longer than 255 bytes - spec.
    ////////////////////////////////////////////////////////////////////////////
    char print_buff[256] = {};

    while (cur_pos < block_size)
    {
        temp = *((ext2_dir_entry*)(buff + cur_pos));
////////////////////////////////////////////////////////////////////////////////
        uint16_t size = __le16_to_cpu(temp.name_len);
        memcpy(print_buff, ((ext2_dir_entry*)(buff + cur_pos))->name, size);
        print_buff[size] = '\0';
        printf("inode: %d name: %s\n", __le32_to_cpu(temp.inode), print_buff);
////////////////////////////////////////////////////////////////////////////////
        cur_pos += __le16_to_cpu(temp.rec_len);
        //printf("cur_pos = %d\n", cur_pos);
    }

    if (cur_pos > block_size)
        return E_ERROR;

    return E_SUCCESS;
}

static int parse_dir_block_2(uint8_t* buff, size_t block_size)
{
    assert(buff != NULL);

    uint32_t cur_pos = 0;
    ext2_dir_entry_2 temp;

    ////////////////////////////////////////////////////////////////////////////
    // len must be no longer than 255 bytes - spec.
    ////////////////////////////////////////////////////////////////////////////
    char print_buff[256] = {};

    while (cur_pos < block_size)
    {
        temp = *((ext2_dir_entry_2*)(buff + cur_pos));
////////////////////////////////////////////////////////////////////////////////
        uint16_t size = __le16_to_cpu(temp.name_len);
        memcpy(print_buff, ((ext2_dir_entry*)(buff + cur_pos))->name, size);
        print_buff[size] = '\0';
        printf("inode %d file_type %d name %s\n",
               __le32_to_cpu(temp.inode), temp.file_type, print_buff);
////////////////////////////////////////////////////////////////////////////////
        cur_pos += __le16_to_cpu(temp.rec_len);
    }

    if (cur_pos > block_size)
        return E_ERROR;

    return E_SUCCESS;
}

static int parse_inderect_block(ext2_fs_t* fs, uint32_t id,
                                uint32_t* curr_block_num, uint8_t* buff)
{
    assert(buff != NULL);
    assert(fs != NULL);
    assert(curr_block_num != NULL);

    errno = 0;
    uint32_t* id_buff = (uint32_t*) malloc(fs->block_size);
    if (id_buff == NULL)
    {
        perror("[parse_inderect_block]"
               "Allocaton of normal block buffer failed\n");
        return E_BADALLOC;
    }


    ssize_t ret = read_block(id, fs, (uint8_t*)id_buff);
    if (ret < 0)
    {
        fprintf(stderr, "[parse_inderect_block] %ld: "
                        "reading inderect block failed\n", ret);
        free(id_buff);
        return E_BADIO;
    }
    (*curr_block_num)--;

    uint32_t ids_per_block = fs->block_size / 4;
    for (uint32_t i = 0; i < ids_per_block && *curr_block_num > 0; i++)
    {
        int ret = read_block(id_buff[i], fs, buff);
        if (ret < 0)
        {
            fprintf(stderr, "[parse_inderect_block] %d:"
                            "read normal block failed\n", ret);
            free(id_buff);
            return E_BADIO;
        }

        if (fs->revision == EXT2_GOOD_OLD_REV)
            ret = parse_dir_block(buff, fs->block_size);
        else
            ret = parse_dir_block_2(buff, fs->block_size);

        if (ret != E_SUCCESS)
        {
            fprintf(stderr, "[parse_inderect_block] %d: "
                            "parse normal block failed\n", ret);
            free(id_buff);
            return E_ERROR;
        }

        (*curr_block_num)--;
    }

    free(id_buff);
    return E_SUCCESS;
}

static int parse_2_inderect_block(ext2_fs_t* fs, uint32_t id,
                                  uint32_t* curr_block_num, uint8_t* buff)
{
    assert(fs != NULL);
    assert(curr_block_num != NULL);
    assert(buff != NULL);

    errno = 0;
    uint32_t* id_buff = (uint32_t*) malloc(fs->block_size);
    if (id_buff == NULL)
    {
        perror("[parse_2_inderect_block]"
               "Allocaton of normal block buffer failed\n");
        return E_BADALLOC;
    }


    ssize_t ret = read_block(id, fs, (uint8_t*)id_buff);
    if (ret < 0)
    {
        fprintf(stderr, "[parse_2_inderect_block] %ld: "
                        "reading inderect block failed\n", ret);
        free(id_buff);
        return E_BADIO;
    }
    (*curr_block_num)--;

    uint32_t ids_per_block = fs->block_size / 4;
    for (uint32_t i = 0; i < ids_per_block && *curr_block_num > 0; i++)
    {
        int ret = parse_inderect_block(fs, id_buff[i], curr_block_num, buff);
        if (ret != E_SUCCESS)
        {
            fprintf(stderr, "[parse_2_inderect_block] %d: "
                            "parse normal block failed\n", ret);
            free(id_buff);
            return E_ERROR;
        }
    }

    free(id_buff);
    return E_SUCCESS;
}

static int parse_3_inderect_block(ext2_fs_t* fs, uint32_t id,
                                  uint32_t* curr_block_num, uint8_t* buff)
{
    assert(fs != NULL);
    assert(curr_block_num != NULL);
    assert(buff != NULL);

    errno = 0;
    uint32_t* id_buff = (uint32_t*) malloc(fs->block_size);
    if (id_buff == NULL)
    {
        perror("[parse_3_inderect_block]"
               "Allocaton of normal block buffer failed\n");
        return E_BADALLOC;
    }

    ssize_t ret = read_block(id, fs, (uint8_t*)id_buff);
    if (ret < 0)
    {
        fprintf(stderr, "[parse_3_inderect_block] %ld: "
                        "reading inderect block failed\n", ret);
        free(id_buff);
        return E_BADIO;
    }
    (*curr_block_num)--;

    uint32_t ids_per_block = fs->block_size / 4;
    for (uint32_t i = 0; i < ids_per_block && *curr_block_num > 0; i++)
    {
        int ret = parse_2_inderect_block(fs, id_buff[i], curr_block_num, buff);
        if (ret != E_SUCCESS)
        {
            fprintf(stderr, "[parse_3_inderect_block] %d: "
                            "parse normal block failed\n", ret);
            free(id_buff);
            return E_ERROR;
        }
    }

    free(id_buff);
    return E_SUCCESS;
}

static int read_dir(ext2_fs_t* fs, inode_t* inode)
{
    assert(fs != NULL);
    assert(inode != NULL);

    if (fs->revision != EXT2_GOOD_OLD_REV && fs->revision != EXT2_DYNAMIC_REV)
    {
        fprintf(stderr, "[read_dir] Unsupported revision %u\n", fs->revision);
        return E_ERROR;
    }

    uint8_t* buff = (uint8_t*) malloc(fs->block_size);
    if (buff == NULL)
    {
        perror("[read_dir] Allocation of buffer failed\n");
        return E_BADALLOC;
    }

    uint32_t curr_block_num = __le32_to_cpu(inode->i_blocks) /
                              (fs->block_size / 512);
    Dprintf("inode block number = %d\n", curr_block_num);

////////////////////////////////////////////////////////////////////////////////
// normal blocks
////////////////////////////////////////////////////////////////////////////////
    for (uint32_t i = 0; i < 12 && curr_block_num > 0; i++)
    {
        int ret = read_block(__le32_to_cpu(inode->i_block[i]), fs, buff);
        if (ret < 0)
        {
            fprintf(stderr, "[read_dir] %d: read normal block failed\n", ret);
            free(buff);
            return E_BADIO;
        }

        if (fs->revision == EXT2_GOOD_OLD_REV)
            ret = parse_dir_block(buff, fs->block_size);
        else
            ret = parse_dir_block_2(buff, fs->block_size);

        if (ret != E_SUCCESS)
        {
            fprintf(stderr, "[read_dir] %d: parse normal block failed\n", ret);
            free(buff);
            return E_ERROR;
        }

        curr_block_num--;
    }

    if (curr_block_num > 0)
    {
        int ret = parse_inderect_block(fs, __le32_to_cpu(inode->i_block[12]),
                                       &curr_block_num, buff);
        if (ret != E_SUCCESS)
        {
            fprintf(stderr, "[read_dir] %d: read inderect block failed\n", ret);
            free(buff);
            return E_ERROR;
        }
    }

    if (curr_block_num > 0)
    {
        int ret = parse_2_inderect_block(fs, __le32_to_cpu(inode->i_block[13]),
                                         &curr_block_num, buff);
        if (ret != E_SUCCESS)
        {
            fprintf(stderr, "[read_dir] %d: "
                            "read doubly-inderect block failed\n", ret);
            free(buff);
            return E_ERROR;
        }
    }

    if (curr_block_num > 0)
    {
        int ret = parse_3_inderect_block(fs, __le32_to_cpu(inode->i_block[14]),
                                         &curr_block_num, buff);
        if (ret != E_SUCCESS)
        {
            fprintf(stderr, "[read_dir] %d: "
                            "read triply-inderect block failed\n", ret);
            free(buff);
            return E_ERROR;
        }
    }

    return E_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// regular file
////////////////////////////////////////////////////////////////////////////////
static int read_inderect_block(ext2_fs_t* fs, uint32_t id, uint8_t* file,
                               uint32_t* cur_pos, uint32_t* remain_size,
                               uint8_t* buff)
{
    assert(fs != NULL);
    assert(file != NULL);
    assert(cur_pos != NULL);
    assert(remain_size != NULL);
    assert(buff != NULL);

    errno = 0;
    uint32_t* id_buff = (uint32_t*) malloc(fs->block_size);
    if (id_buff == NULL)
    {
        perror("[read_inderect_block]"
               "Allocaton of normal block buffer failed\n");
        return E_BADALLOC;
    }

    ssize_t ret = read_block(id, fs, (uint8_t*)id_buff);
    if (ret < 0)
    {
        fprintf(stderr, "[read_inderect_block] %ld: "
                        "reading inderect block failed\n", ret);
        free(id_buff);
        return E_BADIO;
    }

    uint32_t ids_per_block = fs->block_size / 4;
    for (uint32_t i = 0; i < ids_per_block && *remain_size > 0; i++)
    {
        int ret = read_block(id_buff[i], fs, buff);
        if (ret < 0)
        {
            fprintf(stderr, "[read_inderect_block] %d: "
                            "read normal block failed\n", ret);
            free(id_buff);
            return E_BADIO;
        }

        uint32_t cur_read = fs->block_size;
        if (*remain_size < cur_read)
            cur_read = *remain_size;

        memcpy(file + *cur_pos, buff, cur_read);

        *remain_size -= cur_read;
        *cur_pos     += cur_read;
    }

    free(id_buff);
    return E_SUCCESS;
}

static int read_2_inderect_block(ext2_fs_t* fs, uint32_t id, uint8_t* file,
                                 uint32_t* cur_pos, uint32_t* remain_size,
                                 uint8_t* buff)
{
    assert(fs != NULL);
    assert(file != NULL);
    assert(cur_pos != NULL);
    assert(remain_size != NULL);
    assert(buff != NULL);

    errno = 0;
    uint32_t* id_buff = (uint32_t*) malloc(fs->block_size);
    if (id_buff == NULL)
    {
        perror("[read_2_inderect_block]"
               "Allocaton of normal block buffer failed\n");
        return E_BADALLOC;
    }

    ssize_t ret = read_block(id, fs, (uint8_t*)id_buff);
    if (ret < 0)
    {
        fprintf(stderr, "[read_2_inderect_block] %ld: "
                        "reading inderect block failed\n", ret);
        free(id_buff);
        return E_BADIO;
    }

    uint32_t ids_per_block = fs->block_size / 4;
    for (uint32_t i = 0; i < ids_per_block && *remain_size > 0; i++)
    {
        int ret = read_inderect_block(fs, id_buff[i], file, cur_pos,
                                      remain_size, buff);
        if (ret < 0)
        {
            fprintf(stderr, "[read_2_inderect_block] %d: "
                            "read indirect block failed\n", ret);
            free(id_buff);
            return E_BADIO;
        }
    }

    free(id_buff);
    return E_SUCCESS;
}

static int read_3_inderect_block(ext2_fs_t* fs, uint32_t id, uint8_t* file,
                                 uint32_t* cur_pos, uint32_t* remain_size,
                                 uint8_t* buff)
{
    assert(fs != NULL);
    assert(file != NULL);
    assert(cur_pos != NULL);
    assert(remain_size != NULL);
    assert(buff != NULL);

    errno = 0;
    uint32_t* id_buff = (uint32_t*) malloc(fs->block_size);
    if (id_buff == NULL)
    {
        perror("[read_3_inderect_block]"
               "Allocaton of normal block buffer failed\n");
        return E_BADALLOC;
    }

    ssize_t ret = read_block(id, fs, (uint8_t*)id_buff);
    if (ret < 0)
    {
        fprintf(stderr, "[read_3_inderect_block] %ld: "
                        "reading inderect block failed\n", ret);
        free(id_buff);
        return E_BADIO;
    }

    uint32_t ids_per_block = fs->block_size / 4;
    for (uint32_t i = 0; i < ids_per_block && *remain_size > 0; i++)
    {
        int ret = read_2_inderect_block(fs, id_buff[i], file, cur_pos,
                                        remain_size, buff);
        if (ret < 0)
        {
            fprintf(stderr, "[read_3_inderect_block] %d: "
                            "read indirect block failed\n", ret);
            free(id_buff);
            return E_BADIO;
        }
    }

    free(id_buff);
    return E_SUCCESS;
}

// not static because I think it can be used outside of this lib
ssize_t read_reg_file(ext2_fs_t* fs, inode_t* inode, uint8_t* file)
{
    if (fs == NULL)
    {
        fprintf(stderr, "[read_reg_file] Bad input fs pointer\n");
        return E_BADARGS;
    }

    if (inode == NULL)
    {
        fprintf(stderr, "[read_reg_file] Bad input inode pointer\n");
        return E_BADARGS;
    }

    if (file == NULL)
    {
        fprintf(stderr, "[read_reg_file] Bad input buffer for file\n");
        return E_BADARGS;
    }

    uint32_t remain_size = __le32_to_cpu(inode->i_size);

    uint8_t* buff = (uint8_t*) malloc(fs->block_size);
    if (buff == NULL)
    {
        perror("[read_reg_file] Allocation of buffer failed\n");
        return E_BADALLOC;
    }

    uint32_t cur_pos = 0;
    for (uint32_t i = 0; i < 12 && remain_size > 0; i++)
    {
        int ret = read_block(__le32_to_cpu(inode->i_block[i]), fs, buff);
        if (ret < 0)
        {
            fprintf(stderr, "[read_reg_file] %d: "
                            "read normal block failed\n", ret);
            free(buff);
            return E_BADIO;
        }

        uint32_t cur_read = fs->block_size;
        if (remain_size < cur_read)
            cur_read = remain_size;

        memcpy(file + cur_pos, buff, cur_read);

        remain_size -= cur_read;
        cur_pos     += cur_read;
    }

    if (remain_size > 0)
    {
        int ret = read_inderect_block(fs, __le32_to_cpu(inode->i_block[12]),
                                      file, &cur_pos, &remain_size, buff);
        if (ret != E_SUCCESS)
        {
            fprintf(stderr, "[read_reg_file] %d: "
                            "read inderect block failed\n", ret);
            free(buff);
            return E_ERROR;
        }
    }

    if (remain_size > 0)
    {
        int ret = read_2_inderect_block(fs, __le32_to_cpu(inode->i_block[13]),
                                        file, &cur_pos, &remain_size, buff);
        if (ret != E_SUCCESS)
        {
            fprintf(stderr, "[read_reg_file] %d: "
                            "read doubly-inderect block failed\n", ret);
            free(buff);
            return E_ERROR;
        }
    }

    if (remain_size > 0)
    {
        int ret = read_3_inderect_block(fs, __le32_to_cpu(inode->i_block[12]),
                                        file, &cur_pos, &remain_size, buff);
        if (ret != E_SUCCESS)
        {
            fprintf(stderr, "[read_reg_file] %d: "
                            "read triply-inderect block failed\n", ret);
            free(buff);
            return E_ERROR;
        }
    }

    free(buff);
    return E_SUCCESS;
}

int read_inode(ext2_fs_t* fs, inode_t* inode)
{
    if (fs == NULL)
    {
        fprintf(stderr, "[read_inode] Bad input fs pointer\n");
        return E_BADARGS;
    }

    if (inode == NULL)
    {
        fprintf(stderr, "[read_inode] Bad input inode structure pointer\n");
        return E_BADARGS;
    }

    uint16_t mode = __le16_to_cpu(inode->i_mode);

    if (mode & EXT2_S_IFDIR)
    {
        int ret = read_dir(fs, inode);
        return ret;
    }
    else if (mode & EXT2_S_IFREG)
    {
        uint32_t file_size = __le32_to_cpu(inode->i_size);
        errno = 0;
        uint8_t* file = (uint8_t*) malloc(file_size);
        if (file == NULL)
        {
            perror("[read_inode] allocation of file buffer returned error\n");
            return E_BADALLOC;
        }

        ssize_t ret = read_reg_file(fs, inode, file);
        if (ret < 0)
        {
            fprintf(stderr, "[read_inode] read regular file returned error\n");
            free(file);
            return E_BADIO;
        }

        // Can return buffer but I want identic interface as read_dir
        write(1, file, file_size);
        free(file);
        return E_SUCCESS;
    }

    fprintf(stderr, "[read_inode] Uncompatible inode mode %.4X\n", mode);
    return E_ERROR;
}

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "[main] Bad number of input arguments."
                        "Try ./read_ext2 device inode_number\n");
        exit(EXIT_FAILURE);
    }

    errno = 0;
    int dev_fd = open(argv[1], O_RDONLY); // will fail if file doesn't exist
    if (dev_fd < 0)
    {
        perror("[main] Opening device failed\n");
        exit(EXIT_FAILURE);
    }

    super_block_t super_block;
    int err = get_ext2_superblock(dev_fd, &super_block);
    if (err != E_SUCCESS)
    {
        fprintf(stderr, "[main] %d: Getting superblock failed\n", err);
        exit(EXIT_FAILURE);
    }

    if (__le32_to_cpu(super_block.s_rev_level) != EXT2_GOOD_OLD_REV &&
        super_block.s_feature_incompat != 0)
    {
        fprintf(stderr, "[main] Unsupported file system: incopatible features: "
                        "0x%.8X\n", __le32_to_cpu(super_block.s_feature_incompat));
        exit(EXIT_FAILURE);
    }

    ext2_fs_t fs = {
        fs.dev_fd           = dev_fd,
        fs.sb               = &super_block,
        fs.revision         = __le32_to_cpu(super_block.s_rev_level),
        fs.block_size       = ((size_t)1024) <<
                              __le32_to_cpu(super_block.s_log_block_size),
        fs.inode_size       = EXT2_GOOD_OLD_INODE_SIZE,
        fs.blocks_per_group = __le32_to_cpu(super_block.s_blocks_per_group),
        fs.inodes_per_group = __le32_to_cpu(super_block.s_inodes_per_group),
        fs.num_inodes       = __le32_to_cpu(super_block.s_inodes_count),
        fs.num_blocks       = __le32_to_cpu(super_block.s_blocks_count)
    };

    if (fs.revision != EXT2_GOOD_OLD_REV)
        fs.inode_size = __le32_to_cpu(super_block.s_inode_size);

    Dprintf("block_size = %lu\n", fs.block_size);

    errno = 0;
    long long int inode_number = strtoll(argv[2], NULL, 10);
    if (errno != 0)
    {
        perror("[main] Reading inode_number failed\n");
        exit(EXIT_FAILURE);
    }

    inode_t req_inode;
    err = get_ext2_inode(&fs, inode_number, &req_inode);
    if (err != E_SUCCESS)
    {
        fprintf(stderr, "[main] %d: Gettind inode by inode_num failed\n", err);
        exit(EXIT_FAILURE);
    }

    Dprintf("i_mode = 0x%.4X\n", __le16_to_cpu(req_inode.i_mode));
    Dprintf("i_size = %d\n", __le32_to_cpu(req_inode.i_size));

    err = read_inode(&fs, &req_inode);
    if (err != E_SUCCESS)
    {
        fprintf(stderr, "[main] %d: reading inode failed\n", err);
        exit(EXIT_FAILURE);
    }

    close(dev_fd);
    return 0;
}
