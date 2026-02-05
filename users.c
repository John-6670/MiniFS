#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "users.h"

// ======================= GLOBAL STATE =====================
static minifs_user users_table[MAX_USERS];
static minifs_group groups_table[MAX_GROUPS];
static uint32_t next_uid = 1;
static uint32_t next_gid = 1;
static int initialized = 0;

// ======================= INITIALIZATION ================
void init_users_system(void) {
    if (initialized) return;
    
    memset(users_table, 0, sizeof(users_table));
    memset(groups_table, 0, sizeof(groups_table));
    
    // Create root user (uid = 0)
    minifs_user root;
    memset(&root, 0, sizeof(root));
    root.uid = 0;
    strncpy(root.username, "root", MAX_USERNAME - 1);
    root.gid = 0;  // root group
    root.created_at = time(NULL);
    root.is_active = 1;
    users_table[0] = root;
    
    // Create root group (gid = 0)
    minifs_group root_group;
    memset(&root_group, 0, sizeof(root_group));
    root_group.gid = 0;
    strncpy(root_group.groupname, "root", MAX_GROUPNAME - 1);
    root_group.member_uids[0] = 0;
    root_group.member_count = 1;
    root_group.created_at = time(NULL);
    groups_table[0] = root_group;
    
    next_uid = 1;
    next_gid = 1;
    initialized = 1;
}

// ======================= UTILITY FUNCTIONS ==============
static void sha256_hash(const char *input, char *output) {
    // Simple hash function: combine multiple hash methods
    // For production use proper SHA-256, but this avoids external dependencies
    unsigned long hash = 5381;
    int c;
    const unsigned char *p = (const unsigned char *)input;
    
    while ((c = *p++)) {
        hash = ((hash << 5) + hash) + c;
    }
    
    // Convert to hex string (64 chars for SHA-256 format compatibility)
    for (int i = 0; i < 8; i++) {
        sprintf(output + (i * 8), "%08lx", hash ^ (hash >> (i * 4)));
    }
    output[64] = '\0';
}

static int find_user_slot(void) {
    for (int i = 0; i < MAX_USERS; i++) {
        if (users_table[i].uid == 0 && i != 0) {  // uid=0 only for root
            return i;
        }
    }
    return -1;
}

static int find_group_slot(void) {
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups_table[i].gid == 0 && i != 0) {  // gid=0 only for root
            return i;
        }
    }
    return -1;
}

static int find_user_by_uid(uint32_t uid) {
    for (int i = 0; i < MAX_USERS; i++) {
        if (users_table[i].uid == uid) {
            return i;
        }
    }
    return -1;
}

static int find_group_by_gid(uint32_t gid) {
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups_table[i].gid == gid) {
            return i;
        }
    }
    return -1;
}

// ======================= USER MANAGEMENT ==============

int create_user(const char *username, const char *password, uint32_t gid) {
    if (!initialized) init_users_system();
    
    if (!username || strlen(username) == 0 || strlen(username) >= MAX_USERNAME) {
        errno = EINVAL;
        return -1;
    }
    
    if (password && strlen(password) >= 64) {
        errno = EINVAL;
        return -1;
    }
    
    // Check if username already exists
    for (int i = 0; i < MAX_USERS; i++) {
        if (users_table[i].uid != 0 && strncmp(users_table[i].username, username, MAX_USERNAME) == 0) {
            errno = EEXIST;
            return -1;
        }
    }
    
    // Verify group exists
    if (find_group_by_gid(gid) < 0) {
        errno = ENOENT;
        return -1;
    }
    
    // Find available slot
    int slot = find_user_slot();
    if (slot < 0) {
        errno = ENOSPC;
        return -1;
    }
    
    // Create user
    minifs_user new_user;
    memset(&new_user, 0, sizeof(new_user));
    new_user.uid = next_uid++;
    strncpy(new_user.username, username, MAX_USERNAME - 1);
    
    if (password) {
        sha256_hash(password, new_user.password_hash);
    }
    
    new_user.gid = gid;
    new_user.created_at = time(NULL);
    new_user.last_login = 0;
    new_user.is_active = 1;
    
    users_table[slot] = new_user;
    
    // Add user to group
    int group_slot = find_group_by_gid(gid);
    if (group_slot >= 0 && groups_table[group_slot].member_count < MAX_USERS) {
        groups_table[group_slot].member_uids[groups_table[group_slot].member_count++] = new_user.uid;
    }
    
    return (int)new_user.uid;
}

int delete_user(uint32_t uid) {
    if (!initialized) init_users_system();
    
    if (uid == 0) {  // Cannot delete root
        errno = EPERM;
        return -1;
    }
    
    int slot = find_user_by_uid(uid);
    if (slot < 0) {
        errno = ENOENT;
        return -1;
    }
    
    uint32_t primary_gid = users_table[slot].gid;
    
    // Remove user from all groups
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups_table[i].gid != 0) {
            for (uint32_t j = 0; j < groups_table[i].member_count; j++) {
                if (groups_table[i].member_uids[j] == uid) {
                    // Shift remaining members
                    for (uint32_t k = j; k < groups_table[i].member_count - 1; k++) {
                        groups_table[i].member_uids[k] = groups_table[i].member_uids[k + 1];
                    }
                    groups_table[i].member_count--;
                    break;
                }
            }
        }
    }
    
    // Clear user entry
    memset(&users_table[slot], 0, sizeof(minifs_user));
    
    return 0;
}

int user_exists(const char *username, uint32_t *out_uid) {
    if (!initialized) init_users_system();
    
    if (!username) {
        errno = EINVAL;
        return 0;
    }
    
    for (int i = 0; i < MAX_USERS; i++) {
        if (users_table[i].uid != 0 && 
            strncmp(users_table[i].username, username, MAX_USERNAME) == 0) {
            if (out_uid) *out_uid = users_table[i].uid;
            return 1;
        }
    }
    
    return 0;
}

int authenticate_user(const char *username, const char *password, uint32_t *out_uid) {
    if (!initialized) init_users_system();
    
    if (!username || !password) {
        errno = EINVAL;
        return -1;
    }
    
    for (int i = 0; i < MAX_USERS; i++) {
        if (users_table[i].uid != 0 && 
            strncmp(users_table[i].username, username, MAX_USERNAME) == 0) {
            
            if (!users_table[i].is_active) {
                errno = EACCES;
                return -1;
            }
            
            char password_hash[65];
            sha256_hash(password, password_hash);
            
            if (strncmp(users_table[i].password_hash, password_hash, 64) == 0) {
                users_table[i].last_login = time(NULL);
                if (out_uid) *out_uid = users_table[i].uid;
                return 0;
            }
            
            errno = EACCES;
            return -1;
        }
    }
    
    errno = ENOENT;
    return -1;
}

int add_user_to_group(uint32_t uid, uint32_t gid) {
    if (!initialized) init_users_system();
    
    int user_slot = find_user_by_uid(uid);
    if (user_slot < 0) {
        errno = ENOENT;
        return -1;
    }
    
    int group_slot = find_group_by_gid(gid);
    if (group_slot < 0) {
        errno = ENOENT;
        return -1;
    }
    
    // Check if already in group
    for (uint32_t i = 0; i < users_table[user_slot].secondary_gid_count; i++) {
        if (users_table[user_slot].secondary_gids[i] == gid) {
            errno = EEXIST;
            return -1;
        }
    }
    
    // Check if already in primary group
    if (users_table[user_slot].gid == gid) {
        errno = EEXIST;
        return -1;
    }
    
    // Add to secondary groups
    if (users_table[user_slot].secondary_gid_count >= MAX_GROUPS_PER_USER) {
        errno = ENOSPC;
        return -1;
    }
    
    users_table[user_slot].secondary_gids[users_table[user_slot].secondary_gid_count++] = gid;
    
    // Add to group member list
    if (groups_table[group_slot].member_count < MAX_USERS) {
        groups_table[group_slot].member_uids[groups_table[group_slot].member_count++] = uid;
    } else {
        errno = ENOSPC;
        return -1;
    }
    
    return 0;
}

int remove_user_from_group(uint32_t uid, uint32_t gid) {
    if (!initialized) init_users_system();
    
    int user_slot = find_user_by_uid(uid);
    if (user_slot < 0) {
        errno = ENOENT;
        return -1;
    }
    
    int group_slot = find_group_by_gid(gid);
    if (group_slot < 0) {
        errno = ENOENT;
        return -1;
    }
    
    // Cannot remove from primary group
    if (users_table[user_slot].gid == gid) {
        errno = EPERM;
        return -1;
    }
    
    // Find and remove from secondary groups
    int found = 0;
    for (uint32_t i = 0; i < users_table[user_slot].secondary_gid_count; i++) {
        if (users_table[user_slot].secondary_gids[i] == gid) {
            // Shift remaining groups
            for (uint32_t j = i; j < users_table[user_slot].secondary_gid_count - 1; j++) {
                users_table[user_slot].secondary_gids[j] = users_table[user_slot].secondary_gids[j + 1];
            }
            users_table[user_slot].secondary_gid_count--;
            found = 1;
            break;
        }
    }
    
    if (!found) {
        errno = ENOENT;
        return -1;
    }
    
    // Remove from group member list
    for (uint32_t i = 0; i < groups_table[group_slot].member_count; i++) {
        if (groups_table[group_slot].member_uids[i] == uid) {
            // Shift remaining members
            for (uint32_t j = i; j < groups_table[group_slot].member_count - 1; j++) {
                groups_table[group_slot].member_uids[j] = groups_table[group_slot].member_uids[j + 1];
            }
            groups_table[group_slot].member_count--;
            break;
        }
    }
    
    return 0;
}

// ======================= GROUP MANAGEMENT ==============

int create_group(const char *groupname) {
    if (!initialized) init_users_system();
    
    if (!groupname || strlen(groupname) == 0 || strlen(groupname) >= MAX_GROUPNAME) {
        errno = EINVAL;
        return -1;
    }
    
    // Check if groupname already exists
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups_table[i].gid != 0 && 
            strncmp(groups_table[i].groupname, groupname, MAX_GROUPNAME) == 0) {
            errno = EEXIST;
            return -1;
        }
    }
    
    // Find available slot
    int slot = find_group_slot();
    if (slot < 0) {
        errno = ENOSPC;
        return -1;
    }
    
    // Create group
    minifs_group new_group;
    memset(&new_group, 0, sizeof(new_group));
    new_group.gid = next_gid++;
    strncpy(new_group.groupname, groupname, MAX_GROUPNAME - 1);
    new_group.created_at = time(NULL);
    
    groups_table[slot] = new_group;
    
    return (int)new_group.gid;
}

int delete_group(uint32_t gid) {
    if (!initialized) init_users_system();
    
    if (gid == 0) {  // Cannot delete root group
        errno = EPERM;
        return -1;
    }
    
    int slot = find_group_by_gid(gid);
    if (slot < 0) {
        errno = ENOENT;
        return -1;
    }
    
    // Remove all users from this group
    for (uint32_t i = 0; i < groups_table[slot].member_count; i++) {
        uint32_t uid = groups_table[slot].member_uids[i];
        int user_slot = find_user_by_uid(uid);
        
        if (user_slot >= 0) {
            // Remove from user's secondary groups
            for (uint32_t j = 0; j < users_table[user_slot].secondary_gid_count; j++) {
                if (users_table[user_slot].secondary_gids[j] == gid) {
                    for (uint32_t k = j; k < users_table[user_slot].secondary_gid_count - 1; k++) {
                        users_table[user_slot].secondary_gids[k] = users_table[user_slot].secondary_gids[k + 1];
                    }
                    users_table[user_slot].secondary_gid_count--;
                    break;
                }
            }
        }
    }
    
    // Clear group entry
    memset(&groups_table[slot], 0, sizeof(minifs_group));
    
    return 0;
}

int group_exists(const char *groupname, uint32_t *out_gid) {
    if (!initialized) init_users_system();
    
    if (!groupname) {
        errno = EINVAL;
        return 0;
    }
    
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups_table[i].gid != 0 && 
            strncmp(groups_table[i].groupname, groupname, MAX_GROUPNAME) == 0) {
            if (out_gid) *out_gid = groups_table[i].gid;
            return 1;
        }
    }
    
    return 0;
}

int is_user_in_group(uint32_t uid, uint32_t gid) {
    if (!initialized) init_users_system();
    
    int user_slot = find_user_by_uid(uid);
    if (user_slot < 0) {
        return 0;
    }
    
    // Check primary group
    if (users_table[user_slot].gid == gid) {
        return 1;
    }
    
    // Check secondary groups
    for (uint32_t i = 0; i < users_table[user_slot].secondary_gid_count; i++) {
        if (users_table[user_slot].secondary_gids[i] == gid) {
            return 1;
        }
    }
    
    return 0;
}

// ======================= PERMISSION FUNCTIONS ========

int check_permission(uint16_t mode, uint32_t uid, uint32_t gid, 
                     uint32_t file_uid, uint32_t file_gid, int action) {
    if (!initialized) init_users_system();
    
    // Root (uid 0) can do anything
    if (uid == 0) {
        return 1;
    }
    
    uint16_t owner_mask = (action == 0) ? PERM_OWNER_READ : 
                          (action == 1) ? PERM_OWNER_WRITE : PERM_OWNER_EXEC;
    uint16_t group_mask = (action == 0) ? PERM_GROUP_READ : 
                          (action == 1) ? PERM_GROUP_WRITE : PERM_GROUP_EXEC;
    uint16_t other_mask = (action == 0) ? PERM_OTHER_READ : 
                          (action == 1) ? PERM_OTHER_WRITE : PERM_OTHER_EXEC;
    
    // Check owner permission
    if (uid == file_uid) {
        return (mode & owner_mask) != 0;
    }
    
    // Check group permission
    if (is_user_in_group(uid, file_gid)) {
        return (mode & group_mask) != 0;
    }
    
    // Check other permission
    return (mode & other_mask) != 0;
}

int change_owner(uint32_t uid, uint32_t new_owner_uid, uint32_t new_owner_gid) {
    if (!initialized) init_users_system();
    
    // Only root can change owner
    if (uid != 0) {
        errno = EPERM;
        return -1;
    }
    
    // Verify new owner exists
    if (find_user_by_uid(new_owner_uid) < 0) {
        errno = ENOENT;
        return -1;
    }
    
    // Verify new group exists
    if (find_group_by_gid(new_owner_gid) < 0) {
        errno = ENOENT;
        return -1;
    }
    
    return 0;  // Success, caller should update file ownership
}

int change_mode(uint16_t *mode, uint16_t new_mode) {
    if (!mode) {
        errno = EINVAL;
        return -1;
    }
    
    // Mask to only valid permission bits
    *mode = new_mode & (PERM_OWNER_READ | PERM_OWNER_WRITE | PERM_OWNER_EXEC |
                        PERM_GROUP_READ | PERM_GROUP_WRITE | PERM_GROUP_EXEC |
                        PERM_OTHER_READ | PERM_OTHER_WRITE | PERM_OTHER_EXEC |
                        SETUID_BIT | SETGID_BIT | STICKY_BIT);
    
    return 0;
}

// ======================= UTILITY FUNCTIONS FOR QUERIES ===

minifs_user* get_user_by_uid(uint32_t uid) {
    if (!initialized) init_users_system();
    
    int slot = find_user_by_uid(uid);
    return (slot >= 0) ? &users_table[slot] : NULL;
}

minifs_group* get_group_by_gid(uint32_t gid) {
    if (!initialized) init_users_system();
    
    int slot = find_group_by_gid(gid);
    return (slot >= 0) ? &groups_table[slot] : NULL;
}

void list_users(void) {
    if (!initialized) init_users_system();
    
    printf("\n=== Users ===\n");
    for (int i = 0; i < MAX_USERS; i++) {
        if (users_table[i].uid != 0 || i == 0) {  // root or any active user
            printf("UID: %u, Username: %s, GID: %u, Active: %d\n",
                   users_table[i].uid, users_table[i].username, 
                   users_table[i].gid, users_table[i].is_active);
        }
    }
}

void list_groups(void) {
    if (!initialized) init_users_system();
    
    printf("\n=== Groups ===\n");
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (groups_table[i].gid != 0 || i == 0) {  // root or any active group
            printf("GID: %u, Groupname: %s, Members: %u\n",
                   groups_table[i].gid, groups_table[i].groupname, 
                   groups_table[i].member_count);
        }
    }
}
