#ifndef USERS_H
#define USERS_H

#include <stdint.h>
#include <time.h>

#define MAX_USERNAME 32
#define MAX_GROUPNAME 32
#define MAX_USERS 100
#define MAX_GROUPS 50
#define MAX_GROUPS_PER_USER 10

// ======================= USER STRUCT ======================
typedef struct {
    uint32_t uid;                                   // User ID (0 = root)
    char username[MAX_USERNAME];
    char password_hash[64];                         // SHA-256 hash of password
    uint32_t gid;                                   // Primary group ID
    uint32_t secondary_gids[MAX_GROUPS_PER_USER];   // Secondary group IDs
    uint32_t secondary_gid_count;                   // Number of secondary groups
    time_t created_at;
    time_t last_login;
    int is_active;                                  // 1 = active, 0 = disabled
} minifs_user;

// ======================= GROUP STRUCT ====================
typedef struct {
    uint32_t gid;
    char groupname[MAX_GROUPNAME];
    uint32_t member_uids[MAX_USERS];        // UIDs of members
    uint32_t member_count;                  // Number of members
    time_t created_at;
} minifs_group;

// ======================= PERMISSION STRUCT ==============
typedef struct {
    uint32_t owner_uid;                     // Owner user ID
    uint32_t owner_gid;                     // Owner group ID
    uint16_t mode;                          // Unix-style permissions (9 bits)
                                            // Owner: rwx (bits 6-8)
                                            // Group: rwx (bits 3-5)
                                            // Other: rwx (bits 0-2)
} minifs_permission;

// ======================= PERMISSION BITS ================
#define PERM_OWNER_READ     0400             // Owner read
#define PERM_OWNER_WRITE    0200             // Owner write
#define PERM_OWNER_EXEC     0100             // Owner execute
#define PERM_GROUP_READ     0040             // Group read
#define PERM_GROUP_WRITE    0020             // Group write
#define PERM_GROUP_EXEC     0010             // Group execute
#define PERM_OTHER_READ     0004             // Other read
#define PERM_OTHER_WRITE    0002             // Other write
#define PERM_OTHER_EXEC     0001             // Other execute

// ======================= SPECIAL BITS ====================
#define SETUID_BIT          04000            // Set user ID on execution
#define SETGID_BIT          02000            // Set group ID on execution
#define STICKY_BIT          01000            // Sticky bit

// ======================= PERMISSION CHECKS =============
#define CAN_READ(mode, uid, gid, file_uid, file_gid) \
    ((uid == file_uid && (mode & PERM_OWNER_READ)) || \
     (gid == file_gid && (mode & PERM_GROUP_READ)) || \
     (mode & PERM_OTHER_READ))

#define CAN_WRITE(mode, uid, gid, file_uid, file_gid) \
    ((uid == file_uid && (mode & PERM_OWNER_WRITE)) || \
     (gid == file_gid && (mode & PERM_GROUP_WRITE)) || \
     (mode & PERM_OTHER_WRITE))

#define CAN_EXEC(mode, uid, gid, file_uid, file_gid) \
    ((uid == file_uid && (mode & PERM_OWNER_EXEC)) || \
     (gid == file_gid && (mode & PERM_GROUP_EXEC)) || \
     (mode & PERM_OTHER_EXEC))

// ======================= USER MANAGEMENT FUNCTIONS ====
int create_user(const char *username, const char *password, uint32_t gid);
int delete_user(uint32_t uid);
int user_exists(const char *username, uint32_t *out_uid);
int authenticate_user(const char *username, const char *password, uint32_t *out_uid);
int add_user_to_group(uint32_t uid, uint32_t gid);
int remove_user_from_group(uint32_t uid, uint32_t gid);

// ======================= GROUP MANAGEMENT FUNCTIONS ===
int create_group(const char *groupname);
int delete_group(uint32_t gid);
int group_exists(const char *groupname, uint32_t *out_gid);
int is_user_in_group(uint32_t uid, uint32_t gid);

// ======================= PERMISSION FUNCTIONS ========
int check_permission(uint16_t mode, uint32_t uid, uint32_t gid, 
                     uint32_t file_uid, uint32_t file_gid, int action);
                     // action: 0 = read, 1 = write, 2 = execute
int change_owner(uint32_t uid, uint32_t new_owner_uid, uint32_t new_owner_gid);
int change_mode(uint16_t *mode, uint16_t new_mode);

// ======================= UTILITY FUNCTIONS ========
minifs_user* get_user_by_uid(uint32_t uid);
minifs_group* get_group_by_gid(uint32_t gid);
void list_users(void);
void list_groups(void);
void init_users_system(void);

#endif
