#include <sys/file.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

int get_lock_fd(uint64_t entry_off) {
    char path[128];
    sprintf(path, "/tmp/minifs_lock_%llu", (unsigned long long) entry_off);
    return open(path, O_CREAT | O_RDWR, 0666);
}

int lock_read(uint64_t entry_off) {
    int fd = get_lock_fd(entry_off);
    flock(fd, LOCK_SH);   // shared lock
    return fd;
}

int lock_write(uint64_t entry_off) {
    int fd = get_lock_fd(entry_off);
    flock(fd, LOCK_EX);   // exclusive lock
    return fd;
}

void unlock(int fd) {
    flock(fd, LOCK_UN);
    close(fd);
}

int get_super_lock_fd(void) {
    return open("/tmp/minifs_super_lock", O_CREAT | O_RDWR, 0666);
}

int lock_super(void) {
    int fd = get_super_lock_fd();
    flock(fd, LOCK_EX);  // exclusive lock for superblock
    return fd;
}

void unlock_super(int fd) {
    flock(fd, LOCK_UN);
    close(fd);
}