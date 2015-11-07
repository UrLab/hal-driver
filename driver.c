#include <fuse.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "hal.h"
#include "logger.h"

#define streq(s1,s2) (strcmp((s1),(s2)) == 0)
#define HAL_IDX(cat,name) idx(name, hal->cat, hal->n_##cat)

static HAL *hal = NULL;
static int my_uid, my_gid;

void *HALFS_init(struct fuse_conn_info *conn)
{
    hal = HAL_connect();
    if (! hal){
        HAL_WARN("Cannot connect to arduino; quit !");
        exit(1);
    }
    return NULL;
}

static int HALFS_open(const char *path, struct fuse_file_info *fi)
{
    HALFS *file = HALFS_find(hal->root, path);
    if (file)
        return 0;
    return -ENOENT;
}

static int HALFS_read(
    const char *path, 
    char *buf, 
    size_t size, 
    off_t offset,
    struct fuse_file_info *fi
){
    HALFS *file = HALFS_find(hal->root, path);
    int res = -ENOENT;
    if (file){
        res = file->ops.read(hal->conn, file->id, buf, size, offset);
        //DEBUG("Finished READ %s (call len:%lu return len:%d)", path, size, res);
    }
    return res;
}


static int HALFS_write(
    const char *path, 
    const char *buf, 
    size_t size, 
    off_t offset,
    struct fuse_file_info *fi
){
    HALFS *file = HALFS_find(hal->root, path);
    int res = -ENOENT;
    if (file){
        res = file->ops.write(hal->conn, file->id, buf, size, offset);
        //DEBUG("Finished WRITE %s (call len:%lu return len:%d)", path, size, res);
    }
    return res;
}

static int HALFS_size(const char *path, struct fuse_file_info *fi)
{
    HALFS *file = HALFS_find(hal->root, path);
    if (file)
        return (int) file->ops.size;
    return -ENOENT;
}


static int HALFS_trunc(const char *path, off_t offset) 
{
    HALFS *file = HALFS_find(hal->root, path);
    if (file)
        return file->ops.trunc(hal->conn, file->id);
    return -ENOENT;
}

static int HALFS_readdir(
    const char *path, 
    void *buf, 
    fuse_fill_dir_t filler,
    off_t offset, 
    struct fuse_file_info *fi
){

    HALFS *dir = HALFS_find(hal->root, path);
    if (! dir)
        return -ENOENT;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    for (HALFS *it=dir->first_child; it!=NULL; it=it->next_sibling)
        filler(buf, it->name, NULL, 0);

    return 0;
}

static int HALFS_readlink(const char *path, char *buf, size_t size)
{
    HALFS *file = HALFS_find(hal->root, path);
    if (file){
        if (file->ops.target == NULL)
            return -ENOENT;
        strcpy(buf, file->ops.target);
        return 0;
    }
    return -ENOENT;
}

static int HALFS_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));
    HALFS *file = HALFS_find(hal->root, path);
    if (! file)
        return -ENOENT;

    stbuf->st_uid = my_uid;
    stbuf->st_gid = my_gid;

    stbuf->st_mode = HALFS_mode(file);

    if (file->first_child){
        /* has child: Directory */
        stbuf->st_mode |= S_IFDIR;
        stbuf->st_nlink = 2;
    } else if (file->ops.target != NULL){
        /* has target: Symlink */
        stbuf->st_mode |= S_IFLNK;
        stbuf->st_nlink = 1;
        stbuf->st_size = strlen(file->ops.target);
    } else {
        /* otherwise: Regular file */
        stbuf->st_mode |= S_IFREG;
        stbuf->st_nlink = 1;
        stbuf->st_size = file->ops.size;
    }

    return 0;
}

static struct fuse_operations hal_ops = {
    .getattr    = HALFS_getattr,
    .readdir    = HALFS_readdir,
    .open       = HALFS_open,
    .read       = HALFS_read,
    .write      = HALFS_write,
    .truncate   = HALFS_trunc,
    .init       = HALFS_init,
    .readlink   = HALFS_readlink
};

/* ============================================== */

int main(int argc, char *argv[])
{
    my_uid = getuid();
    my_gid = getgid();
    return fuse_main(argc, argv, &hal_ops, NULL);
}