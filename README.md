# MiniFS - A Lightweight User-Space Filesystem

MiniFS is an educational implementation of a lightweight, user-space filesystem with user and group management, Unix-style permissions, and file locking mechanisms. It provides a complete CLI interface for filesystem operations, user administration, and permission management.

## Features

### Core Filesystem Operations
- **File Management**: Create, open, read, write, and delete files
- **Block-Based Storage**: 4 MB filesystem with 4 KB blocks
- **Dynamic Allocation**: Efficient block allocation and freelist management
- **File Shrinking**: Resize files by truncating content
- **Filesystem Statistics**: Monitor used/free space and file counts

### User & Group Management
- **User System**: Create users with password hashing (SHA-256)
- **Group System**: Organize users into groups
- **Group Membership**: Support for primary and secondary group assignments
- **User Switching**: Switch between users with `su` command

### Permissions & Access Control
- **Unix-Style Permissions**: Standard rwx permissions for owner, group, and others
- **Permission Bits**: Full support for 9-bit permission model (644, 755, 777, etc.)
- **Ownership Control**: Change file owner and group with `chown` and `chgrp`
- **Permission Modification**: Update permissions with `chmod`
- **Permission Checking**: Access control enforced on read/write operations
- **Root Privileges**: Root user (UID 0) bypasses permission checks

### Concurrency & Locking
- **File-Level Locking**: Shared locks for reads, exclusive locks for writes
- **Superblock Locking**: Atomic filesystem metadata operations
- **Thread-Safe Operations**: File descriptor-based lock management

### Visualization & Debugging
- **Block Visualization**: ASCII visualization of block allocation
- **Filesystem Statistics**: Detailed space usage reporting
- **File Listing**: Browse all files with their metadata
- **File Information**: View file ownership, permissions, and size

## Building MiniFS

### Prerequisites
- GCC compiler (or compatible C compiler)
- Linux/Unix operating system
- Standard POSIX development libraries

### Compilation

```bash
gcc -o minifs main.c file_op.c filesys.c locks.c users.c -Wall -Wextra
```

This produces an executable named `minifs`.

### Clean Build

```bash
rm -f minifs filesys.db  # Remove compiled binary and filesystem
gcc -o minifs main.c file_op.c filesys.c locks.c users.c
```

## Running MiniFS

### Start the CLI

```bash
./minifs
```

The filesystem will initialize automatically. On first run, it creates a 4 MB filesystem with a root user and group.

### Example Session

```
=== MiniFS Command Line Interface ===
Initializing filesystem...
Ready!

=== MiniFS CLI Commands ===
  open <filename> [flags]        - Open a file (flags: c=create, w=write)
  read <pos> <size>              - Read from current file
  write <pos> <text>             - Write text to current file
  ...
  help                           - Show this help
  exit                           - Exit CLI
=========================

MiniFS> open test.txt c
Opened: test.txt
MiniFS> write 0 Hello World!
Wrote 12 bytes
MiniFS> read 0 12
Read 12 bytes: 'Hello World!'
MiniFS> exit
Goodbye!
```

## Command Reference

### File Operations

| Command | Syntax | Description |
|---------|--------|-------------|
| `open` | `open <filename> [flags]` | Open a file (c=create, w=write) |
| `read` | `read <pos> <size>` | Read from current file at position |
| `write` | `write <pos> <text>` | Write text to current file at position |
| `close` | `close` | Close current file |
| `rm` | `rm` | Remove current file |
| `stat` | `stat` | Display file statistics with permissions |
| `shrink` | `shrink <newsize>` | Truncate file to new size |

### Filesystem Operations

| Command | Syntax | Description |
|---------|--------|-------------|
| `list` | `list` | List all files in filesystem |
| `fsstat` | `fsstat` | Show filesystem statistics |
| `viz` | `viz` | Visualize block allocation |

### User Management

| Command | Syntax | Description |
|---------|--------|-------------|
| `useradd` | `useradd <username>` | Create a new user |
| `userdel` | `userdel <username>` | Delete a user |
| `lusers` | `lusers` | List all users |
| `su` | `su <user> <group>` | Switch to another user |
| `whoami` | `whoami` | Display current user and group |

### Group Management

| Command | Syntax | Description |
|---------|--------|-------------|
| `groupadd` | `groupadd <groupname>` | Create a new group |
| `groupdel` | `groupdel <groupname>` | Delete a group |
| `usermod` | `usermod -aG <group> <user>` | Add user to group |
| `lgroups` | `lgroups` | List all groups |

### Permission Management

| Command | Syntax | Description |
|---------|--------|-------------|
| `chmod` | `chmod <mode> <path>` | Change file permissions (e.g., 755) |
| `chown` | `chown <user>[:<group>] <path>` | Change file owner and group |
| `chgrp` | `chgrp <group> <path>` | Change file group owner |
| `getfacl` | `getfacl <path>` | Show file ownership and permissions |

### Utility Commands

| Command | Syntax | Description |
|---------|--------|-------------|
| `echo` | `echo <text>` | Print text to console |
| `help` | `help` | Display help menu |
| `exit` | `exit` | Exit MiniFS |

## Filesystem Architecture

### Storage Layout

```
Superblock (Block 0)
├── Magic: 0xDEADBEEF
├── Version: 1
├── Root Directory Offset
├── File Count
└── Freelist Head

File Blocks (Sequential)
├── File Entry (64+ bytes)
├── Block Header (16 bytes)
└── Data (4096 - file_entry_size - header_size bytes)

Free List
├── Start Offset
├── End Offset
├── Block Count
└── Next Freelist Node
```

### Block Size: 4096 bytes
### Total Filesystem Size: 4 MB (1024 blocks)

## Permission Model

MiniFS uses Unix-style permissions with a 9-bit model:

```
Mode 755 = Owner:rwx Group:r-x Other:r-x
Mode 644 = Owner:rw- Group:r-- Other:r--
Mode 777 = Owner:rwx Group:rwx Other:rwx
```

### Permission Bits
- **4 (r)**: Read permission
- **2 (w)**: Write permission
- **1 (x)**: Execute permission

### Access Control Rules
1. **Root (UID 0)**: Bypasses all permission checks
2. **Owner**: Checks owner permission bits (bits 6-8)
3. **Group**: Checks group permission bits (bits 3-5)
4. **Others**: Checks other permission bits (bits 0-2)

## Concurrency & Locking

### Lock Types
- **File Read Lock**: Shared lock for concurrent reads
- **File Write Lock**: Exclusive lock for writes
- **Superblock Lock**: Protects filesystem metadata

### Lock Mechanism
- File-based locking using `/tmp/minifs_lock_*` files
- `flock()` system calls for lock management
- Automatic lock acquisition and release

## Performance Considerations

- **Block Size**: 4 KB blocks optimized for standard filesystems
- **Freelist Management**: O(n) allocation but acceptable for educational use
- **Permission Checking**: O(1) for single-level permission checks
- **File Lookup**: O(n) traversal of file chain (suitable for small file counts)

## Limitations

- **Single-Level Directory**: No subdirectories supported
- **Maximum 1024 Files**: Limited by memory and block count
- **Limited Concurrency**: File locks are process-based, not kernel-managed
- **No Persistence**: User/group data is in-memory only
- **Temporary Lock Files**: Lock files stored in `/tmp` directory

## File Format

### Filesystem Image (filesys.db)
- Binary file containing entire 4 MB filesystem
- Persistent storage between sessions
- Delete `filesys.db` to reinitialize filesystem

## License

MIT License
