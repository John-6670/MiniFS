#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#define FS_PATH "filesys.db"
#define FS_SIZE (4 * 1024 * 1024) // 4 MB
#define BLOCK_SIZE 4096 // 4 KB blocks
#define MAGIC 0xDEADBEEF
#define VERSION 1
#define MAX_NAME 64

#define FLAG_CREAT  0x01
#define FLAG_WRITE  0x02

// ======================= PERMISSION BITS ================
#define PERM_READ           4    // read permission (any user level)
#define PERM_WRITE          2    // write permission (any user level)
#define PERM_EXEC           1    // execute permission (any user level)

#define PERM_OWNER_READ     0400
#define PERM_OWNER_WRITE    0200
#define PERM_OWNER_EXEC     0100
#define PERM_GROUP_READ     0040
#define PERM_GROUP_WRITE    0020
#define PERM_GROUP_EXEC     0010
#define PERM_OTHER_READ     0004
#define PERM_OTHER_WRITE    0002
#define PERM_OTHER_EXEC     0001

#pragma pack(push,1)
typedef struct {
    int32_t magic;
    int32_t version;
    uint64_t root_dir_offset;
    int32_t file_count;
    uint64_t last_file;
    uint64_t freelist_head;
} minifs_superblock;

typedef struct {
    char name[MAX_NAME];
    int32_t type;
    uint16_t mode;                  // Unix-style permissions
    uint32_t owner_uid;             // File owner user ID
    uint32_t owner_gid;             // File owner group ID
    uint64_t size;
    uint64_t next_file;
    uint64_t last_block;
    time_t created_at;              // Creation timestamp
    time_t modified_at;             // Last modification timestamp
} minifs_file_entry;

typedef struct {
    uint64_t next_block;     // Offset to next block (0 if last)
} minifs_block_header;

typedef struct {
    uint64_t start_offset;
    uint64_t end_offset;
    uint32_t block_count;
    uint64_t next_free;     // Offset to next freelist node (0 if end)
} minifs_freelist;

extern int fs_fd;
extern minifs_superblock superblock;

void die(const char *msg);
ssize_t my_pread(int fd, void *buf, size_t count, off_t off);
ssize_t my_pwrite(int fd, const void *buf, size_t count, off_t off);
void fs_read(void *buf, size_t n, uint64_t off);
void fs_write(const void *buf, size_t n, uint64_t off);
void init_filesystem(void);
int get_fs_stat_minifs(uint64_t *out_used, uint64_t *out_free, int *out_count);
uint64_t allocate_blocks(uint64_t count);
void free_blocks(uint64_t offset, uint64_t count);

// ======================= PERMISSION CHECK FUNCTIONS ====
int check_file_permission(minifs_file_entry *entry, uint32_t uid, uint32_t gid, int action);
// action: 1 = read, 2 = write, 4 = execute
int is_user_in_group(uint32_t uid, uint32_t gid);

#define DATA_PER_BLOCK (BLOCK_SIZE - sizeof(minifs_block_header))
#define DATA_PER_FIRST_BLOCK (BLOCK_SIZE - sizeof(minifs_file_entry) - sizeof(minifs_block_header))

#endif
