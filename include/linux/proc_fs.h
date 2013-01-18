#ifndef _LINUX_PROC_FS_H
#define _LINUX_PROC_FS_H

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/magic.h>
#include <linux/atomic.h>
#include <linux/proc_ns.h>

struct net;
struct completion;
struct mm_struct;

/*
 * The proc filesystem constants/structures
 */

/*
 * Offset of the first process in the /proc root directory..
 */
#define FIRST_PROCESS_ENTRY 256

/* Worst case buffer size needed for holding an integer. */
#define PROC_NUMBUF 13

/*
 * This is not completely implemented yet. The idea is to
 * create an in-memory tree (like the actual /proc filesystem
 * tree) of these proc_dir_entries, so that we can dynamically
 * add new files to /proc.
 *
 * The "next" pointer creates a linked list of one /proc directory,
 * while parent/subdir create the directory structure (every
 * /proc file has a parent, but "subdir" is NULL for all
 * non-directory entries).
 */

struct proc_dir_entry {
	unsigned int low_ino;
	umode_t mode;
	nlink_t nlink;
	uid_t uid;
	gid_t gid;
	loff_t size;
	const struct inode_operations *proc_iops;
	const struct file_operations *proc_fops;
	struct proc_dir_entry *next, *parent, *subdir;
	void *data;
	atomic_t count;		/* use count */
	atomic_t in_use;	/* number of callers into module in progress; */
			/* negative -> it's going away RSN */
	struct completion *pde_unload_completion;
	struct list_head pde_openers;	/* who did ->open, but not ->release */
	spinlock_t pde_unload_lock; /* proc_fops checks and pde_users bumps */
	u8 namelen;
	char name[];
};

enum kcore_type {
	KCORE_TEXT,
	KCORE_VMALLOC,
	KCORE_RAM,
	KCORE_VMEMMAP,
	KCORE_OTHER,
};

struct kcore_list {
	struct list_head list;
	unsigned long addr;
	size_t size;
	int type;
};

struct vmcore {
	struct list_head list;
	unsigned long long paddr;
	unsigned long long size;
	loff_t offset;
};

#ifdef CONFIG_PROC_FS

extern void proc_root_init(void);

void proc_flush_task(struct task_struct *task);

struct proc_dir_entry *proc_create_data(const char *name, umode_t mode,
				struct proc_dir_entry *parent,
				const struct file_operations *proc_fops,
				void *data);
extern void proc_remove(struct proc_dir_entry *);
extern void remove_proc_entry(const char *name, struct proc_dir_entry *parent);
extern int remove_proc_subtree(const char *name, struct proc_dir_entry *parent);


/*
 * proc_tty.c
 */
struct tty_driver;
#ifdef CONFIG_TTY
extern void proc_tty_init(void);
#else
static inline void proc_tty_init(void)
{ }
#endif
extern void proc_tty_register_driver(struct tty_driver *driver);
extern void proc_tty_unregister_driver(struct tty_driver *driver);

/*
 * proc_devtree.c
 */
#ifdef CONFIG_PROC_DEVICETREE
struct device_node;
struct property;
extern void proc_device_tree_init(void);
extern void proc_device_tree_add_node(struct device_node *, struct proc_dir_entry *);
extern void proc_device_tree_add_prop(struct proc_dir_entry *pde, struct property *prop);
extern void proc_device_tree_remove_prop(struct proc_dir_entry *pde,
					 struct property *prop);
extern void proc_device_tree_update_prop(struct proc_dir_entry *pde,
					 struct property *newprop,
					 struct property *oldprop);
#endif /* CONFIG_PROC_DEVICETREE */

extern struct proc_dir_entry *proc_symlink(const char *,
		struct proc_dir_entry *, const char *);
extern struct proc_dir_entry *proc_mkdir(const char *,struct proc_dir_entry *);
extern struct proc_dir_entry *proc_mkdir_data(const char *, umode_t,
					      struct proc_dir_entry *, void *);
extern struct proc_dir_entry *proc_mkdir_mode(const char *name, umode_t mode,
			struct proc_dir_entry *parent);

static inline struct proc_dir_entry *proc_create(const char *name, umode_t mode,
	struct proc_dir_entry *parent, const struct file_operations *proc_fops)
{
	return proc_create_data(name, mode, parent, proc_fops, NULL);
}
 
extern struct proc_dir_entry *proc_net_fops_create(struct net *net,
	const char *name, umode_t mode, const struct file_operations *fops);
extern void proc_net_remove(struct net *net, const char *name);
extern void proc_set_size(struct proc_dir_entry *, loff_t);
extern void proc_set_user(struct proc_dir_entry *, uid_t, gid_t);
extern void *PDE_DATA(const struct inode *);
extern void *proc_get_parent_data(const struct inode *);

#else

#define proc_net_fops_create(net, name, mode, fops)  ({ (void)(mode), NULL; })
static inline void proc_net_remove(struct net *net, const char *name) {}

static inline void proc_flush_task(struct task_struct *task)
{
}

#define proc_create(name, mode, parent, fops)  ({ (void)(mode), NULL; })

static inline struct proc_dir_entry *proc_create_data(const char *name,
	umode_t mode, struct proc_dir_entry *parent,
	const struct file_operations *proc_fops, void *data)
{
	return NULL;
}
static inline void proc_remove(struct proc_dir_entry *de) {}
#define remove_proc_entry(name, parent) do {} while (0)
#define remove_proc_subtree(name, parent) do {} while (0)

static inline struct proc_dir_entry *proc_symlink(const char *name,
		struct proc_dir_entry *parent,const char *dest) {return NULL;}
static inline struct proc_dir_entry *proc_mkdir(const char *name,
	struct proc_dir_entry *parent) {return NULL;}
static inline struct proc_dir_entry *proc_mkdir_data(const char *name,
	umode_t mode, struct proc_dir_entry *parent, void *data) { return NULL; }
static inline struct proc_dir_entry *proc_mkdir_mode(const char *name,
	umode_t mode, struct proc_dir_entry *parent) { return NULL; }
static inline void proc_set_size(struct proc_dir_entry *de, loff_t size) {}
static inline void proc_set_user(struct proc_dir_entry *de, uid_t uid, gid_t gid) {}
static inline void *PDE_DATA(const struct inode *inode) {BUG(); return NULL;}

#endif /* CONFIG_PROC_FS */

#if !defined(CONFIG_PROC_KCORE)
static inline void
kclist_add(struct kcore_list *new, void *addr, size_t size, int type)
{
}
#else
extern void kclist_add(struct kcore_list *, void *, size_t, int type);
#endif

union proc_op {
	int (*proc_get_link)(struct dentry *, struct path *);
	int (*proc_read)(struct task_struct *task, char *page);
	int (*proc_show)(struct seq_file *m,
		struct pid_namespace *ns, struct pid *pid,
		struct task_struct *task);
};

struct ctl_table_header;
struct ctl_table;

struct proc_inode {
	struct pid *pid;
	int fd;
	union proc_op op;
	struct proc_dir_entry *pde;
	struct ctl_table_header *sysctl;
	struct ctl_table *sysctl_entry;
	struct proc_ns ns;
	struct inode vfs_inode;
};

#include <linux/signal.h>

void render_sigset_t(struct seq_file *m, const char *header, sigset_t *set);

static inline struct proc_dir_entry *proc_net_mkdir(
	struct net *net, const char *name, struct proc_dir_entry *parent)
{
	return proc_mkdir_data(name, 0, parent, net);
}

#endif /* _LINUX_PROC_FS_H */
