#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>

/*
 * Syscall implementations
 */

/* open() system call */
int sys_open(userptr_t filename, int flags, int mode, int *retval) {
	char fname[PATH_MAX];

	/*copies a null-terminated C string from the specified user address into 
	a DTrace scratch buffer, and returns the address of this buffer.*/
	int err = copyinstr(filename, fname, PATH_MAX, NULL);
	if(err) {
		*retval = -1;
		return err;
	}

	return open_file(fname, flags, mode, retval);
}

/* read() system call */
int sys_read(int fd, void *buf, size_t buflen, int *retval) {
	struct file *file = curthread->t_filetable->files[fd];
	struct iovec iov;
	struct uio u_io;
	int result;

	/* Bad file desciptor */
	if (fd < 0 || fd >= OPEN_MAX) {
		*retval = -1;
		return EBADF;
	}

	/* fd not associated with an opened file */
	if (file == NULL) {
		*retval = -1;
		return EBADF;
	}

	/* Aquire lock to file */
	lock_acquire(file->f_lock);

	/* File not opened for reading */
	if (file->file_mode != O_RDONLY && file->file_mode != O_RDWR) {
		lock_release(file->f_lock);
		*retval = -1;
		return EBADF;
	}

	/* Initialise uio and iovec for buffer read (in kernel) */
	char *k_buf = (char *) kmalloc(buflen);
	uio_kinit(&iov, &u_io, (void *) k_buf, buflen, file->file_offset, UIO_READ);

	/* Read file */
	result = VOP_READ(file->file_vnode, &u_io);
	if (result) {
		lock_release(file->f_lock);
		kfree(k_buf);
		*retval = -1;
		return result;
	}

	/* Copy kbuf out to buf */
	copyout((const void *) k_buf, (userptr_t) buf, buflen);
	kfree(k_buf);

	lock_release(file->f_lock);

	/* retval is the number of bytes read */
	*retval = buflen - u_io.uio_resid;

	return 0;
}

/* write() system call */
int sys_write(int fd, userptr_t buf, size_t size, int *retval) {

	struct uio u_io;
	struct iovec u_iovec;
	struct file *file;

	int err = search_filetable(fd, &file);

	if(err) {
		*retval = -1;
		return err;
	}
	lock_acquire(file->f_lock);

	if(file->file_mode == O_RDONLY) {
		*retval = -1;
		lock_release(file->f_lock);
		return EBADF;
	}

	/* Init kernel buffer and copy content to kbuf */
	size_t actual_size;
	char *kbuf = (char *) kmalloc(size);
	copyinstr((userptr_t) buf, kbuf, size, &actual_size);

	/*Initialize a uio */
	uio_kinit(&u_iovec, &u_io, (void *) kbuf, size, file->file_offset, UIO_WRITE);
    
	err = VOP_WRITE(file->file_vnode, &u_io);
	if(err){
		*retval = -1;
		lock_release(file->f_lock);
		return err;
	}

	file->file_offset += size;

	lock_release(file->f_lock);

	/* retval is the number of bytes wrote */
	*retval = size - u_io.uio_resid;

	return 0;
}


/* lseek() system call */
int sys_lseek(int fd, off_t pos, int whence, off_t *retval) {
	struct file *file = curthread->t_filetable->files[fd];
	struct stat temp;
	int ret;

	/* Bad file desciptor */
	if (fd < 0 || fd >= OPEN_MAX) {
		*retval = -1;
		return EBADF;
	}

	/* fd not associated with an opened file */
	if (file == NULL) {
		*retval = -1;
		return EBADF;
	}

	/* Aquire lock to file */
	lock_acquire(file->f_lock);

	/* File not seekable */
	if (!VOP_ISSEEKABLE(file->file_vnode)) {
		lock_release(file->f_lock);
		*retval = -1;
		return ESPIPE;
	}
	
	/* Determine new position */
	switch (whence) {
		/* New position is pos */
		case SEEK_SET:
		*retval = pos;
		break;

		/* New position is current position plus pos */
		case SEEK_CUR:
		*retval = file->file_offset + pos;
		break;

		/* New position is end of file plus pos */
		case SEEK_END:
		/* Get stats of file to get size */
		ret = VOP_STAT(file->file_vnode, &temp);
		if (ret) {
			lock_release(file->f_lock);
			return ret;
		}
		*retval = temp.st_size + pos;
		break;

		/* whence is invalid */
		default:
		lock_release(file->f_lock);
		*retval = -1;
		return EINVAL;
	}

	/* The resulting seek position would be negative */
	if (*retval < 0) {
		lock_release(file->f_lock);
		*retval = -1;
		return EINVAL;
	}
	
	/* Update position in file */
	file->file_offset = *retval;
	lock_release(file->f_lock);

	return 0;
}

/* close() system call */
int sys_close(int fd, int *retval) {
	struct file *file = curthread->t_filetable->files[fd];

    if (fd < 0 || fd >= OPEN_MAX) {
		*retval = -1;
        return EBADF;
    }

    if (file == NULL) {
		*retval = -1;
        return EBADF;
    } 
	
    lock_acquire(file->f_lock);

    /* each file should have refcount of at least one after sys_open() */
    KASSERT( file->f_refcount > 0); 
    file->f_refcount--;

	*retval = 0;

    if (file->f_refcount == 0) {
        vfs_close(file-> file_vnode);
        lock_release(file -> f_lock);
        lock_destroy(file -> f_lock);
        kfree(file);
        curthread->t_filetable->files[fd] = NULL;
        return 0;
    }

    lock_release(file->f_lock);
	curthread->t_filetable->files[fd] = NULL;

    return 0;
}

/* dup2() system call */
/* TODO: too many open files (EMFILE)
 *		 too many open files in system (ENFILE) */
int sys_dup2(int oldfd, int newfd, int *retval) {
	struct filetable *curfiletable = curthread->t_filetable;

	/* oldfd/newfd out of range */
	if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX) {
		*retval = -1;
		return EBADF;
	}

	/* oldfd not associated with any open file */
	if (curfiletable->files[oldfd] == NULL) {
		*retval = -1;
		return EBADF;
	}

	/* No effect when oldfd == newfd */
	if (oldfd == newfd) {
		*retval = oldfd;
		return 0;
	}

	/* Close file associated with newfd */
	if (curfiletable->files[newfd] != NULL) {
		int ret;
		if (sys_close(newfd, &ret)) {
			*retval = -1;
			return EBADF;
		}
	}

	/* Copy */
	lock_acquire(curfiletable->files[oldfd]->f_lock);

	curfiletable->files[newfd] = curfiletable->files[oldfd];

	/* Increment file->f_refcount */
	curfiletable->files[newfd]->f_refcount++;

	lock_release(curfiletable->files[oldfd]->f_lock);

	/* retval contains newfd */
	*retval = newfd;

	return 0;
}

/*
 * syscall support functions
 */

int open_file(char *file_name, int flag, int mode, int *fd) {
	struct vnode *vn;
	struct file *file;
	size_t act_len;
	
	/* error checking */
	char *k_file_name = (char *) kmalloc(sizeof(char) * PATH_MAX);
	if (k_file_name == NULL) {
		*fd = -1;
		return ENOMEM;
	}
	copyinstr((userptr_t) file_name, k_file_name, PATH_MAX, &act_len);

	int err = vfs_open(k_file_name, flag, mode, &vn);
	if(err) {
		*fd = -1;
		kfree(k_file_name);
		return err;
	}
	
	/* vfs_close for NULL file */
	file = kmalloc(sizeof(struct file));
	if (file == NULL) {
		*fd = -1;
		kfree(k_file_name);
		vfs_close(vn);
		return ENOMEM;
	}
	
	file->file_vnode = vn;
	file->file_offset = 0;
	file->f_lock = lock_create("lock");
	file->file_mode = flag;
	file->f_refcount = 1;

	/* NULL lock */
	if(file->f_lock == NULL) {
		*fd = -1;
		kfree(k_file_name);
		vfs_close(vn);
		kfree(file);
		return ENOMEM;
	}
	
    err = insert_file(file, fd);

	if(err) {
		*fd = -1;
		kfree(k_file_name);
		lock_destroy(file->f_lock);
		kfree(file);
		vfs_close(vn);
		return err;
	}
    
	kfree(k_file_name);
	return 0;
}

/* insert_file():
 *
 * This function inserts a file pointer to an empty slot in the thread's
 * file table and associates the fd variable with the index of the
 * slot being placed in  */
int insert_file(struct file *file, int *fd) {
	for(int i = 0; i < OPEN_MAX; i++) {
		if(curthread->t_filetable->files[i] == NULL) {
			*fd = i;
			curthread->t_filetable->files[i] = file;
			return 0;
		}
	}
	return EMFILE; 
}	

/* initialize_filetable():
 *
 * This function is called at the begining of runprogram and initialises
 * a filetable for the current thread.
 *  */
int initialize_filetable(void) {
	struct file *file;
	/* Allcoate memory for the filetable */	
	curthread->t_filetable = kmalloc(sizeof(struct filetable));
	if (curthread->t_filetable == NULL)
		return ENOMEM;

	/* initialize with NULL for all the files */
	for (int i = 0; i < OPEN_MAX; i++)
		curthread->t_filetable->files[i] = NULL;
	
	/* stdin, stdout, stderr */
	struct vnode *v0, *v1, *v2;
	char con0[5] = "con:";
	char con1[5] = "con:";
	char con2[5] = "con:";

	curthread->t_filetable->files[0] = (struct file *) kmalloc(sizeof(struct file));
	curthread->t_filetable->files[1] = (struct file *) kmalloc(sizeof(struct file));
	curthread->t_filetable->files[2] = (struct file *) kmalloc(sizeof(struct file));

	int res0 = vfs_open(con0, O_RDONLY, 0664, &v0);
	if (res0)
		return EINVAL;
	file = curthread->t_filetable->files[0];
	file->file_vnode = v0;
	file->file_mode = O_RDONLY;
	file->file_offset = 0;
	file->f_lock = lock_create(con0);

	int res1 = vfs_open(con1, O_WRONLY, 0664, &v1);
	if (res1)
		return EINVAL;
	file = curthread->t_filetable->files[1];
	file->file_vnode = v1;
	file->file_mode = O_WRONLY;
	file->file_offset = 0;
	file->f_lock = lock_create(con1);
	
	int res2 = vfs_open(con2, O_WRONLY, 0664, &v2);
	if (res2)
		return EINVAL;
	file = curthread->t_filetable->files[2];
	file->file_vnode = v2;
	file->file_mode = O_WRONLY;
	file->file_offset = 0;
	file->f_lock = lock_create(con2);

	return 0;
}

int search_filetable(int fd, struct file **file) {

	if(fd < 0 || fd >= OPEN_MAX)
		return EBADF;

	*file = curthread->t_filetable->files[fd];
	if(*file == NULL)
		return EBADF;
	
	return 0;
}	

/* Closes all file in the filetable and free table */
void destroy_filetable(struct filetable *ft) {
	KASSERT(ft != NULL);
	int ret;

	for (int i = 0; i < OPEN_MAX; i++) {
		/* Close file if file is present */
		if (ft->files[i] != NULL) {
			KASSERT(sys_close(i, &ret) == 0);
		}
	}

	/* All files closed, free table */
	kfree(ft);
	ft = NULL;
}
