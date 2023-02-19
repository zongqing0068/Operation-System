#ifndef _TYPES_H_
#define _TYPES_H_

/******************************************************************************
* SECTION: Type def
*******************************************************************************/
typedef int          boolean;
typedef uint16_t     flag16;

typedef enum nfs_file_type {
    N_FILE,           // 普通文件
    N_DIR             // 目录文件
} NFS_FILE_TYPE;

/******************************************************************************
* SECTION: Macro
*******************************************************************************/
#define TRUE                    1
#define FALSE                   0
#define UINT32_BITS             32
#define UINT8_BITS              8

#define NFS_SUPER_OFS           0
#define NFS_ROOT_INO            0

#define NFS_ERROR_NONE          0
#define NFS_ERROR_ACCESS        EACCES
#define NFS_ERROR_SEEK          ESPIPE     
#define NFS_ERROR_ISDIR         EISDIR
#define NFS_ERROR_NOSPACE       ENOSPC
#define NFS_ERROR_EXISTS        EEXIST
#define NFS_ERROR_NOTFOUND      ENOENT
#define NFS_ERROR_UNSUPPORTED   ENXIO
#define NFS_ERROR_IO            EIO     /* Error Input/Output */
#define NFS_ERROR_INVAL         EINVAL  /* Invalid Args */

#define NFS_INODE_PER_FILE      1   // 每个文件使用1个inode
#define NFS_DATA_PER_FILE       6   // 采用直接索引方式，一个文件最多使用6个数据块

#define NFS_DATA_PER_DIR        2   // 此处为新增设计，一个文件夹最多使用2个数据块，
                                    // 尽管inode中的数据块号数组仍有6项，
                                    // 但若对应的是文件夹，则只使用前2项。

#define MAX_NAME_LEN            128    

/**
 * 自行规定位图大小。
 * 已知磁盘容量为4MB，一个block大小为两个512B的磁盘块，即1KB，
 * 因此，最多有4MB/1KB=4096个block。
 * (由于存在超级块和位图等，因此数据块必不足4096块)
 * 由于4096bit=512Byte，又block大小为1024B，所以data位图只需要1个block。
 * 采用直接索引方式，且一个文件不超过 6个数据块*1KB数据块大小=6KB，
 * 因此所需的索引节点数量小于等于数据块数量，所以inode位图也只需要1个block。
 * 此外，超级块也需要1个block。
 */
#define NFS_SUPER_NUM           1
#define NFS_MAP_INODE_NUM       1
#define NFS_MAP_DATA_NUM        1

/**
 * 规定索引节点inode最大个数：
 * 由上，磁盘中最多有4096个block，一个文件不超过6个block，
 * 采用直接索引方式，每个文件对应一个inode，
 * 超级块、inode位图、data位图各占1个block，
 * 则inode个数约为(4096-3)/(6+1)=585 (这是sfs中的计算方式)
 * 由于事实上，一个nfs_inode_d约占40B，即40/1024=0.04个block，
 * (1个block大约可存25个nfs_inode_d)
 * 因此inode个数最大可达约(4096-3)/(6+0.04)=678
 * 又因为规定了一个文件夹最多使用两个数据块，
 * 所以inode个数的最大值可设置的略大于此值，
 * 向上取最接近的25的倍数700为inode的最大个数。
 */
#define NFS_MAX_INO             700

/******************************************************************************
* SECTION: Macro Function
*******************************************************************************/

// io大小
#define NFS_IO_SZ()                     (nfs_super.sz_io)
// 磁盘大小
#define NFS_DISK_SZ()                   (nfs_super.sz_disk)
// 设备描述符
#define NFS_DRIVER()                    (nfs_super.fd)
// 一个block的大小
#define NFS_BLK_SZ()                    (nfs_super.sz_blk)
// 一个索引节点在磁盘上的大小
#define NFS_INODE_SZ()                  (sizeof(struct nfs_inode_d))    
// blks个block的大小
#define NFS_BLKS_SZ(blks)               (blks * NFS_BLK_SZ())
// 以round为单位向下取整
#define NFS_ROUND_DOWN(value, round)    (value % round == 0 ? value : (value / round) * round)
// 以round为单位向上取整
#define NFS_ROUND_UP(value, round)      (value % round == 0 ? value : (value / round + 1) * round)
// 设置文件名
#define NFS_ASSIGN_FNAME(pnfs_dentry, _fname)   memcpy(pnfs_dentry->name, _fname, strlen(_fname))
// 计算第ino个索引节点的偏移，事实上inode连续存储在磁盘中，因此只会在最后一个存储inode的数据块中存在碎片
#define NFS_INO_OFS(ino)                (nfs_super.inode_offset + ino * NFS_INODE_SZ())
// 计算第dno个数据块的偏移
#define NFS_DATA_OFS(dno)               (nfs_super.data_offset + NFS_BLKS_SZ(dno))
// 判断该inode是否对应目录
#define NFS_IS_DIR(pinode)              (pinode->dentry->ftype == N_DIR)
// 判断该inode是否对应文件
#define NFS_IS_FILE(pinode)             (pinode->dentry->ftype == N_FILE)

/******************************************************************************
* SECTION: FS Specific Structure - In memory structure
*******************************************************************************/
struct custom_options {
	const char*        device;
};

struct nfs_super {
    uint32_t            magic;              // 幻数，用于标识文件系统
    int                 fd;                 // 设备描述符
    /* TODO: Define yourself */

    int                 sz_io;              // 设备单次IO大小，512B
    int                 sz_disk;            // 磁盘大小，4MB
    int                 sz_usage;           
    int                 sz_blk;             // EXT2文件系统的块大小，1024B
    
    int                 max_ino;            // 最多支持的文件数，即索引节点最大个数
    uint8_t*            map_inode;          // inode位图，用1bit记录某一个索引节点是否被使用
    int                 map_inode_blks;     // inode位图占用的块数
    int                 map_inode_offset;   // inode位图在磁盘上的偏移
    int                 inode_offset;       // 第一个索引节点在磁盘上的偏移
    
    int                 max_data;           // 最多支持的数据块数
    uint8_t*            map_data;           // data位图，用1bit记录某一个数据块是否被使用
    int                 map_data_blks;      // data位图占用的块数
    int                 map_data_offset;    // data位图在磁盘上的偏移
    int                 data_offset;        // 第一个数据块在磁盘上的偏移

    boolean             is_mounted;         // 是否挂载

    struct nfs_dentry*  root_dentry;        // 根目录项
};

struct nfs_inode {  // 每个inode对应一个文件
    uint32_t            ino;                                    // 在inode位图中的下标 
    /* TODO: Define yourself */
    int                 size;                                   // 文件已占用的空间
    int                 dir_cnt;                                // 目录项数量
    struct nfs_dentry*  dentry;                                 // 指向该inode的dentry
    struct nfs_dentry*  dentrys;                                // 当inode是文件夹时，存储所有目录项
    uint8_t *           data_addr_pointer[NFS_DATA_PER_FILE];   // 数据块指针数组，指向内存地址
    int                 data_pointer[NFS_DATA_PER_FILE];        // 数据块块号数组 （可固定分配）
};

struct nfs_dentry {
    char                name[MAX_NAME_LEN];
    uint32_t            ino;                // dentry所指向的inode在位图中的下标
    /* TODO: Define yourself */
    struct nfs_dentry*  parent;             // 父亲inode的dentry
    struct nfs_dentry*  brother;            // 兄弟inode的dentry
    struct nfs_inode*   inode;              // 指向的inode
    NFS_FILE_TYPE       ftype;              // dentry所指向的inode对应的文件类型
};

static inline struct nfs_dentry* new_dentry(char * fname, NFS_FILE_TYPE ftype) {
    struct nfs_dentry * dentry = (struct nfs_dentry *)malloc(sizeof(struct nfs_dentry));
    memset(dentry, 0, sizeof(struct nfs_dentry));
    NFS_ASSIGN_FNAME(dentry, fname);
    dentry->ftype   = ftype;
    dentry->ino     = -1;
    dentry->inode   = NULL;
    dentry->parent  = NULL;
    dentry->brother = NULL;     
    return dentry;                                         
}

/******************************************************************************
* SECTION: FS Specific Structure - Disk structure
*******************************************************************************/
struct nfs_super_d
{
    uint32_t            magic_num;
    int                 sz_usage;
    
    int                 map_inode_blks;     // inode位图占用的块数
    int                 map_inode_offset;   // inode位图在磁盘上的偏移
    int                 inode_offset;       // 第一个索引节点在磁盘上的偏移
    
    int                 map_data_blks;      // data位图占用的块数
    int                 map_data_offset;    // data位图在磁盘上的偏移
    int                 data_offset;        // 第一个数据块在磁盘上的偏移
};

struct nfs_inode_d
{  // 每个inode对应一个文件
    int                 ino;                                // 在inode位图中的下标
    int                 size;                               // 文件已占用空间 
    NFS_FILE_TYPE       ftype;                              // 文件类型（目录类型、普通文件类型）
    int                 dir_cnt;                            // 如果是目录类型文件，下面有几个目录项
    int                 data_pointer[NFS_DATA_PER_FILE];    // 数据块块号数组（可固定分配） 
};  

struct nfs_dentry_d
{
    char                fname[MAX_NAME_LEN];    // 指向的ino文件名
    NFS_FILE_TYPE       ftype;                  // 指向的ino文件类型
    int                 ino;                    // 指向的ino号
}; 

#endif /* _TYPES_H_ */