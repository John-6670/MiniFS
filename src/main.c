#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "filesys.h"
#include "file_op.h"
#include "users.h"


void print_help(void) {
    printf("\n=== MiniFS CLI Commands ===\n");
    printf("  open <filename> [flags]        - Open a file (flags: c=create, w=write)\n");
    printf("  read <pos> <size>              - Read from current file\n");
    printf("  write <pos> <text>             - Write text to current file\n");
    printf("  write_many <number>            - perfrom many writes\n");
    printf("  shrink <newsize>               - Shrink current file to new size\n");
    printf("  stat                           - Show current file stats (with permissions)\n");
    printf("  fsstat                         - Show filesystem stats\n");
    printf("  close                          - Close current file\n");
    printf("  rm                             - Remove current file\n");
    printf("  list                           - List all files\n");
    printf("  viz                            - Visualize filesystem blocks\n");
    printf("  useradd <username>             - Create a new user\n");
    printf("  userdel <username>             - Delete a user\n");
    printf("  groupadd <groupname>           - Create a new group\n");
    printf("  groupdel <groupname>           - Delete a group\n");
    printf("  usermod -aG <group> <user>     - Add user to group\n");
    printf("  lusers                         - List all users\n");
    printf("  lgroups                        - List all groups\n");
    printf("  chmod <mode> <path>            - Change file permissions (e.g., chmod 755 file.txt)\n");
    printf("  chown <user>[:<group>] <path>  - Change file owner and group\n");
    printf("  chgrp <group> <path>           - Change file group owner\n");
    printf("  getfacl <path>                 - Show file ownership and permissions\n");
    printf("  su <user> <group>              - Switch to another user and group\n");
    printf("  whoami                         - Show current user and group\n");
    printf("  echo <text>                    - Print text to console\n");
    printf("  help                           - Show this help\n");
    printf("  exit                           - Exit CLI\n");
    printf("=========================\n\n");
}

void cmd_open(char *args) {
    char filename[MAX_NAME];
    char flags_str[10] = "";
    int flags = 0;

    int n = sscanf(args, "%63s %9s", filename, flags_str);
    if (n < 1) {
        printf("Usage: open <filename> [flags]\n");
        return;
    }

    if (strlen(flags_str) > 0) {
        if (strchr(flags_str, 'c')) flags |= FLAG_CREAT;
        if (strchr(flags_str, 'w')) flags |= FLAG_WRITE;
    }

    if (open_minifs(filename, flags) == 0) {
        printf("Opened: %s\n", filename);
    } else {
        perror("open failed");
    }
}

void cmd_read(char *args) {
    uint64_t pos;
    size_t size;

    if (sscanf(args, "%lu %zu", &pos, &size) != 2) {
        printf("Usage: read <pos> <size>\n");
        return;
    }

    char *buf = malloc(size + 1);
    if (!buf) {
        perror("malloc");
        return;
    }

    ssize_t r = read_minifs(pos, buf, size);
    if (r < 0) {
        perror("read failed");
    } else {
        buf[r] = '\0';
        printf("Read %zd bytes: '%s'\n", r, buf);
    }
    free(buf);
}

void cmd_write(char *args) {
    uint64_t pos;
    char text[BLOCK_SIZE];

    if (sscanf(args, "%lu %[^\n]", &pos, text) != 2) {
        printf("Usage: write <pos> <text>\n");
        return;
    }

    ssize_t w = write_minifs(pos, text, strlen(text));
    if (w < 0) {
        perror("write failed");
    } else {
        printf("Wrote %zd bytes\n", w);
    }
}

void cmd_shrink(char *args) {
    uint64_t newsize;

    if (sscanf(args, "%lu", &newsize) != 1) {
        printf("Usage: shrink <newsize>\n");
        return;
    }

    if (shrink_minifs(newsize, 1) == 0) {
        printf("Shrunk to %lu bytes\n", newsize);
    } else {
        perror("shrink failed");
    }
}

void cmd_fsstat(void) {
    uint64_t used, free;
    int count;
    
    if (get_fs_stat_minifs(&used, &free, &count) == 0) {
        printf("Filesystem stats:\n");
        printf("  Used: %lu bytes\n", used);
        printf("  Free: %lu bytes\n", free);
        printf("  Files: %d\n", count);
    } else {
        perror("fsstat failed");
    }
}

void cmd_close(void) {
    if (close_minifs() == 0) {
        printf("File closed\n");
    } else {
        perror("close failed");
    }
}

void cmd_rm(void) {
    if (rm_minifs() == 0) {
        printf("File removed\n");
    } else {
        perror("rm failed");
    }
}

void cmd_list(void) {
    printf("\n=== Files in filesystem ===\n");
    uint64_t off = superblock.root_dir_offset;
    int idx = 0;
    
    if (off == 0) {
        printf("(no files)\n");
        return;
    }

    while (off != 0) {
        minifs_file_entry e;
        fs_read(&e, sizeof(e), off);
        uint64_t blocks = calculate_blocks_needed(e.size);
        printf("%d. %s (size: %lu bytes, blocks: %lu, owner: %u:%u, mode: %03o)\n", 
               ++idx, e.name, e.size, blocks, e.owner_uid, e.owner_gid, e.mode);
        off = e.next_file;
    }
    printf("===========================\n\n");
}

void cmd_viz(void) {
    printf("\n=== Filesystem Visualization ===\n");
    printf("Total size: %d bytes (%d blocks of %d bytes)\n", 
           FS_SIZE, FS_SIZE / BLOCK_SIZE, BLOCK_SIZE);
    printf("Block 0: Superblock\n");
    
    // Track which blocks are used
    int total_blocks = FS_SIZE / BLOCK_SIZE;
    char *block_status = calloc(total_blocks, 1); // 0 = free, 1 = used
    if (!block_status) {
        perror("malloc");
        return;
    }
    
    // Mark superblock as used
    block_status[0] = 1;
    
    // Mark all file blocks as used
    uint64_t file_off = superblock.root_dir_offset;
    while (file_off != 0) {
        minifs_file_entry e;
        fs_read(&e, sizeof(e), file_off);
        
        // Mark this file's blocks
        uint64_t block_off = file_off;
        int block_num = block_off / BLOCK_SIZE;
        block_status[block_num] = 1;
        
        minifs_block_header header;
        fs_read(&header, sizeof(header), block_off + sizeof(minifs_file_entry));
        
        while (header.next_block != 0) {
            block_off = header.next_block;
            block_num = block_off / BLOCK_SIZE;
            block_status[block_num] = 1;
            fs_read(&header, sizeof(header), block_off);
        }
        
        file_off = e.next_file;
    }
    
    // Print free list info
    printf("\n--- Free List ---\n");
    uint64_t free_off = superblock.freelist_head;
    int free_list_num = 1;
    uint64_t total_free_blocks = 0;
    
    while (free_off != 0) {
        minifs_freelist free_node;
        fs_read(&free_node, sizeof(free_node), free_off);
        
        int start_block = free_node.start_offset / BLOCK_SIZE;
        int end_block = free_node.end_offset / BLOCK_SIZE;
        
        printf("Free region %d: blocks %d-%d (%u blocks, %lu bytes)\n",
               free_list_num++, start_block, end_block - 1, 
               free_node.block_count, (uint64_t)free_node.block_count * BLOCK_SIZE);
        
        total_free_blocks += free_node.block_count;
        free_off = free_node.next_free;
    }
    
    printf("\n--- Block Map ---\n");
    printf("Legend: [#] = Used, [.] = Free, [S] = Superblock\n\n");
    
    for (int i = 0; i < total_blocks; i++) {
        if (i == 0) {
            printf("[S]");
        } else if (block_status[i]) {
            printf("[#]");
        } else {
            printf("[.]");
        }
        
        if ((i + 1) % 32 == 0) {
            printf(" %d\n", i + 1);
        } else if ((i + 1) % 8 == 0) {
            printf(" ");
        }
    }
    
    if (total_blocks % 32 != 0) {
        printf("\n");
    }
    
    printf("\n--- Summary ---\n");
    int used_blocks = 0;
    for (int i = 0; i < total_blocks; i++) {
        if (block_status[i]) used_blocks++;
    }
    
    int free_blocks = total_blocks - used_blocks;
    printf("Total blocks: %d\n", total_blocks);
    printf("Used blocks:  %d (%.1f%%)\n", used_blocks, 100.0 * used_blocks / total_blocks);
    printf("Free blocks:  %d (%.1f%%)\n", free_blocks, 100.0 * free_blocks / total_blocks);
    printf("Used space:   %lu bytes\n", (uint64_t)used_blocks * BLOCK_SIZE);
    printf("Free space:   %lu bytes\n", (uint64_t)free_blocks * BLOCK_SIZE);
    
    free(block_status);
    printf("================================\n\n");
}

void cmd_useradd(char *args) {
    char username[MAX_USERNAME];
    
    if (sscanf(args, "%31s", username) != 1) {
        printf("Usage: useradd <username>\n");
        return;
    }
    
    // Create user with default group (root group for now)
    int uid = create_user(username, NULL, 0);
    if (uid < 0) {
        perror("useradd failed");
    } else {
        printf("User '%s' created with UID %d\n", username, uid);
    }
}

void cmd_userdel(char *args) {
    char username[MAX_USERNAME];
    uint32_t uid;
    
    if (sscanf(args, "%31s", username) != 1) {
        printf("Usage: userdel <username>\n");
        return;
    }
    
    if (!user_exists(username, &uid)) {
        printf("User '%s' does not exist\n", username);
        return;
    }
    
    if (delete_user(uid) == 0) {
        printf("User '%s' (UID %u) deleted\n", username, uid);
    } else {
        perror("userdel failed");
    }
}

void cmd_groupadd(char *args) {
    char groupname[MAX_GROUPNAME];
    
    if (sscanf(args, "%31s", groupname) != 1) {
        printf("Usage: groupadd <groupname>\n");
        return;
    }
    
    int gid = create_group(groupname);
    if (gid < 0) {
        perror("groupadd failed");
    } else {
        printf("Group '%s' created with GID %d\n", groupname, gid);
    }
}

void cmd_groupdel(char *args) {
    char groupname[MAX_GROUPNAME];
    uint32_t gid;
    
    if (sscanf(args, "%31s", groupname) != 1) {
        printf("Usage: groupdel <groupname>\n");
        return;
    }
    
    if (!group_exists(groupname, &gid)) {
        printf("Group '%s' does not exist\n", groupname);
        return;
    }
    
    if (delete_group(gid) == 0) {
        printf("Group '%s' (GID %u) deleted\n", groupname, gid);
    } else {
        perror("groupdel failed");
    }
}

void cmd_usermod(char *args) {
    char option[10];
    char groupname[MAX_GROUPNAME];
    char username[MAX_USERNAME];
    uint32_t uid, gid;
    
    if (sscanf(args, "%9s %31s %31s", option, groupname, username) != 3) {
        printf("Usage: usermod -aG <group> <user>\n");
        return;
    }
    
    // Check if option is -aG
    if (strcmp(option, "-aG") != 0) {
        printf("Usage: usermod -aG <group> <user>\n");
        return;
    }
    
    // Find user and group
    if (!user_exists(username, &uid)) {
        printf("User '%s' does not exist\n", username);
        return;
    }
    
    if (!group_exists(groupname, &gid)) {
        printf("Group '%s' does not exist\n", groupname);
        return;
    }
    
    if (add_user_to_group(uid, gid) == 0) {
        printf("User '%s' added to group '%s'\n", username, groupname);
    } else {
        perror("usermod failed");
    }
}

void cmd_lusers(void) {
    list_users();
}

void cmd_lgroups(void) {
    list_groups();
}

void cmd_chmod(char *args) {
    uint16_t mode;
    char filename[MAX_NAME];
    minifs_file_entry entry;
    uint64_t offset;
    
    if (sscanf(args, "%ho %63s", &mode, filename) != 2) {
        printf("Usage: chmod <octal_mode> <path> (e.g., chmod 755 file.txt)\n");
        return;
    }
    
    if (!find_file_by_name(filename, &entry, &offset)) {
        printf("File '%s' not found\n", filename);
        return;
    }
    
    entry.mode = mode & 0777;
    fs_write(&entry, sizeof(entry), offset);
    
    // Update in-memory curr_entry if it's the currently opened file
    // (The function checks internally if a file is open and updates accordingly)
    update_curr_entry_mode(entry.mode);
    
    printf("Permissions of '%s' changed to %03o\n", filename, entry.mode);
}

void cmd_chown(char *args) {
    char user_group[MAX_USERNAME + MAX_GROUPNAME + 2];  // "user:group"
    char filename[MAX_NAME];
    char username[MAX_USERNAME];
    char groupname[MAX_GROUPNAME];
    uint32_t uid, gid;
    minifs_file_entry entry;
    uint64_t offset;
    
    if (sscanf(args, "%63s %63s", user_group, filename) != 2) {
        printf("Usage: chown <user>[:<group>] <path>\n");
        return;
    }
    
    // Parse user:group
    char *colon = strchr(user_group, ':');
    if (colon) {
        int user_len = colon - user_group;
        strncpy(username, user_group, user_len);
        username[user_len] = '\0';
        strncpy(groupname, colon + 1, MAX_GROUPNAME - 1);
    } else {
        strncpy(username, user_group, MAX_USERNAME - 1);
        strcpy(groupname, "");  // Empty means keep current group
    }
    
    if (!find_file_by_name(filename, &entry, &offset)) {
        printf("File '%s' not found\n", filename);
        return;
    }
    
    if (!user_exists(username, &uid)) {
        printf("User '%s' does not exist\n", username);
        return;
    }
    
    if (strlen(groupname) > 0) {
        if (!group_exists(groupname, &gid)) {
            printf("Group '%s' does not exist\n", groupname);
            return;
        }
    } else {
        gid = entry.owner_gid;  // Keep current group if not specified
    }
    
    entry.owner_uid = uid;
    entry.owner_gid = gid;
    fs_write(&entry, sizeof(entry), offset);
    
    // Update in-memory curr_entry if it's the currently opened file
    update_curr_entry_owner(uid, gid);
    
    printf("Ownership of '%s' changed to %s:%s (UID:%u, GID:%u)\n", 
           filename, username, strlen(groupname) > 0 ? groupname : "unchanged", uid, gid);
}

void cmd_chgrp(char *args) {
    char groupname[MAX_GROUPNAME];
    char filename[MAX_NAME];
    uint32_t gid;
    minifs_file_entry entry;
    uint64_t offset;
    
    if (sscanf(args, "%31s %63s", groupname, filename) != 2) {
        printf("Usage: chgrp <group> <path>\n");
        return;
    }
    
    if (!group_exists(groupname, &gid)) {
        printf("Group '%s' does not exist\n", groupname);
        return;
    }
    
    if (!find_file_by_name(filename, &entry, &offset)) {
        printf("File '%s' not found\n", filename);
        return;
    }
    
    entry.owner_gid = gid;
    fs_write(&entry, sizeof(entry), offset);
    
    // Update in-memory curr_entry if it's the currently opened file
    update_curr_entry_owner(entry.owner_uid, gid);
    
    printf("Group of '%s' changed to %s (GID:%u)\n", filename, groupname, gid);
}

void cmd_getfacl(char *args) {
    char filename[MAX_NAME];
    minifs_file_entry entry;
    uint64_t offset;
    minifs_user *owner_user;
    minifs_group *owner_group;
    
    if (sscanf(args, "%63s", filename) != 1) {
        printf("Usage: getfacl <path>\n");
        return;
    }
    
    if (!find_file_by_name(filename, &entry, &offset)) {
        printf("File '%s' not found\n", filename);
        return;
    }
    
    owner_user = get_user_by_uid(entry.owner_uid);
    owner_group = get_group_by_gid(entry.owner_gid);
    
    uint8_t owner_perms = (entry.mode >> 6) & 0x7;
    uint8_t group_perms = (entry.mode >> 3) & 0x7;
    uint8_t other_perms = entry.mode & 0x7;
    
    printf("\n# File: %s\n", filename);
    printf("# Owner: %s\n", owner_user ? owner_user->username : "unknown");
    printf("# Group: %s\n", owner_group ? owner_group->groupname : "unknown");
    printf("\nuser::%c%c%c\n", 
           (owner_perms & 4) ? 'r' : '-',
           (owner_perms & 2) ? 'w' : '-',
           (owner_perms & 1) ? 'x' : '-');
    printf("group::%c%c%c\n",
           (group_perms & 4) ? 'r' : '-',
           (group_perms & 2) ? 'w' : '-',
           (group_perms & 1) ? 'x' : '-');
    printf("other::%c%c%c\n",
           (other_perms & 4) ? 'r' : '-',
           (other_perms & 2) ? 'w' : '-',
           (other_perms & 1) ? 'x' : '-');
}

void cmd_su(char *args) {
    char username[MAX_USERNAME];
    char groupname[MAX_GROUPNAME];
    uint32_t uid, gid;
    minifs_user *user;
    minifs_group *group;
    
    if (sscanf(args, "%31s %31s", username, groupname) != 2) {
        printf("Usage: su <user> <group>\n");
        return;
    }
    
    if (!user_exists(username, &uid)) {
        printf("User '%s' does not exist\n", username);
        return;
    }
    
    if (!group_exists(groupname, &gid)) {
        printf("Group '%s' does not exist\n", groupname);
        return;
    }
    
    set_current_user(uid, gid);
    user = get_user_by_uid(uid);
    group = get_group_by_gid(gid);
    printf("Switched to user: %s (UID:%u), group: %s (GID:%u)\n", 
           user->username, uid, group->groupname, gid);
}

void cmd_whoami(void) {
    uint32_t uid, gid;
    get_current_user(&uid, &gid);
    minifs_user *user = get_user_by_uid(uid);
    minifs_group *group = get_group_by_gid(gid);
    printf("Current user: %s (UID=%u), group: %s (GID=%u)\n", 
           user ? user->username : "unknown", uid,
           group ? group->groupname : "unknown", gid);
}

void cmd_echo(char *args) {
    if (strlen(args) == 0) {
        printf("\n");
    } else {
        printf("%s\n", args);
    }
}

void cmd_stat(void) {
    uint64_t size;
    uint32_t uid, gid;
    uint16_t mode;
    
    if (get_file_stats_minifs(&size) == 0) {
        get_file_ownership(&uid, &gid);
        get_file_permissions(&mode);
        printf("File stats:\n");
        printf("  Size: %lu bytes\n", size);
        printf("  Owner: UID=%u, GID=%u\n", uid, gid);
        printf("  Permissions: %03o\n", mode);
    } else {
        perror("stat failed");
    }
}

void cmd_many_writes(char* args) {
    int number;
    sscanf(args, "%u", &number);
    for (int i = 1; i < number; i++) {
        cmd_write("0 hello");
    }
}

int main(void) {
    printf("=== MiniFS Command Line Interface ===\n");
    printf("Initializing filesystem...\n");
    init_filesystem();
    printf("Ready!\n");
    print_help();

    char line[1024];
    char cmd[64];

    while (1) {
        printf("MiniFS> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }

        // Remove trailing newline
        line[strcspn(line, "\n")] = 0;

        // Skip empty lines
        if (strlen(line) == 0) continue;

        // Parse command
        if (sscanf(line, "%63s", cmd) != 1) continue;

        // Find arguments (everything after command)
        char *args = line + strlen(cmd);
        while (*args == ' ') args++;

        // Execute commands
        if (strcmp(cmd, "open") == 0) {
            cmd_open(args);
        } else if (strcmp(cmd, "read") == 0) {
            cmd_read(args);
        } else if (strcmp(cmd, "write") == 0) {
            cmd_write(args);
        } else if (strcmp(cmd, "write_many") == 0) {
            cmd_many_writes(args);
        } else if (strcmp(cmd, "shrink") == 0) {
            cmd_shrink(args);
        } else if (strcmp(cmd, "stat") == 0) {
            cmd_stat();
        } else if (strcmp(cmd, "fsstat") == 0) {
            cmd_fsstat();
        } else if (strcmp(cmd, "close") == 0) {
            cmd_close();
        } else if (strcmp(cmd, "rm") == 0) {
            cmd_rm();
        } else if (strcmp(cmd, "list") == 0) {
            cmd_list();
        } else if (strcmp(cmd, "viz") == 0) {
            cmd_viz();
        } else if (strcmp(cmd, "useradd") == 0) {
            cmd_useradd(args);
        } else if (strcmp(cmd, "userdel") == 0) {
            cmd_userdel(args);
        } else if (strcmp(cmd, "groupadd") == 0) {
            cmd_groupadd(args);
        } else if (strcmp(cmd, "groupdel") == 0) {
            cmd_groupdel(args);
        } else if (strcmp(cmd, "usermod") == 0) {
            cmd_usermod(args);
        } else if (strcmp(cmd, "lusers") == 0) {
            cmd_lusers();
        } else if (strcmp(cmd, "lgroups") == 0) {
            cmd_lgroups();
        } else if (strcmp(cmd, "chmod") == 0) {
            cmd_chmod(args);
        } else if (strcmp(cmd, "chown") == 0) {
            cmd_chown(args);
        } else if (strcmp(cmd, "chgrp") == 0) {
            cmd_chgrp(args);
        } else if (strcmp(cmd, "getfacl") == 0) {
            cmd_getfacl(args);
        } else if (strcmp(cmd, "su") == 0) {
            cmd_su(args);
        } else if (strcmp(cmd, "whoami") == 0) {
            cmd_whoami();
        } else if (strcmp(cmd, "echo") == 0) {
            cmd_echo(args);
        } else if (strcmp(cmd, "help") == 0) {
            print_help();
        } else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
            printf("Goodbye!\n");
            break;
        } else {
            printf("Unknown command: %s (type 'help' for commands)\n", cmd);
        }
    }

    if (fs_fd >= 0) {
        close(fs_fd);
    }

    return 0;
}