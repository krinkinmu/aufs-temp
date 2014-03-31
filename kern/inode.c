#include <linux/slab.h>
#include "inode.h"

static struct kmem_cache *aufs_inode_cache;

struct inode *aufs_alloc_inode(struct super_block *sb)
{
	struct aufs_inode *const i =
		(struct aufs_inode *)kmem_cache_alloc(aufs_inode_cache, GFP_KERNEL);
	if (!i)
		return NULL;
	return &i->vfs_inode;
}

static void aufs_destroy_callback(struct rcu_head *head)
{
	struct inode *const inode = container_of(head, struct inode, i_rcu);
	pr_debug("destroing inode %u\n", (unsigned)inode->i_ino);
	kmem_cache_free(aufs_inode_cache, AUFS_I(inode));
}

void aufs_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, aufs_destroy_callback);
}

static void aufs_init_once(void *i)
{
	struct aufs_inode *inode = (struct aufs_inode *)i;
	inode_init_once(&inode->vfs_inode);
}

int aufs_create_inode_cache(void)
{
	aufs_inode_cache = kmem_cache_create("aufs inode cache",
							sizeof(struct aufs_inode),
							0, SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD,
							aufs_init_once);
	if (aufs_inode_cache == NULL)
		return -ENOMEM;
	return 0;
}

void aufs_destroy_inode_cache(void)
{
	rcu_barrier();
	kmem_cache_destroy(aufs_inode_cache);
}
