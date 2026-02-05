#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include "filesys.h"
#include "file_op.h"
#include "locks.h"

int fs_fd = -1;
minifs_superblock superblock;

void die(const char *msg) {
    perror(msg);
    exit(1);
}

ssize_t my_pread(int fd, void *buf, size_t count, off_t off) {
    size_t done = 0;
    char *p = buf;
    while (done < count) {
        ssize_t r = pread(fd, p + done, count - done, off + done);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) break;
        done += (size_t)r;
    }
    return (ssize_t)done;
}

ssize_t my_pwrite(int fd, const void *buf, size_t count, off_t off) {
    size_t done = 0;
    const char *p = buf;
    while (done < count) {
        ssize_t w = pwrite(fd, p + done, count - done, off + done);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (size_t)w;
    }
    return (ssize_t)done;
}

void fs_read(void *buf, size_t n, uint64_t off) {
    if (my_pread(fs_fd, buf, n, (off_t)off) != (ssize_t)n)
        die("fs_read");
}

void fs_write(const void *buf, size_t n, uint64_t off) {
    if (my_pwrite(fs_fd, buf, n, (off_t)off) != (ssize_t)n)
        die("fs_write");
}

void init_filesystem(void) {
    struct stat st;
    int exists = (stat(FS_PATH, &st) == 0);

    int fd = lock_super();

    fs_fd = open(FS_PATH, O_RDWR | O_CREAT, 0644);
    if (fs_fd < 0) die("open filesys.db");

    if (!exists || st.st_size != FS_SIZE) {
        if (ftruncate(fs_fd, FS_SIZE) != 0) die("ftruncate filesys.db");

        memset(&superblock, 0, sizeof(superblock));
        superblock.magic = MAGIC;
        superblock.version = VERSION;
        superblock.root_dir_offset = 0;
        superblock.file_count = 0;
        
        superblock.freelist_head = BLOCK_SIZE;
        
        uint64_t num_blocks = FS_SIZE / BLOCK_SIZE;
        
        minifs_freelist header;
        header.next_free = 0;
        header.block_count = num_blocks - 1;
        header.start_offset = BLOCK_SIZE;
        header.end_offset = FS_SIZE;
        
        fs_write(&header, sizeof(header), superblock.freelist_head);
        fs_write(&superblock, sizeof(superblock), 0);
    } else {
        fs_read(&superblock, sizeof(superblock), 0);
        if ((uint32_t)superblock.magic != MAGIC) {
            fprintf(stderr, "Bad filesystem magic\n");
            exit(1);
        }
    }

    unlock_super(fd);
}

int get_fs_stat_minifs(uint64_t *out_used, uint64_t *out_free, int *out_count) {
    uint64_t used = 0;
    uint64_t off = superblock.root_dir_offset;
    while (off != 0) {
        minifs_file_entry e;
        fs_read(&e, sizeof(e), off);
        used += calculate_blocks_needed(e.size) * BLOCK_SIZE;
        off = e.next_file;
    }
    if (out_used) *out_used = used;
    if (out_free) *out_free = (uint64_t)FS_SIZE - used;
    if (out_count) *out_count = superblock.file_count;
    return 0;
}

// ======================= PERMISSION CHECK IMPLEMENTATION ====
int check_file_permission(minifs_file_entry *entry, uint32_t uid, uint32_t gid, int action) {
    if (!entry) {
        errno = EINVAL;
        return 0;
    }
    
    // Root user (uid 0) can do anything
    if (uid == 0) {
        return 1;
    }
    
    // Check owner permissions
    if (uid == entry->owner_uid) {
        uint16_t owner_bits = (entry->mode >> 6) & 0x7;  // bits 6-8
        return (owner_bits & action) != 0;
    }
    
    // Check group permissions
    if (is_user_in_group(uid, entry->owner_gid)) {
        uint16_t group_bits = (entry->mode >> 3) & 0x7;  // bits 3-5
        return (group_bits & action) != 0;
    }
    
    // Check other permissions
    uint16_t other_bits = entry->mode & 0x7;  // bits 0-2
    return (other_bits & action) != 0;
}
