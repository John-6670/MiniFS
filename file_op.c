#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include "filesys.h"
#include "file_op.h"
#include "locks.h"

static minifs_file_entry curr_entry;
static uint64_t curr_entry_offset = 0;
static int curr_opened = 0;
static uint32_t current_uid = 0;  // Current user (default root)
static uint32_t current_gid = 0;  // Current group (default root)

int find_file_by_name(const char *name, minifs_file_entry *out_entry, uint64_t *out_offset) {
    uint64_t off = superblock.root_dir_offset;
    while (off != 0) {
        minifs_file_entry e;
        fs_read(&e, sizeof(e), off);
        if (strncmp(e.name, name, MAX_NAME) == 0) {
            if (out_entry) *out_entry = e;
            if (out_offset) *out_offset = off;
            return 1;
        }
        off = e.next_file;
    }
    return 0;
}

void initial_block(uint64_t offset, uint64_t next_block, int first_block) {
    minifs_block_header header;
    header.next_block = next_block;
    
    if (first_block) {
        // For first block: file_entry + header + data
        minifs_file_entry entry;
        memset(&entry, 0, sizeof(entry));
        fs_write(&entry, sizeof(entry), offset);
        fs_write(&header, sizeof(header), offset + sizeof(entry));
        // Zero out the data portion
        char zero_data[DATA_PER_FIRST_BLOCK];
        memset(zero_data, 0, DATA_PER_FIRST_BLOCK);
        fs_write(zero_data, DATA_PER_FIRST_BLOCK, offset + sizeof(entry) + sizeof(header));
    } else {
        // For other blocks: header + data
        fs_write(&header, sizeof(header), offset);
        char zero_data[DATA_PER_BLOCK];
        memset(zero_data, 0, DATA_PER_BLOCK);
        fs_write(zero_data, DATA_PER_BLOCK, offset + sizeof(header));
    }
}

void add_to_freelist(uint64_t offset) {
    int fd = lock_super();

    uint64_t new_start = offset;
    uint64_t new_end = offset + BLOCK_SIZE;
    
    uint64_t prev_offset = 0;
    uint64_t curr_offset = superblock.freelist_head;
    
    while (curr_offset != 0) {
        minifs_freelist curr_free;
        fs_read(&curr_free, sizeof(curr_free), curr_offset);
        
        if (new_end == curr_free.start_offset) {
            curr_free.start_offset = new_start;
            curr_free.block_count++;
            
            if (prev_offset != 0) {
                minifs_freelist prev_free;
                fs_read(&prev_free, sizeof(prev_free), prev_offset);
                
                if (prev_free.end_offset == new_start) {
                    prev_free.end_offset = curr_free.end_offset;
                    prev_free.block_count += curr_free.block_count;
                    prev_free.next_free = curr_free.next_free;
                    fs_write(&prev_free, sizeof(prev_free), prev_offset);
                    unlock_super(fd);
                    return;
                }
            }
            
            fs_write(&curr_free, sizeof(curr_free), curr_offset);
            unlock_super(fd);
            return;
        }
        else if (curr_free.end_offset == new_start) {
            curr_free.end_offset = new_end;
            curr_free.block_count++;
            
            if (curr_free.next_free != 0) {
                minifs_freelist next_free;
                fs_read(&next_free, sizeof(next_free), curr_free.next_free);
                
                if (new_end == next_free.start_offset) {
                    curr_free.end_offset = next_free.end_offset;
                    curr_free.block_count += next_free.block_count;
                    curr_free.next_free = next_free.next_free;
                }
            }
            
            fs_write(&curr_free, sizeof(curr_free), curr_offset);
            unlock_super(fd);
            return;
        }
        else if (new_start < curr_free.start_offset) {
            minifs_freelist new_free;
            new_free.start_offset = new_start;
            new_free.end_offset = new_end;
            new_free.block_count = 1;
            new_free.next_free = curr_offset;
            
            fs_write(&new_free, sizeof(new_free), offset);
            
            if (prev_offset == 0) {
                superblock.freelist_head = offset;
            } else {
                minifs_freelist prev_free;
                fs_read(&prev_free, sizeof(prev_free), prev_offset);
                prev_free.next_free = offset;
                fs_write(&prev_free, sizeof(prev_free), prev_offset);
            }
            
            fs_write(&superblock, sizeof(superblock), 0);
            unlock_super(fd);
            return;
        }
        
        prev_offset = curr_offset;
        curr_offset = curr_free.next_free;
    }
    
    minifs_freelist new_free;
    new_free.start_offset = new_start;
    new_free.end_offset = new_end;
    new_free.block_count = 1;
    new_free.next_free = 0;
    
    fs_write(&new_free, sizeof(new_free), offset);
    
    if (superblock.freelist_head == 0) {
        superblock.freelist_head = offset;
    } else if (prev_offset != 0) {
        minifs_freelist prev_free;
        fs_read(&prev_free, sizeof(prev_free), prev_offset);
        prev_free.next_free = offset;
        fs_write(&prev_free, sizeof(prev_free), prev_offset);
    }
    
    fs_write(&superblock, sizeof(superblock), 0);
    unlock_super(fd);
}

uint64_t get_from_freelist(void) {
    if (superblock.freelist_head == 0) {
        errno = ENOSPC;
        return 0;
    }

    int fd = lock_super();
    
    uint64_t free_offset = superblock.freelist_head;
    minifs_freelist free_node;
    fs_read(&free_node, sizeof(free_node), free_offset);
    
    uint64_t allocated_offset = free_node.start_offset;
    
    if (allocated_offset + BLOCK_SIZE > FS_SIZE) {
        errno = ENOSPC;
        unlock_super(fd);
        return 0;
    }
    
    free_node.start_offset += BLOCK_SIZE;
    free_node.block_count--;
    
    if (free_node.block_count == 0) {
        superblock.freelist_head = free_node.next_free;
        fs_write(&superblock, sizeof(superblock), 0);
        
        char zero[sizeof(minifs_freelist)];
        memset(zero, 0, sizeof(zero));
        fs_write(zero, sizeof(zero), free_offset);
    } else {
        uint64_t new_free_offset = free_node.start_offset;
        fs_write(&free_node, sizeof(free_node), new_free_offset);
        superblock.freelist_head = new_free_offset;
        fs_write(&superblock, sizeof(superblock), 0);
    }
    
    unlock_super(fd);

    return allocated_offset;
}

uint64_t alloc(int blocks_needed, int first_block) {
    if (blocks_needed == 0) return 0;

    uint64_t first_offset = 0;
    uint64_t prev_offset = 0;
    
    for (int i = 0; i < blocks_needed; i++) {
        uint64_t off = get_from_freelist();
        if (off == 0) {
            // Failed to allocate - return 0
            return 0;
        }
        
        if (i == 0) {
            first_offset = off;
            // Initialize the first block
            initial_block(off, 0, first_block);
            prev_offset = off;
        } else {
            // Initialize current block
            initial_block(off, 0, 0);
            
            // Link previous block to this one
            minifs_block_header header;
            if (prev_offset == first_offset && first_block) {
                fs_read(&header, sizeof(header), prev_offset + sizeof(minifs_file_entry));
                header.next_block = off;
                fs_write(&header, sizeof(header), prev_offset + sizeof(minifs_file_entry));
            } else {
                fs_read(&header, sizeof(header), prev_offset);
                header.next_block = off;
                fs_write(&header, sizeof(header), prev_offset);
            }
            
            prev_offset = off;
        }
    }

    return first_offset;
}

void free_block(uint64_t offset) {
    char zero[BLOCK_SIZE];
    memset(zero, 0, BLOCK_SIZE);
    fs_write(zero, BLOCK_SIZE, offset);
    add_to_freelist(offset);
}

void free_minifs(uint64_t offset, uint64_t count) {
    minifs_block_header header;
    fs_read(&header, sizeof(header), offset);
    
    for (uint64_t i = 0; i < count; i++) {
        free_block(offset + i * BLOCK_SIZE);
    }
}

uint64_t calculate_blocks_needed(uint64_t file_size) {    
    if (file_size <= DATA_PER_FIRST_BLOCK) {
        return 1;
    }
    
    uint64_t remaining = file_size - DATA_PER_FIRST_BLOCK;
    uint64_t additional_blocks = (remaining + DATA_PER_BLOCK - 1) / DATA_PER_BLOCK;
    return 1 + additional_blocks;
}

int open_minifs(const char *name, int flags) {
    if (curr_opened) {
        fprintf(stderr, "A file is already opened\n");
        return -1;
    }
    if (!name || strlen(name) == 0 || strlen(name) >= MAX_NAME) {
        errno = EINVAL;
        return -1;
    }
    if (strchr(name, '/')) {
        errno = EINVAL;
        return -1;
    }

    uint64_t off = 0;
    if (find_file_by_name(name, &curr_entry, &off)) {
        if (flags & FLAG_CREAT) {
            errno = EEXIST;
            return -1;
        }

        curr_entry_offset = off;
        curr_opened = 1;
        if (curr_entry.size > 0) {
            uint64_t block_offset = curr_entry_offset;
            minifs_block_header header;
            fs_read(&header, sizeof(header), block_offset + sizeof(minifs_file_entry));
            
            while (header.next_block != 0) {
                block_offset = header.next_block;
                fs_read(&header, sizeof(header), block_offset);
            }
            
            curr_entry.last_block = block_offset;
            // Update the file entry with correct last_block
            fs_write(&curr_entry, sizeof(curr_entry), curr_entry_offset);
        } else {
            curr_entry.last_block = curr_entry_offset;
        }
        
        return 0;
    }

    if (!(flags & FLAG_CREAT)) {
        errno = ENOENT;
        return -1;
    }

    uint64_t entry_block = alloc(1, 1);
    if (entry_block == 0) {
        errno = ENOSPC;
        return -1;
    }

    minifs_file_entry ent;
    memset(&ent, 0, sizeof(ent));
    strncpy(ent.name, name, MAX_NAME - 1);
    ent.type = 1;
    ent.mode = 0644;                    // Default permissions: rw-r--r--
    ent.owner_uid = current_uid;        // Set current user as owner
    ent.owner_gid = current_gid;        // Set current group as owner group
    ent.size = 0;
    ent.next_file = 0;
    ent.last_block = entry_block;
    ent.created_at = time(NULL);
    ent.modified_at = time(NULL);

    minifs_block_header header;
    header.next_block = 0;

    char zero_block[BLOCK_SIZE];
    memset(zero_block, 0, BLOCK_SIZE);
    memcpy(zero_block, &ent, sizeof(ent));
    memcpy(zero_block + sizeof(ent), &header, sizeof(header));
    fs_write(zero_block, BLOCK_SIZE, entry_block);

    if (superblock.root_dir_offset == 0) {
        superblock.root_dir_offset = entry_block;
    } else {
        uint64_t last_off = superblock.last_file;
        minifs_file_entry last_entry;
        fs_read(&last_entry, sizeof(last_entry), last_off);
        last_entry.next_file = entry_block;
        fs_write(&last_entry, sizeof(last_entry), last_off);
    }

    superblock.file_count++;
    superblock.last_file = entry_block;
    fs_write(&superblock, sizeof(superblock), 0);

    curr_entry = ent;
    curr_entry_offset = entry_block;
    curr_opened = 1;
    return 0;
}

ssize_t read_minifs(uint64_t pos, void *buf, size_t n) {
    if (!curr_opened) {
        errno = EBADF;
        return -1;
    }
    if (!buf) {
        errno = EINVAL;
        return -1;
    }

    int fd = lock_read(curr_entry_offset);

    // Check read permission
    if (!check_file_permission(&curr_entry, current_uid, current_gid, PERM_READ)) {
        errno = EACCES;
        fprintf(stderr, "Permission denied: read access not allowed on this file\n");
        unlock(fd);
        return -1;
    }

    if (pos >= curr_entry.size) {
        unlock(fd);
        return 0;
    }
    
    size_t toread = n;
    if (pos + toread > curr_entry.size) {
        toread = (size_t)(curr_entry.size - pos);
    }

    size_t bytes_read = 0;
    uint64_t current_pos = pos;
    uint64_t block_offset = curr_entry_offset;
    
    uint64_t first_block_data_start = sizeof(minifs_file_entry) + sizeof(minifs_block_header);
    uint64_t skip = 0;
    
    if (current_pos < DATA_PER_FIRST_BLOCK) {
        skip = first_block_data_start + current_pos;
        size_t to_read = DATA_PER_FIRST_BLOCK - current_pos;
        if (to_read > toread) to_read = toread;
        
        fs_read((char*)buf + bytes_read, to_read, block_offset + skip);
        bytes_read += to_read;
        current_pos += to_read;
        
        if (bytes_read >= toread) {
            unlock(fd);
            return (ssize_t)bytes_read;
        }
        
        minifs_block_header header;
        fs_read(&header, sizeof(header), block_offset + sizeof(minifs_file_entry));
        block_offset = header.next_block;
    } else {
        current_pos -= DATA_PER_FIRST_BLOCK;
        
        minifs_block_header header;
        fs_read(&header, sizeof(header), block_offset + sizeof(minifs_file_entry));
        block_offset = header.next_block;
        
        while (current_pos >= DATA_PER_BLOCK && block_offset != 0) {
            current_pos -= DATA_PER_BLOCK;
            fs_read(&header, sizeof(header), block_offset);
            block_offset = header.next_block;
        }
    }
    
    while (bytes_read < toread && block_offset != 0) {
        uint64_t data_start = sizeof(minifs_block_header);
        size_t to_read = DATA_PER_BLOCK - current_pos;
        if (to_read > toread - bytes_read) to_read = toread - bytes_read;
        
        fs_read((char*)buf + bytes_read, to_read, block_offset + data_start + current_pos);
        bytes_read += to_read;
        current_pos = 0;
        
        if (bytes_read >= toread) break;
        
        minifs_block_header header;
        fs_read(&header, sizeof(header), block_offset);
        block_offset = header.next_block;
    }

    unlock(fd);
    
    return (ssize_t)bytes_read;
}

ssize_t write_minifs(uint64_t pos, const void *buf, size_t n) {
    if (!curr_opened) {
        errno = EBADF;
        return -1;
    }
    if (!buf && n != 0) {
        errno = EINVAL;
        return -1;
    }

    int fd = lock_write(curr_entry_offset);

    sleep(5);

    // Check write permission
    if (!check_file_permission(&curr_entry, current_uid, current_gid, PERM_WRITE)) {
        errno = EACCES;
        fprintf(stderr, "Permission denied: write access not allowed on this file\n");
        return -1;
    }

    uint64_t new_size = pos + n;
    uint64_t blocks_needed = calculate_blocks_needed(new_size);
    uint64_t current_blocks = calculate_blocks_needed(curr_entry.size);
    
    if (blocks_needed > current_blocks) {
        uint64_t blocks_to_allocate = blocks_needed - current_blocks;
        
        // Find the last block in the current chain
        uint64_t last_block_offset = curr_entry_offset;
        minifs_block_header header;
        
        if (current_blocks == 1) {
            // Only first block exists
            fs_read(&header, sizeof(header), last_block_offset + sizeof(minifs_file_entry));
        } else {
            // Navigate to the last block
            fs_read(&header, sizeof(header), last_block_offset + sizeof(minifs_file_entry));
            while (header.next_block != 0) {
                last_block_offset = header.next_block;
                fs_read(&header, sizeof(header), last_block_offset);
            }
        }
        
        // Allocate new blocks
        uint64_t new_blocks_start = alloc(blocks_to_allocate, 0);
        if (new_blocks_start == 0) {
            errno = ENOSPC;
            return -1;
        }
        
        // Link the last existing block to the first new block
        header.next_block = new_blocks_start;
        if (last_block_offset == curr_entry_offset) {
            fs_write(&header, sizeof(header), last_block_offset + sizeof(minifs_file_entry));
        } else {
            fs_write(&header, sizeof(header), last_block_offset);
        }
        
        // Update last_block to point to the actual last block
        uint64_t temp_offset = new_blocks_start;
        fs_read(&header, sizeof(header), temp_offset);
        while (header.next_block != 0) {
            temp_offset = header.next_block;
            fs_read(&header, sizeof(header), temp_offset);
        }
        curr_entry.last_block = temp_offset;
        
        fs_write(&curr_entry, sizeof(curr_entry), curr_entry_offset);
        
        fs_read(&curr_entry, sizeof(curr_entry), curr_entry_offset);
    }

    if (blocks_needed < current_blocks) {
        shrink_minifs(new_size, 0);
    }
    
    size_t bytes_written = 0;
    uint64_t current_pos = pos;
    uint64_t block_offset = curr_entry_offset;
    
    uint64_t first_block_data_start = sizeof(minifs_file_entry) + sizeof(minifs_block_header);
    
    if (current_pos < DATA_PER_FIRST_BLOCK) {
        size_t to_write = DATA_PER_FIRST_BLOCK - current_pos;
        if (to_write > n) to_write = n;
        
        fs_write((const char*)buf + bytes_written, to_write, 
                 block_offset + first_block_data_start + current_pos);
        bytes_written += to_write;
        current_pos += to_write;
        
        if (bytes_written >= n) goto update_size;
        
        minifs_block_header header;
        fs_read(&header, sizeof(header), block_offset + sizeof(minifs_file_entry));
        block_offset = header.next_block;
        current_pos = 0;
    } else {
        current_pos -= DATA_PER_FIRST_BLOCK;
        
        minifs_block_header header;
        fs_read(&header, sizeof(header), block_offset + sizeof(minifs_file_entry));
        block_offset = header.next_block;
        
        while (current_pos >= DATA_PER_BLOCK && block_offset != 0) {
            current_pos -= DATA_PER_BLOCK;
            fs_read(&header, sizeof(header), block_offset);
            block_offset = header.next_block;
        }
    }
    
    while (bytes_written < n && block_offset != 0) {
        uint64_t data_start = sizeof(minifs_block_header);
        size_t to_write = DATA_PER_BLOCK - current_pos;
        if (to_write > n - bytes_written) to_write = n - bytes_written;
        
        fs_write((const char*)buf + bytes_written, to_write, 
                 block_offset + data_start + current_pos);
        bytes_written += to_write;
        current_pos = 0;
        
        if (bytes_written >= n) break;
        
        minifs_block_header header;
        fs_read(&header, sizeof(header), block_offset);
        block_offset = header.next_block;
    }

update_size:
    if (new_size > curr_entry.size) {
        curr_entry.size = new_size;
        fs_write(&curr_entry, sizeof(curr_entry), curr_entry_offset);
    }
    
    unlock(fd);

    return (ssize_t)bytes_written;
}

int shrink_minifs(uint64_t new_size, int do_lock) {
    if (!curr_opened) {
        errno = EBADF;
        return -1;
    }
    if (new_size > curr_entry.size) {
        errno = EINVAL;
        return -1;
    }

    int fd = -1;
    if (do_lock) {
        fd = lock_write(curr_entry_offset);
    }

    uint64_t new_blocks = calculate_blocks_needed(new_size);
    uint64_t old_blocks = calculate_blocks_needed(curr_entry.size);
    
    if (new_blocks < old_blocks) {
        uint64_t block_offset = curr_entry_offset;
        minifs_block_header header;
        
        fs_read(&header, sizeof(header), block_offset + sizeof(minifs_file_entry));
        
        for (uint64_t i = 1; i < new_blocks && header.next_block != 0; i++) {
            block_offset = header.next_block;
            fs_read(&header, sizeof(header), block_offset);
        }
        
        uint64_t next_to_free = header.next_block;
        header.next_block = 0;
        fs_write(&header, sizeof(header), block_offset);
        
        while (next_to_free != 0) {
            uint64_t current_free = next_to_free;
            minifs_block_header free_header;
            fs_read(&free_header, sizeof(free_header), current_free);
            next_to_free = free_header.next_block;
            
            free_block(current_free);
        }
    }

    curr_entry.size = new_size;
    fs_write(&curr_entry, sizeof(curr_entry), curr_entry_offset);

    if (do_lock) {
        unlock(fd);
    }
    
    return 0;
}

int get_file_stats_minifs(uint64_t *out_size) {
    if (!curr_opened) {
        errno = EBADF;
        return -1;
    }
    if (out_size) *out_size = curr_entry.size;
    return 0;
}

int close_minifs(void) {
    if (!curr_opened) {
        fprintf(stderr, "Error: no file is currently open\n");
        return -1;
    }

    memset(&curr_entry, 0, sizeof(curr_entry));
    curr_entry_offset = 0;
    curr_opened = 0;

    return 0;
}

int rm_minifs(void) {
    if (!curr_opened) {
        errno = EBADF;
        return -1;
    }

    int fd = lock_write(curr_entry_offset);

    uint64_t prev = 0;
    uint64_t cur = superblock.root_dir_offset;

    while (cur != 0) {
        minifs_file_entry e;
        fs_read(&e, sizeof(e), cur);
        if (cur == curr_entry_offset) {
            if (prev == 0) {
                superblock.root_dir_offset = e.next_file;
            } else {
                minifs_file_entry prev_e;
                fs_read(&prev_e, sizeof(prev_e), prev);
                prev_e.next_file = e.next_file;
                fs_write(&prev_e, sizeof(prev_e), prev);
            }

            uint64_t block_offset = cur;
            while (block_offset != 0) {
                uint64_t current_block = block_offset;
                
                if (current_block == cur) {
                    minifs_block_header header;
                    fs_read(&header, sizeof(header), current_block + sizeof(minifs_file_entry));
                    block_offset = header.next_block;
                } else {
                    minifs_block_header header;
                    fs_read(&header, sizeof(header), current_block);
                    block_offset = header.next_block;
                }
                
                free_block(current_block);
            }

            if (superblock.file_count > 0) superblock.file_count--;
            fs_write(&superblock, sizeof(superblock), 0);

            close_minifs();
            unlock(fd);
            return 0;
        }
        prev = cur;
        cur = e.next_file;
    }

    unlock(fd);

    return -1;
}

// ======================= FILE OWNERSHIP/PERMISSION FUNCTIONS ====

int get_file_ownership(uint32_t *out_uid, uint32_t *out_gid) {
    if (!curr_opened) {
        errno = EBADF;
        return -1;
    }
    
    if (out_uid) *out_uid = curr_entry.owner_uid;
    if (out_gid) *out_gid = curr_entry.owner_gid;
    
    return 0;
}

int set_file_ownership(uint32_t new_uid, uint32_t new_gid) {
    if (!curr_opened) {
        errno = EBADF;
        return -1;
    }
    
    curr_entry.owner_uid = new_uid;
    curr_entry.owner_gid = new_gid;
    fs_write(&curr_entry, sizeof(curr_entry), curr_entry_offset);
    
    return 0;
}

int set_file_permissions(uint16_t new_mode) {
    if (!curr_opened) {
        errno = EBADF;
        return -1;
    }
    
    // Mask to valid permission bits
    curr_entry.mode = new_mode & 0777;
    fs_write(&curr_entry, sizeof(curr_entry), curr_entry_offset);
    
    return 0;
}

int get_file_permissions(uint16_t *out_mode) {
    if (!curr_opened) {
        errno = EBADF;
        return -1;
    }
    
    if (out_mode) *out_mode = curr_entry.mode;
    return 0;
}

int check_curr_file_permission(uint32_t uid, uint32_t gid, int action) {
    if (!curr_opened) {
        errno = EBADF;
        return 0;
    }
    
    return check_file_permission(&curr_entry, uid, gid, action);
}

// ======================= CURRENT USER/GROUP MANAGEMENT ====

void set_current_user(uint32_t uid, uint32_t gid) {
    current_uid = uid;
    current_gid = gid;
}

void get_current_user(uint32_t *out_uid, uint32_t *out_gid) {
    if (out_uid) *out_uid = current_uid;
    if (out_gid) *out_gid = current_gid;
}

void update_curr_entry_mode(uint16_t new_mode) {
    if (curr_opened) {
        curr_entry.mode = new_mode;
    }
}

void update_curr_entry_owner(uint32_t new_uid, uint32_t new_gid) {
    if (curr_opened) {
        curr_entry.owner_uid = new_uid;
        curr_entry.owner_gid = new_gid;
    }
}
