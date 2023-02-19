#include "../include/nfs.h"
#include "../include/types.h"

extern struct nfs_super      nfs_super; 
extern struct custom_options nfs_options;

/**
 * @brief 获取文件名
 * 
 * @param path 
 * @return char* 
 */
char* nfs_get_fname(const char* path) {
    // 在参数path所指向的字符串中搜索最后一次出现字符'/'的位置
    // 返回'/'后面的字符串，即文件名
    // 若未找到，则返回空指针
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}
/**
 * @brief 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 * @param path 
 * @return int 
 */
int nfs_calc_lvl(const char * path) {
    // 通过比较参数path所指向的字符串中的字符'/'的个数来计算路径的层级
    // char* path_cpy = (char *)malloc(strlen(path));
    // strcpy(path_cpy, path);
    char* str = (char*)path;
    int   lvl = 0;
    if (strcmp(path, "/") == 0) {
        return lvl;
    }
    while (*str) {
        if (*str == '/') {
            lvl++;
        }
        str++;
    }
    return lvl;
}
/**
 * @brief 驱动读
 * 
 * @param offset 
 * @param out_content 
 * @param size 
 * @return int 
 */
int nfs_driver_read(int offset, uint8_t *out_content, int size) {
    // 由于inode采用连续存储，因此此处offset不按照IO大小对齐，只将size对齐到IO大小
    int      size_aligned   = NFS_ROUND_UP(size, NFS_BLK_SZ());  // 将size对齐到块大小1024B（两个IO大小）
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);  // 开辟一段空间temp_content(cur)用于存放待读取内容
    uint8_t* cur            = temp_content;
    ddriver_seek(NFS_DRIVER(), offset, SEEK_SET);  // 移动磁盘头到offset处
    while (size_aligned != 0)
    {
        ddriver_read(NFS_DRIVER(), cur, NFS_IO_SZ());  // 按单次设备IO大小读取磁盘内容到cur中
        cur          += NFS_IO_SZ();
        size_aligned -= NFS_IO_SZ();   
    }
    memcpy(out_content, temp_content, size);  // 将读取内容拷贝到输出out_content中
    free(temp_content);  // 释放temp_content的内存空间
    return NFS_ERROR_NONE;
}
/**
 * @brief 驱动写
 * 
 * @param offset 
 * @param in_content 
 * @param size 
 * @return int 
 */
int nfs_driver_write(int offset, uint8_t *in_content, int size) {
    // 由于inode采用连续存储，因此此处offset不按照IO大小对齐，只将size对齐到IO大小
    int      size_aligned   = NFS_ROUND_UP(size, NFS_BLK_SZ());  // 将size对齐到块大小1024B
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);  // 开辟一段空间temp_content(cur)用于存放待读取内容
    uint8_t* cur            = temp_content;
    nfs_driver_read(offset, temp_content, size_aligned);  // 按IO大小进行读取（先读取一整块，然后修改相应部分后将整块写回）
    memcpy(temp_content, in_content, size);
    
    ddriver_seek(NFS_DRIVER(), offset, SEEK_SET);  // 移动磁盘头到offset处
    while (size_aligned != 0)
    {
        ddriver_write(NFS_DRIVER(), cur, NFS_IO_SZ());  // 按单次设备IO大小将cur中的内容写到磁盘中
        cur          += NFS_IO_SZ();
        size_aligned -= NFS_IO_SZ();   
    }

    free(temp_content);  // 释放temp_content的内存空间
    return NFS_ERROR_NONE;
}
/**
 * @brief 为一个inode分配dentry，采用头插法
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int nfs_alloc_dentry(struct nfs_inode* inode, struct nfs_dentry* dentry) {
    if (inode->dentrys == NULL) {  // 当前inode的dentrys链表为空
        inode->dentrys = dentry;  // 直接让inode的dentrys指针指向该dentry
    }
    else {  // 通过brother指针将dentry插入dentrys链表头部
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;  // 该目录下的目录项数加一
    return inode->dir_cnt;
}


/**
 * @brief 分配一个inode，占用位图
 * 
 * @param dentry 该dentry指向分配的inode
 * @return nfs_inode
 */
struct nfs_inode* nfs_alloc_inode(struct nfs_dentry * dentry) {
    struct nfs_inode* inode;
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    int data_cursor = 0;
    boolean is_find_free_entry = FALSE;
    int find_free_data_num = 0;

    /* 在索引节点位图上查找空闲的索引节点 */
    for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(nfs_super.map_inode_blks); byte_cursor++)
    {
        /* 按字节遍历索引节点位图，然后比对该字节(8位)中的每一位是否为1 */
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((nfs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                nfs_super.map_inode[byte_cursor] |= (0x1 << bit_cursor);  // 修改该位为已使用
                is_find_free_entry = TRUE;  // 标记为已找到
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    /* 未找到空闲索引节点 */
    if (!is_find_free_entry || ino_cursor == nfs_super.max_ino)
        return -NFS_ERROR_NOSPACE;

    /* 找到空闲索引节点，则给其分配内存 */
    inode = (struct nfs_inode*)malloc(sizeof(struct nfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;
    
    /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino   = inode->ino;
    /* inode指回dentry */
    inode->dentry = dentry;
    
    inode->dir_cnt = 0;
    inode->dentrys = NULL;

    /* 根据当前inode对应的是文件还是目录，设置相应待分配数据块的个数 */
    int data_blks_num_tofind;  // 需要分配的数据块个数
    if(NFS_IS_FILE(inode)) {
        data_blks_num_tofind = NFS_DATA_PER_FILE;
    }
    else data_blks_num_tofind = NFS_DATA_PER_DIR;


    /* 查找数据块位图，寻找空闲数据块，要求共找到data_blks_num_tofind个数据块 */
    for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(nfs_super.map_data_blks); 
         byte_cursor++)
    {
        /* 按字节遍历数据块位图，然后比对该字节(8位)中的每一位是否为1 */
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((nfs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前data_cursor位置空闲 */
                nfs_super.map_data[byte_cursor] |= (0x1 << bit_cursor);
                inode->data_pointer[find_free_data_num] = data_cursor;
                find_free_data_num++;
                break;
            }
            data_cursor++;
            if (find_free_data_num == data_blks_num_tofind) break;
        }
        if (find_free_data_num == data_blks_num_tofind) break;
    }
    
    /* 未找全空闲数据块 */
    if ((find_free_data_num != data_blks_num_tofind) || data_cursor == nfs_super.max_data)
        return -NFS_ERROR_NOSPACE;

    
    /** 若inode指向的是文件，则给其分配内存，
     * 若是文件夹则不用，目录项都存在dentrys中，
     * 只需要将对应的数据位图的NFS_DATA_PER_DIR=2个数据块标为已用即可。
     * (保证在磁盘块中预留对应的数据块用于存储刷回的目录项)
     */
    if (NFS_IS_FILE(inode)) {
        for(int i = 0; i < NFS_DATA_PER_FILE; i++){
            inode->data_addr_pointer[i] = (uint8_t *)malloc(NFS_BLKS_SZ(NFS_DATA_PER_FILE));
        }
    }

    return inode;
}

/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 * 
 * @param inode 
 * @return int 
 */
int nfs_sync_inode(struct nfs_inode * inode) {
    struct nfs_inode_d  inode_d;
    struct nfs_dentry*  dentry_cursor;
    struct nfs_dentry_d dentry_d;
    int ino             = inode->ino;

    /* 用内存中的inode更新将要刷回磁盘的inode_d */
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;
    for(int i = 0; i < NFS_DATA_PER_FILE; i++){
        inode_d.data_pointer[i] = inode->data_pointer[i];
    }

    /* 将inode_d刷回磁盘 */
    if (nfs_driver_write(NFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct nfs_inode_d)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }
                                                      /* Cycle 1: 写 INODE */
                                                      /* Cycle 2: 写 数据 */

    /** 
     * 若inode对应的是文件夹，则需要将其对应的所有dentrys刷回磁盘，
     * 由于在磁盘中，dentrys要存储在inode_d对应的数据块中，
     * 所以需要根据内存中inode中的数据块号数组data_pointer计算出磁盘中每个数据块的偏移，
     * 从而将每一个dentry_d存储到磁盘中的对应位置。
     */
    int offset, offset_max;
    if (NFS_IS_DIR(inode)) {
        dentry_cursor = inode->dentrys;

        /**
         * 由于sfs中数据块为一片连续的存储空间，所以dentrys可直接存储到数据块中，
         * 但此处的nfs中，数据块是分散存储的，因此dentrys需要按照数据块大小存储，
         * 不可出现一个dentry存储到两个数据块中的情况，所以需要遍历所有的数据块。
        */
        for(int i = 0; i < NFS_DATA_PER_DIR; i++){
            offset = NFS_DATA_OFS(inode->data_pointer[i]);  // inode对应的第i个数据块的起始偏移
            offset_max = NFS_DATA_OFS(inode->data_pointer[i] + 1);  // inode对应的第i个数据块的最大偏移
            
            /* 保证当前dentry不为空，且当前磁盘中的数据块还能存下一个完整的dentry_d*/
            while (dentry_cursor != NULL && offset + sizeof(struct nfs_dentry_d) < offset_max)
            {
                /* 用内存中dentry_cursor指向的dentry更新将要刷回磁盘的dentry_d */
                memcpy(dentry_d.fname, dentry_cursor->name, MAX_NAME_LEN);
                dentry_d.ftype = dentry_cursor->ftype;
                dentry_d.ino = dentry_cursor->ino;

                /* 将dentry_d刷回磁盘 */
                if (nfs_driver_write(offset, (uint8_t *)&dentry_d, sizeof(struct nfs_dentry_d)) != NFS_ERROR_NONE) {
                    return -NFS_ERROR_IO;                     
                }

                /* 若该dentry指向的inode不空，则将调用nfs_sync_inode函数本身将其递归地刷回磁盘 */
                if (dentry_cursor->inode != NULL) {
                    nfs_sync_inode(dentry_cursor->inode);
                }

                /* 指向下一个待刷回的dentry */ 
                dentry_cursor = dentry_cursor->brother;

                /* 更新offset */
                offset += sizeof(struct nfs_dentry_d);
            }
        }
    }
    /* 若inode对应的是文件，则直接将数据块刷回磁盘中的对应位置 */
    else if (NFS_IS_FILE(inode)) {
        for(int i = 0; i < NFS_DATA_PER_FILE; i++){
            if (nfs_driver_write(NFS_DATA_OFS(inode->data_pointer[i]), inode->data_addr_pointer[i], 
                                NFS_BLK_SZ()) != NFS_ERROR_NONE) {
                return -NFS_ERROR_IO;
            }
        }
    }
    return NFS_ERROR_NONE;
}



/**
 * @brief 
 * 
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct nfs_inode* 
 */
struct nfs_inode* nfs_read_inode(struct nfs_dentry * dentry, int ino) {
    struct nfs_inode* inode = (struct nfs_inode*)malloc(sizeof(struct nfs_inode));  // 用于存放读取到的inode
    struct nfs_inode_d inode_d;
    struct nfs_dentry* sub_dentry;
    struct nfs_dentry_d dentry_d;
    int    dir_cnt = 0, i;

    /* 从磁盘中读取ino对应的inode_d */
    if (nfs_driver_read(NFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct nfs_inode_d)) != NFS_ERROR_NONE) {
        return NULL;                    
    }

    /* 用读取到的inode_d中的已有参数更新内存中将要返回的inode中的相关参数 */
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    inode->dentry = dentry;
    inode->dentrys = NULL;
    for(int i = 0; i < NFS_DATA_PER_FILE; i++)  // 复制inode中的所有块号
        inode->data_pointer[i] = inode_d.data_pointer[i];

    /* 若inode对应的是文件夹，则需要将其所有目录项保存到该inode的dentrys中 */
    if (NFS_IS_DIR(inode)) {
        dir_cnt = inode_d.dir_cnt;        
        int offset, offset_max;
        /* 由于目录项按照block大小存储在磁盘上，所以需要按照block大小读取每一个block中的目录项 */
        for(int i = 0; i < NFS_DATA_PER_DIR; i++){
            if(dir_cnt == 0) break;
            offset = NFS_DATA_OFS(inode->data_pointer[i]);  // inode对应的第i个数据块的起始偏移
            offset_max = NFS_DATA_OFS(inode->data_pointer[i] + 1);  // inode对应的第i个数据块的最大偏移
            while(dir_cnt > 0 && offset + sizeof(struct nfs_dentry_d) < offset_max){

                /* 从磁盘中读出该偏移下的dentry_d */
                if (nfs_driver_read(offset, (uint8_t *)&dentry_d, sizeof(struct nfs_dentry_d)) != NFS_ERROR_NONE)
                    return NULL;

                /* 用从磁盘中读出的dentry_d初始化内存中的sub_dentry */
                sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
                sub_dentry->parent = inode->dentry;
                sub_dentry->ino    = dentry_d.ino;

                /* 将sub_dentry挂到inode的dentrys链表上 */
                nfs_alloc_dentry(inode, sub_dentry);

                /* 更新偏移量以及剩余目录项数量*/
                offset += sizeof(struct nfs_dentry_d);
                dir_cnt--;
            }
        }
    }

    /* 若inode对应的是文件，则按照数据块读取磁盘中的对应内容 */
    else if (NFS_IS_FILE(inode)) {
        for(int i = 0; i < NFS_DATA_PER_FILE; i++){
            inode->data_addr_pointer[i] = (uint8_t *)malloc(NFS_BLK_SZ());
            if (nfs_driver_read(NFS_DATA_OFS(inode->data_pointer[i]), (uint8_t *)inode->data_addr_pointer[i], 
                            NFS_BLK_SZ()) != NFS_ERROR_NONE)
                return NULL;
        }
    }
    return inode;
}
/**
 * @brief 返回inode中的第dir个dentry
 * 
 * @param inode 
 * @param dir [0...]
 * @return struct nfs_dentry* 
 */
struct nfs_dentry* nfs_get_dentry(struct nfs_inode * inode, int dir) {
    struct nfs_dentry* dentry_cursor = inode->dentrys;
    int    cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt) {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}
/**
 * @brief 
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry 
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 * 
 * @param path 
 * @return struct nfs_inode* 
 */
struct nfs_dentry* nfs_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct nfs_dentry* dentry_cursor = nfs_super.root_dentry;  // 从根目录开始查找
    struct nfs_dentry* dentry_ret = NULL;
    struct nfs_inode*  inode; 
    int   total_lvl = nfs_calc_lvl(path);  // 计算路径的层级以便搜索路径
    int   lvl = 0;
    boolean is_hit;
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));
    *is_root = FALSE;
    strcpy(path_cpy, path);

    /* 若层数为0表示当前是根目录，直接返回根目录项root_dentry */
    if (total_lvl == 0) {                           /* 根目录 */
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = nfs_super.root_dentry;
    }

    /**
     * 不为0则需要从根目录开始，依次匹配路径中的目录项，直到找到文件所对应的目录项，
     * 如果没找到则返回最后一次匹配的目录项。
     * 按分割符"/"切分路径，得到切分后的第一个子字符串，即最外层文件夹。
     */
    fname = strtok(path_cpy, "/");

    while (fname)
    {   
        /* 更新当前所在的层数 */
        lvl++;  

        /* Cache机制，若dentry对应的inode为空，则从磁盘中读取该dentry */
        if (dentry_cursor->inode == NULL) {           
            nfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        /* 获取当前dentry对应的inode */
        inode = dentry_cursor->inode;

        /* 若当前inode对应文件，且还没查找到对应层数，说明路径错误，跳出循环 */
        if (NFS_IS_FILE(inode) && lvl < total_lvl) {
            dentry_ret = inode->dentry;
            break;
        }

        /* 若当前inode对应文件夹，则继续向内层遍历*/
        if (NFS_IS_DIR(inode)) {
            dentry_cursor = inode->dentrys;
            is_hit        = FALSE;

            /* 遍历该文件夹下的所有dentry */
            while (dentry_cursor)
            {
                if (memcmp(dentry_cursor->name, fname, strlen(fname)) == 0) {
                    is_hit = TRUE;  // 若名字相同，则命中，跳出循环
                    break;
                }
                dentry_cursor = dentry_cursor->brother;
            }
            
            /* 若未命中，则说明未查找到该文件 */
            if (!is_hit) {
                *is_find = FALSE;
                dentry_ret = inode->dentry;
                break;
            }

            /* 若命中且到达要求层数，则说明找到文件，跳出外层循环*/
            if (is_hit && lvl == total_lvl) {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/");  // 获取下一层文件夹名字
    }

    /* 无论是否命中，均返回最终的dentry对应的inode */
    if (dentry_ret->inode == NULL) {
        dentry_ret->inode = nfs_read_inode(dentry_ret, dentry_ret->ino);
    }
    
    return dentry_ret;
}
/**
 * @brief 挂载nfs, Layout 如下
 * 
 * Layout
 * | Super | Inode Map | Data Map | Data |
 * 
 * IO_SZ = BLK_SZ
 * 
 * 每个Inode占用一个Blk
 * @param options 
 * @return int 
 */
int nfs_mount(struct custom_options options){
    /* 定义磁盘各部分结构。*/
    int                 ret = NFS_ERROR_NONE;
    int                 driver_fd;
    struct nfs_super_d  nfs_super_d; 
    struct nfs_dentry*  root_dentry;
    struct nfs_inode*   root_inode;

    int                 inode_num;
    int                 map_inode_blks;
    int                 map_data_blks;
    
    int                 super_blks;
    boolean             is_init = FALSE;

    nfs_super.is_mounted = FALSE;

    /* 打开驱动。*/
    // driver_fd = open(options.device, O_RDWR);
    driver_fd = ddriver_open(options.device);

    if (driver_fd < 0) {
        return driver_fd;
    }
    
    /* 向内存超级块中标记驱动并写入磁盘大小、单次IO大小和数据块大小。*/
    nfs_super.fd = driver_fd;
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_SIZE,  &nfs_super.sz_disk);
    ddriver_ioctl(NFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &nfs_super.sz_io);
    nfs_super.sz_blk = 2 * nfs_super.sz_io;  // EXT2文件系统一个块大小是两个IO单位，即1024B

    
    /* 读取磁盘超级块nfs_super_d到内存。*/
    if (nfs_driver_read(NFS_SUPER_OFS, (uint8_t *)(&nfs_super_d), 
                        sizeof(struct nfs_super_d)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }   

    /* 根据超级块幻数判断是否为第一次启动磁盘，如果是第一次启动磁盘，则需要建立磁盘超级块的布局。*/
                                                      /* 读取super */
    if (nfs_super_d.magic_num != NFS_MAGIC) {     /* 幻数无 */
        /* 规定各部分大小，初始化相应参数。*/
        super_blks = NFS_SUPER_NUM; // 超级块个数，1
        nfs_super_d.map_inode_blks = NFS_MAP_INODE_NUM;  // 索引节点位图块数，1
        nfs_super_d.map_data_blks = NFS_MAP_DATA_NUM;  // 数据块位图块数，1

        nfs_super_d.map_inode_offset = NFS_SUPER_OFS + NFS_BLKS_SZ(super_blks);  // inode位图偏移
        nfs_super_d.map_data_offset = nfs_super_d.map_inode_offset + NFS_BLKS_SZ(nfs_super_d.map_inode_blks);  // data位图偏移
        nfs_super.max_ino = NFS_MAX_INO;  // 索引节点最大个数，700

        nfs_super_d.inode_offset = nfs_super_d.map_data_offset + NFS_BLKS_SZ(nfs_super_d.map_data_blks);  // 第一个索引节点的偏移
        nfs_super_d.data_offset = nfs_super_d.inode_offset + NFS_ROUND_UP(NFS_INO_OFS(nfs_super.max_ino), NFS_BLK_SZ());  // 第一个数据块的偏移
        nfs_super.max_data = (nfs_super.sz_disk - nfs_super_d.data_offset) / NFS_BLK_SZ();  // 数据块最大个数

        nfs_super_d.sz_usage = 0;
        nfs_super_d.magic_num = NFS_MAGIC;

        is_init = TRUE;
    }

    // printf("!!!!\n");
    // printf("%d\n", nfs_super_d.map_inode_offset);
    // printf("%d\n", nfs_super_d.map_data_offset);
    // printf("%d\n", nfs_super_d.inode_offset);
    // printf("%d\n", nfs_super_d.data_offset);

    /* 初始化内存中的超级块。*/

    nfs_super.sz_usage   = nfs_super_d.sz_usage;

    /* 建立inode位图的in-memory结构，先申请位图内存，后给相关参数赋值。*/
    nfs_super.map_inode = (uint8_t *)malloc(NFS_BLKS_SZ(nfs_super_d.map_inode_blks));
    nfs_super.map_inode_blks = nfs_super_d.map_inode_blks;
    nfs_super.map_inode_offset = nfs_super_d.map_inode_offset;

    /* 建立data位图的in-memory结构，先申请位图内存，后给相关参数赋值。*/
    nfs_super.map_data = (uint8_t *)malloc(NFS_BLKS_SZ(nfs_super_d.map_data_blks));
    nfs_super.map_data_blks = nfs_super_d.map_data_blks;
    nfs_super.map_data_offset = nfs_super_d.map_data_offset;

    /* 初始化索引节点块偏移和数据块偏移的值。*/
    nfs_super.inode_offset = nfs_super_d.inode_offset;
    nfs_super.data_offset = nfs_super_d.data_offset;

    /* 从磁盘上读取索引节点位图。*/
    if (nfs_driver_read(nfs_super_d.map_inode_offset, (uint8_t *)(nfs_super.map_inode), 
                        NFS_BLKS_SZ(nfs_super_d.map_inode_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    /* 创建根目录项*/
    root_dentry = new_dentry("/", N_DIR);

    /* 初始化内存中的根目录项。*/
    if (is_init) {
        root_inode = nfs_alloc_inode(root_dentry);
        nfs_sync_inode(root_inode);
    }
    
    root_inode            = nfs_read_inode(root_dentry, NFS_ROOT_INO);
    root_dentry->inode    = root_inode;
    nfs_super.root_dentry = root_dentry;
    nfs_super.is_mounted  = TRUE;

    // nfs_dump_map();
    return ret;
}
/**
 * @brief 
 * 
 * @return int 
 */
int nfs_umount() {
    struct nfs_super_d  nfs_super_d; 

    if (!nfs_super.is_mounted) {
        return NFS_ERROR_NONE;
    }

    nfs_sync_inode(nfs_super.root_dentry->inode);     /* 从根节点向下刷回所有节点 */

    /* 用内存中的超级块nfs_super更新即将写回磁盘的nfs_super_d */
    nfs_super_d.magic_num           = NFS_MAGIC;
    nfs_super_d.sz_usage            = nfs_super.sz_usage;

    nfs_super_d.map_inode_blks      = nfs_super.map_inode_blks;
    nfs_super_d.map_inode_offset    = nfs_super.map_inode_offset;
    nfs_super_d.inode_offset        = nfs_super.inode_offset;

    nfs_super_d.map_data_blks       = nfs_super.map_data_blks;
    nfs_super_d.map_data_offset     = nfs_super.map_data_offset;
    nfs_super_d.data_offset         = nfs_super.data_offset;

    /* 将超级块nfs_super_d刷回磁盘 */
    if (nfs_driver_write(NFS_SUPER_OFS, (uint8_t *)&nfs_super_d, 
                     sizeof(struct nfs_super_d)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }


    /* 将inode位图刷回磁盘 */
    if (nfs_driver_write(nfs_super_d.map_inode_offset, (uint8_t *)(nfs_super.map_inode), 
                         NFS_BLKS_SZ(nfs_super_d.map_inode_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    /* 将data位图刷回磁盘 */
    if (nfs_driver_write(nfs_super_d.map_data_offset, (uint8_t *)(nfs_super.map_data), 
                         NFS_BLKS_SZ(nfs_super_d.map_data_blks)) != NFS_ERROR_NONE) {
        return -NFS_ERROR_IO;
    }

    /* 释放内存中的inode位图和data位图 */
    free(nfs_super.map_inode);
    free(nfs_super.map_data);
    
    /* 关闭驱动 */
    ddriver_close(NFS_DRIVER());

    return NFS_ERROR_NONE;
}
