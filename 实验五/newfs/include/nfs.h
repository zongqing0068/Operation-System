#ifndef _NFS_H_
#define _NFS_H_

#define FUSE_USE_VERSION 26
#include "stdio.h"
#include "stdlib.h"
#include <unistd.h>
#include "fcntl.h"
#include "string.h"
#include "fuse.h"
#include <stddef.h>
#include "ddriver.h"
#include "errno.h"
#include "types.h"

#define NFS_MAGIC           0x00000513      /* TODO: Define by yourself */
#define NFS_DEFAULT_PERM    0777   /* 全权限打开 */
 
/******************************************************************************
* SECTION: nfs_utils.c
*******************************************************************************/
char* 			   nfs_get_fname(const char* path);
int 			   nfs_calc_lvl(const char * path);
int 			   nfs_driver_read(int offset, uint8_t *out_content, int size);
int 			   nfs_driver_write(int offset, uint8_t *in_content, int size);
int 			   nfs_mount(struct custom_options options);
int 			   nfs_umount();
int 			   nfs_alloc_dentry(struct nfs_inode * inode, struct nfs_dentry * dentry);
// int 			   nfs_drop_dentry(struct nfs_inode * inode, struct nfs_dentry * dentry);
struct nfs_inode*  nfs_alloc_inode(struct nfs_dentry * dentry);
int 			   nfs_sync_inode(struct nfs_inode * inode);
// int 			   nfs_drop_inode(struct nfs_inode * inode);
struct nfs_inode*  nfs_read_inode(struct nfs_dentry * dentry, int ino);
struct nfs_dentry* nfs_get_dentry(struct nfs_inode * inode, int dir);
struct nfs_dentry* nfs_lookup(const char * path, boolean * is_find, boolean* is_root);

/******************************************************************************
* SECTION: nfs.c
*******************************************************************************/
void* 			   nfs_init(struct fuse_conn_info *);
void  			   nfs_destroy(void *);
int   			   nfs_mkdir(const char *, mode_t);
int   			   nfs_getattr(const char *, struct stat *);
int   			   nfs_readdir(const char *, void *, fuse_fill_dir_t, off_t, struct fuse_file_info *);
int   			   nfs_mknod(const char *, mode_t, dev_t);
int   			   nfs_write(const char *, const char *, size_t, off_t, struct fuse_file_info *);
int   			   nfs_read(const char *, char *, size_t, off_t, struct fuse_file_info *);
int   			   nfs_access(const char *, int);
int   			   nfs_unlink(const char *);
int   			   nfs_rmdir(const char *);
int   			   nfs_rename(const char *, const char *);
int   			   nfs_utimens(const char *, const struct timespec tv[2]);
int   			   nfs_truncate(const char *, off_t);
int   			   nfs_open(const char *, struct fuse_file_info *);
int   			   nfs_opendir(const char *, struct fuse_file_info *);

#endif  /* _nfs_H_ */