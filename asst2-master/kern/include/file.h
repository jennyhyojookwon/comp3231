/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>


/*
 * Put your function declarations and data types here ...
 */
struct file {
    int file_mode;
    struct vnode *file_vnode;
    off_t file_offset;
    struct lock *f_lock;
    int f_refcount;
};

struct filetable {
    struct file *files[OPEN_MAX];
};

int open_file(char *filename, int flag, int mode, int *fd);
int insert_file(struct file *file, int *fd);
int initialize_filetable(void);
int search_filetable(int fd, struct file **file);
void destroy_filetable(struct filetable *ft);

/* syscall prototypes */
int sys_open(userptr_t filename, int flags, int mode, int *retval);
int sys_read(int fd, void *buf, size_t buflen, int* retval);
int sys_write(int fd, userptr_t buf, size_t size, int *retval);
int sys_lseek(int fd, off_t pos, int whence, off_t *retval);
int sys_close(int fd, int *retval);
int sys_dup2(int oldfd, int newfd, int *retval);




#endif /* _FILE_H_ */
