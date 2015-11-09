#ifndef DEFINE_DIRTREE_HEADER
#define DEFINE_DIRTREE_HEADER

#include <sys/stat.h>
#include <sys/types.h>
#include <fuse.h>
#include "com.h"

extern struct fuse_operations HALFS_ops;

typedef struct HALFS_t HALFS;

struct HALFS_t {
	const char *name;
	HALFS *first_child, *next_sibling;
    unsigned char id;
    struct {
        const char *target; /* Target for symlinks */
        int mode; /* File mode */
        size_t size;
        int (* trunc)(HALConnection *, unsigned char); /* File truncate */
        int (* read)(HALConnection *, unsigned char, char *, size_t, off_t); /* File read */
        int (* write)(HALConnection *, unsigned char, const char *, size_t, off_t); /* File write */
    } ops;
};

HALFS *HALFS_create(const char *name);
void HALFS_destroy(HALFS *self);

void HALFS_addChild(HALFS *self, HALFS *child);

HALFS *HALFS_find(HALFS *root, const char *full_path);
HALFS *HALFS_findParent(HALFS *root, const char *full_path);
HALFS *HALFS_insert(HALFS *root, const char *full_path);

int HALFS_mode(HALFS *node);

#endif
