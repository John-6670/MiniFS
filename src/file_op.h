#ifndef FILE_OPERATIONS_H
#define FILE_OPERATIONS_H

#include <stdint.h>
#include <sys/types.h>
#include "filesys.h"

int find_file_by_name(const char *name, minifs_file_entry *out_entry, uint64_t *out_offset);
uint64_t alloc(int blocks_needed, int first_block);
void free_block(uint64_t offset);
void add_to_freelist(uint64_t offset);
uint64_t get_from_freelist(void);
uint64_t calculate_blocks_needed(uint64_t file_size);

int open_minifs(const char *name, int flags);
ssize_t read_minifs(uint64_t pos, void *buf, size_t n);
ssize_t write_minifs(uint64_t pos, const void *buf, size_t n);
int shrink_minifs(uint64_t new_size, int do_lock);
int get_file_stats_minifs(uint64_t *out_size);
int close_minifs(void);
int rm_minifs(void);

// ======================= FILE OWNERSHIP/PERMISSION FUNCTIONS ====
int get_file_ownership(uint32_t *out_uid, uint32_t *out_gid);
int set_file_ownership(uint32_t new_uid, uint32_t new_gid);
int set_file_permissions(uint16_t new_mode);
int get_file_permissions(uint16_t *out_mode);
int check_curr_file_permission(uint32_t uid, uint32_t gid, int action);

// ======================= CURRENT USER/GROUP FUNCTIONS ====
void set_current_user(uint32_t uid, uint32_t gid);
void get_current_user(uint32_t *out_uid, uint32_t *out_gid);
void update_curr_entry_mode(uint16_t new_mode);
void update_curr_entry_owner(uint32_t new_uid, uint32_t new_gid);

#endif
