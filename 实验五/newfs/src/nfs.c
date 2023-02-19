#include "nfs.h"

/******************************************************************************
* SECTION: 宏定义
*******************************************************************************/
#define OPTION(t, p)        { t, offsetof(struct custom_options, p), 1 }

/******************************************************************************
* SECTION: 全局变量
*******************************************************************************/
static const struct fuse_opt option_spec[] = {		/* 用于FUSE文件系统解析参数 */
	OPTION("--device=%s", device),
	FUSE_OPT_END
};

struct custom_options nfs_options;			 /* 全局选项 */
struct nfs_super nfs_super; 
/******************************************************************************
* SECTION: FUSE操作定义
*******************************************************************************/
static struct fuse_operations operations = {
	.init = nfs_init,					/* mount文件系统 */		
	.destroy = nfs_destroy,				/* umount文件系统 */
	.mkdir = nfs_mkdir,					/* 建目录，mkdir */
	.getattr = nfs_getattr,				/* 获取文件属性，类似stat，必须完成 */
	.readdir = nfs_readdir,				/* 填充dentrys */
	.mknod = nfs_mknod,					/* 创建文件，touch相关 */
	.write = NULL,						/* 写入文件 */
	.read = NULL,						/* 读文件 */
	.utimens = nfs_utimens,				/* 修改时间，忽略，避免touch报错 */
	.truncate = NULL,					/* 改变文件大小 */
	.unlink = NULL,						/* 删除文件 */
	.rmdir	= NULL,						/* 删除目录， rm -r */
	.rename = NULL,						/* 重命名，mv */

	.open = NULL,							
	.opendir = NULL,
	.access = NULL
};
/******************************************************************************
* SECTION: 必做函数实现
*******************************************************************************/
/**
 * @brief 挂载（mount）文件系统
 * 
 * @param conn_info 可忽略，一些建立连接相关的信息 
 * @return void*
 */
void* nfs_init(struct fuse_conn_info * conn_info) {
	/* TODO: 在这里进行挂载 */
	if (nfs_mount(nfs_options) != NFS_ERROR_NONE) {
		fuse_exit(fuse_get_context()->fuse);
		return NULL;
	} 
	return NULL;

	/* 下面是一个控制设备的示例 */
	// nfs_super.fd = ddriver_open(nfs_options.device);
}

/**
 * @brief 卸载（umount）文件系统
 * 
 * @param p 可忽略
 * @return void
 */
void nfs_destroy(void* p) {
	/* TODO: 在这里进行卸载 */
	if (nfs_umount() != NFS_ERROR_NONE) {
		fuse_exit(fuse_get_context()->fuse);
		return;
	}
	return;
	
	// ddriver_close(nfs_super.fd);
	// return;
}

/**
 * @brief 创建目录
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建模式（只读？只写？），可忽略
 * @return int 0成功，否则失败
 */
int nfs_mkdir(const char* path, mode_t mode) {
	/* TODO: 解析路径，创建目录 */
	(void)mode;
	boolean is_find, is_root;
	char* fname;

	/* 通过nfs_lookup函数寻找上级目录项，即查找路径path最后一层对应的dentry */
	struct nfs_dentry* last_dentry = nfs_lookup(path, &is_find, &is_root);

	struct nfs_dentry* dentry;

	/* 若发现该路径命中，说明待创建目录已经存在，创建失败 */
	if (is_find) {
		return -NFS_ERROR_EXISTS;
	}

	/* 若发现最后一层目录是文件，说明不能在该目录下继续创建目录，创建失败  */
	if (NFS_IS_FILE(last_dentry->inode)) {
		return -NFS_ERROR_UNSUPPORTED;
	}

	/* 否则可以创建目录并建立连接 */
	fname  = nfs_get_fname(path);  // 获取最内层的文件名
	dentry = new_dentry(fname, N_DIR);  // 为其创建新的目录项，类型为文件夹
	dentry->parent = last_dentry;  // 该目录项的外层指向刚刚查找到的最后一层目录
	nfs_alloc_inode(dentry);  // 为该dentry创建inode
	nfs_alloc_dentry(last_dentry->inode, dentry);  // 将该dentry挂到该inode的dentrys链表里
	
	return 0;
}

/**
 * @brief 获取文件或目录的属性，该函数非常重要
 * 
 * @param path 相对于挂载点的路径
 * @param nfs_stat 返回状态
 * @return int 0成功，否则失败
 */
int nfs_getattr(const char* path, struct stat * nfs_stat) {
	/* TODO: 解析路径，获取Inode，填充nfs_stat，可参考/fs/simplefs/sfs.c的sfs_getattr()函数实现 */
	boolean	is_find, is_root;

	/* 调用nfs_lookup函数找到路径所对应的目录项 */
	struct nfs_dentry* dentry = nfs_lookup(path, &is_find, &is_root);
	if (is_find == FALSE) {
		return -NFS_ERROR_NOTFOUND;
	}

	/* 判断目录项的文件类型并据此对nfs_stat中的类型和大小进行填写 */
	if (NFS_IS_DIR(dentry->inode)) {  // 目录类型
		nfs_stat->st_mode = S_IFDIR | NFS_DEFAULT_PERM;
		nfs_stat->st_size = dentry->inode->dir_cnt * sizeof(struct nfs_dentry_d);  // 目录大小 = 目录项的数量 * 每个目录项的大小
	}
	else if (NFS_IS_FILE(dentry->inode)) {  // 文件类型
		nfs_stat->st_mode = S_IFREG | NFS_DEFAULT_PERM;
		nfs_stat->st_size = dentry->inode->size;
	}

	/* 调用相关函数对其它参数进行填写 */
	nfs_stat->st_nlink   = 1;
	nfs_stat->st_uid 	 = getuid();  		// 获取用户id
	nfs_stat->st_gid 	 = getgid();  		// 获取组id
	nfs_stat->st_atime   = time(NULL);		// 文件最后一次被访问时间  
	nfs_stat->st_mtime   = time(NULL);		// 文件最后一次被修改时间
	nfs_stat->st_blksize = NFS_IO_SZ();		// IO大小，512B

	/* 对应根目录需要特殊设置其参数 */
	if (is_root) {
		nfs_stat->st_size	= nfs_super.sz_usage; 
		nfs_stat->st_blocks = NFS_DISK_SZ() / NFS_IO_SZ();
		nfs_stat->st_nlink  = 2;		/* !特殊，根目录link数为2 */
	}

	return 0;
}

/**
 * @brief 遍历目录项，填充至buf，并交给FUSE输出
 * 
 * @param path 相对于挂载点的路径
 * @param buf 输出buffer
 * @param filler 参数讲解:
 * 
 * typedef int (*fuse_fill_dir_t) (void *buf, const char *name,
 *				const struct stat *stbuf, off_t off)
 * buf: name会被复制到buf中
 * name: dentry名字
 * stbuf: 文件状态，可忽略
 * off: 下一次offset从哪里开始，这里可以理解为第几个dentry
 * 
 * @param offset 第几个目录项？
 * @param fi 可忽略
 * @return int 0成功，否则失败
 */
int nfs_readdir(const char * path, void * buf, fuse_fill_dir_t filler, off_t offset,
			    		 struct fuse_file_info * fi) {
    /* TODO: 解析路径，获取目录的Inode，并读取目录项，利用filler填充到buf，可参考/fs/simplefs/sfs.c的sfs_readdir()函数实现 */
	boolean	is_find, is_root;
	int		cur_dir = offset;

	/* 使用lookup函数解析路径 */
	struct nfs_dentry* dentry = nfs_lookup(path, &is_find, &is_root);

	struct nfs_dentry* sub_dentry;
	struct nfs_inode* inode;

	/* 若成功找到路径，则获取目录的Inode，并读取对应目录项*/
	if (is_find) {
		inode = dentry->inode;
		sub_dentry = nfs_get_dentry(inode, cur_dir);  // 获取inode中的第cur_dir个dentry
		if (sub_dentry) {
			/* 将该目录项的名字name保存到buf中，并使目录项偏移加一，代表下一次访问下一个目录项。*/ 
			filler(buf, sub_dentry->name, NULL, ++offset);  
		}
		return NFS_ERROR_NONE;
	}
    return -NFS_ERROR_NOTFOUND;
}

/**
 * @brief 创建文件
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建文件的模式，可忽略
 * @param dev 设备类型，可忽略
 * @return int 0成功，否则失败
 */
int nfs_mknod(const char* path, mode_t mode, dev_t dev) {
	/* TODO: 解析路径，并创建相应的文件 */
	boolean	is_find, is_root;
	
	/* 使用lookup函数解析路径，寻找上级目录项，即查找路径最后一层对应的dentry */
	struct nfs_dentry* last_dentry = nfs_lookup(path, &is_find, &is_root);

	struct nfs_dentry* dentry;
	char* fname;
	
	/* 若发现该路径命中，说明待创建目录已经存在，创建失败 */
	if (is_find == TRUE) {
		return -NFS_ERROR_EXISTS;
	}

	/* 否则可以创建目录项和对应的inode，并和父目录项建立连接 */
	fname = nfs_get_fname(path);  // 获取最内层的文件名
	/* 根据mode创建对应类型的dentry */
	if (S_ISREG(mode)) {
		dentry = new_dentry(fname, N_FILE);
	}
	else if (S_ISDIR(mode)) {
		dentry = new_dentry(fname, N_DIR);
	}
	else {
		dentry = new_dentry(fname, N_FILE);
	}
	dentry->parent = last_dentry;  // 该目录项的外层指向刚刚查找到的最后一层目录
	nfs_alloc_inode(dentry);  // 为该dentry创建inode
	nfs_alloc_dentry(last_dentry->inode, dentry);  // 将该dentry挂到该inode的dentrys链表里

	return 0;
}

/**
 * @brief 修改时间，为了不让touch报错 
 * 
 * @param path 相对于挂载点的路径
 * @param tv 实践
 * @return int 0成功，否则失败
 */
int nfs_utimens(const char* path, const struct timespec tv[2]) {
	(void)path;
	return 0;
}
/******************************************************************************
* SECTION: 选做函数实现
*******************************************************************************/
/**
 * @brief 写入文件
 * 
 * @param path 相对于挂载点的路径
 * @param buf 写入的内容
 * @param size 写入的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 写入大小
 */
int nfs_write(const char* path, const char* buf, size_t size, off_t offset,
		        struct fuse_file_info* fi) {
	/* 选做 */
	return size;
}

/**
 * @brief 读取文件
 * 
 * @param path 相对于挂载点的路径
 * @param buf 读取的内容
 * @param size 读取的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 读取大小
 */
int nfs_read(const char* path, char* buf, size_t size, off_t offset,
		       struct fuse_file_info* fi) {
	/* 选做 */
	return size;			   
}

/**
 * @brief 删除文件
 * 
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则失败
 */
int nfs_unlink(const char* path) {
	/* 选做 */
	return 0;
}

/**
 * @brief 删除目录
 * 
 * 一个可能的删除目录操作如下：
 * rm ./tests/mnt/j/ -r
 *  1) Step 1. rm ./tests/mnt/j/j
 *  2) Step 2. rm ./tests/mnt/j
 * 即，先删除最深层的文件，再删除目录文件本身
 * 
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则失败
 */
int nfs_rmdir(const char* path) {
	/* 选做 */
	return 0;
}

/**
 * @brief 重命名文件 
 * 
 * @param from 源文件路径
 * @param to 目标文件路径
 * @return int 0成功，否则失败
 */
int nfs_rename(const char* from, const char* to) {
	/* 选做 */
	return 0;
}

/**
 * @brief 打开文件，可以在这里维护fi的信息，例如，fi->fh可以理解为一个64位指针，可以把自己想保存的数据结构
 * 保存在fh中
 * 
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则失败
 */
int nfs_open(const char* path, struct fuse_file_info* fi) {
	/* 选做 */
	return 0;
}

/**
 * @brief 打开目录文件
 * 
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则失败
 */
int nfs_opendir(const char* path, struct fuse_file_info* fi) {
	/* 选做 */
	return 0;
}

/**
 * @brief 改变文件大小
 * 
 * @param path 相对于挂载点的路径
 * @param offset 改变后文件大小
 * @return int 0成功，否则失败
 */
int nfs_truncate(const char* path, off_t offset) {
	/* 选做 */
	return 0;
}


/**
 * @brief 访问文件，因为读写文件时需要查看权限
 * 
 * @param path 相对于挂载点的路径
 * @param type 访问类别
 * R_OK: Test for read permission. 
 * W_OK: Test for write permission.
 * X_OK: Test for execute permission.
 * F_OK: Test for existence. 
 * 
 * @return int 0成功，否则失败
 */
int nfs_access(const char* path, int type) {
	/* 选做: 解析路径，判断是否存在 */
	return 0;
}
/******************************************************************************
* SECTION: FUSE入口
*******************************************************************************/
int main(int argc, char **argv)
{
    int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	nfs_options.device = strdup("/home/students/200110513/ddriver");

	if (fuse_opt_parse(&args, &nfs_options, option_spec, NULL) == -1)
		return -1;
	
	ret = fuse_main(args.argc, args.argv, &operations, NULL);
	fuse_opt_free_args(&args);
	return ret;
}