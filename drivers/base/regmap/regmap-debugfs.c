/*
 * Register map access API - debugfs
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/device.h>

#include "internal.h"

static struct dentry *regmap_debugfs_root;

/* Calculate the length of a fixed format  */
static size_t regmap_calc_reg_len(int max_val, char *buf, size_t buf_size)
{
	return snprintf(NULL, 0, "%x", max_val);
}

static ssize_t regmap_name_read_file(struct file *file,
				     char __user *user_buf, size_t count,
				     loff_t *ppos)
{
	struct regmap *map = file->private_data;
	int ret;
	char *buf;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = snprintf(buf, PAGE_SIZE, "%s\n", map->dev->driver->name);
	if (ret < 0) {
		kfree(buf);
		return ret;
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, ret);
	kfree(buf);
	return ret;
}

static const struct file_operations regmap_name_fops = {
	.open = simple_open,
	.read = regmap_name_read_file,
	.llseek = default_llseek,
};

static void regmap_debugfs_free_dump_cache(struct regmap *map)
{
	struct regmap_debugfs_off_cache *c;

	while (!list_empty(&map->debugfs_off_cache)) {
		c = list_first_entry(&map->debugfs_off_cache,
				     struct regmap_debugfs_off_cache,
				     list);
		list_del(&c->list);
		kfree(c);
	}
}

/*
 * Work out where the start offset maps into register numbers, bearing
 * in mind that we suppress hidden registers.
 */
static unsigned int regmap_debugfs_get_dump_start(struct regmap *map,
						  unsigned int base,
						  loff_t from,
						  loff_t *pos)
{
	struct regmap_debugfs_off_cache *c = NULL;
	loff_t p = 0;
	unsigned int i, ret;

	/*
	 * If we don't have a cache build one so we don't have to do a
	 * linear scan each time.
	 */
	if (list_empty(&map->debugfs_off_cache)) {
		for (i = base; i <= map->max_register; i += map->reg_stride) {
			/* Skip unprinted registers, closing off cache entry */
			if (!regmap_readable(map, i) ||
			    regmap_precious(map, i)) {
				if (c) {
					c->max = p - 1;
					list_add_tail(&c->list,
						      &map->debugfs_off_cache);
					c = NULL;
				}

				continue;
			}

			/* No cache entry?  Start a new one */
			if (!c) {
				c = kzalloc(sizeof(*c), GFP_KERNEL);
				if (!c) {
					regmap_debugfs_free_dump_cache(map);
					return base;
				}
				c->min = p;
				c->base_reg = i;
			}

			p += map->debugfs_tot_len;
		}
	}

	/* Close the last entry off if we didn't scan beyond it */
	if (c) {
		c->max = p - 1;
		list_add_tail(&c->list,
			      &map->debugfs_off_cache);
	}

	/*
	 * This should never happen; we return above if we fail to
	 * allocate and we should never be in this code if there are
	 * no registers at all.
	 */
	WARN_ON(list_empty(&map->debugfs_off_cache));
	ret = base;

	/* Find the relevant block */
	list_for_each_entry(c, &map->debugfs_off_cache, list) {
		if (from >= c->min && from <= c->max) {
			*pos = c->min;
			return c->base_reg;
		}

		*pos = c->min;
		ret = c->base_reg;
	}

	return ret;
}

static ssize_t regmap_read_debugfs(struct regmap *map, unsigned int from,
				   unsigned int to, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	size_t buf_pos = 0;
	loff_t p = *ppos;
	ssize_t ret;
	int i;
	char *buf;
	unsigned int val, start_reg;

	if (*ppos < 0 || !count)
		return -EINVAL;

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Calculate the length of a fixed format  */
	if (!map->debugfs_tot_len) {
		map->debugfs_reg_len = regmap_calc_reg_len(map->max_register,
							   buf, count);
		map->debugfs_val_len = 2 * map->format.val_bytes;
		map->debugfs_tot_len = map->debugfs_reg_len +
			map->debugfs_val_len + 3;      /* : \n */
	}

	/* Work out which register we're starting at */
	start_reg = regmap_debugfs_get_dump_start(map, from, *ppos, &p);

	for (i = start_reg; i <= to; i += map->reg_stride) {
		if (!regmap_readable(map, i))
			continue;

		if (regmap_precious(map, i))
			continue;

		/* If we're in the region the user is trying to read */
		if (p >= *ppos) {
			/* ...but not beyond it */
			if (buf_pos + 1 + map->debugfs_tot_len >= count)
				break;

			/* Format the register */
			snprintf(buf + buf_pos, count - buf_pos, "%.*x: ",
				 map->debugfs_reg_len, i - from);
			buf_pos += map->debugfs_reg_len + 2;

			/* Format the value, write all X if we can't read */
			ret = regmap_read(map, i, &val);
			if (ret == 0)
				snprintf(buf + buf_pos, count - buf_pos,
					 "%.*x", map->debugfs_val_len, val);
			else
				memset(buf + buf_pos, 'X',
				       map->debugfs_val_len);
			buf_pos += 2 * map->format.val_bytes;

			buf[buf_pos++] = '\n';
		}
		p += map->debugfs_tot_len;
	}

	ret = buf_pos;

	if (copy_to_user(user_buf, buf, buf_pos)) {
		ret = -EFAULT;
		goto out;
	}

	*ppos += buf_pos;

out:
	kfree(buf);
	return ret;
}

static ssize_t regmap_map_read_file(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct regmap *map = file->private_data;

	return regmap_read_debugfs(map, 0, map->max_register, user_buf,
				   count, ppos);
}

#undef REGMAP_ALLOW_WRITE_DEBUGFS
#ifdef REGMAP_ALLOW_WRITE_DEBUGFS
/*
 * This can be dangerous especially when we have clients such as
 * PMICs, therefore don't provide any real compile time configuration option
 * for this feature, people who want to use this will need to modify
 * the source code directly.
 */
static ssize_t regmap_map_write_file(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	char buf[32];
	size_t buf_size;
	char *start = buf;
	unsigned long reg, value;
	struct regmap *map = file->private_data;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	while (*start == ' ')
		start++;
	reg = simple_strtoul(start, &start, 16);
	while (*start == ' ')
		start++;
	if (strict_strtoul(start, 16, &value))
		return -EINVAL;

	/* Userspace has been fiddling around behind the kernel's back */
	add_taint(TAINT_USER);

	regmap_write(map, reg, value);
	return buf_size;
}
#else
#define regmap_map_write_file NULL
#endif

static const struct file_operations regmap_map_fops = {
	.open = simple_open,
	.read = regmap_map_read_file,
	.write = regmap_map_write_file,
	.llseek = default_llseek,
};

static ssize_t regmap_range_read_file(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct regmap_range_node *range = file->private_data;
	struct regmap *map = range->map;

	return regmap_read_debugfs(map, range->range_min, range->range_max,
				   user_buf, count, ppos);
}

static const struct file_operations regmap_range_fops = {
	.open = simple_open,
	.read = regmap_range_read_file,
	.llseek = default_llseek,
};

static ssize_t regmap_access_read_file(struct file *file,
				       char __user *user_buf, size_t count,
				       loff_t *ppos)
{
	int reg_len, tot_len;
	size_t buf_pos = 0;
	loff_t p = 0;
	ssize_t ret;
	int i;
	struct regmap *map = file->private_data;
	char *buf;

	if (*ppos < 0 || !count)
		return -EINVAL;

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Calculate the length of a fixed format  */
	reg_len = regmap_calc_reg_len(map->max_register, buf, count);
	tot_len = reg_len + 10; /* ': R W V P\n' */

	for (i = 0; i <= map->max_register; i += map->reg_stride) {
		/* Ignore registers which are neither readable nor writable */
		if (!regmap_readable(map, i) && !regmap_writeable(map, i))
			continue;

		/* If we're in the region the user is trying to read */
		if (p >= *ppos) {
			/* ...but not beyond it */
			if (buf_pos + tot_len + 1 >= count)
				break;

			/* Format the register */
			snprintf(buf + buf_pos, count - buf_pos,
				 "%.*x: %c %c %c %c\n",
				 reg_len, i,
				 regmap_readable(map, i) ? 'y' : 'n',
				 regmap_writeable(map, i) ? 'y' : 'n',
				 regmap_volatile(map, i) ? 'y' : 'n',
				 regmap_precious(map, i) ? 'y' : 'n');

			buf_pos += tot_len;
		}
		p += tot_len;
	}

	ret = buf_pos;

	if (copy_to_user(user_buf, buf, buf_pos)) {
		ret = -EFAULT;
		goto out;
	}

	*ppos += buf_pos;

out:
	kfree(buf);
	return ret;
}

static const struct file_operations regmap_access_fops = {
	.open = simple_open,
	.read = regmap_access_read_file,
	.llseek = default_llseek,
};

void regmap_debugfs_init(struct regmap *map, const char *name)
{
	struct rb_node *next;
	struct regmap_range_node *range_node;
	const char *devname = "dummy";

	if (map->dev)
		devname = dev_name(map->dev);

	INIT_LIST_HEAD(&map->debugfs_off_cache);

	if (name) {
		map->debugfs_name = kasprintf(GFP_KERNEL, "%s-%s",
					      dev_name(map->dev), name);
		name = map->debugfs_name;
	} else {
		name = dev_name(map->dev);
	}

	map->debugfs = debugfs_create_dir(name, regmap_debugfs_root);
	if (!map->debugfs) {
		dev_warn(map->dev, "Failed to create debugfs directory\n");
		return;
	}

	debugfs_create_file("name", 0400, map->debugfs,
			    map, &regmap_name_fops);

	if (map->max_register) {
		umode_t registers_mode;

		if (IS_ENABLED(REGMAP_ALLOW_WRITE_DEBUGFS))
			registers_mode = 0600;
		else
			registers_mode = 0400;

		debugfs_create_file("registers", registers_mode, map->debugfs,
				    map, &regmap_map_fops);
		debugfs_create_file("access", 0400, map->debugfs,
				    map, &regmap_access_fops);
	}

	if (map->cache_type) {
		debugfs_create_bool("cache_only", 0400, map->debugfs,
				    &map->cache_only);
		debugfs_create_bool("cache_dirty", 0400, map->debugfs,
				    &map->cache_dirty);
		debugfs_create_bool("cache_bypass", 0400, map->debugfs,
				    &map->cache_bypass);
	}

	next = rb_first(&map->range_tree);
	while (next) {
		range_node = rb_entry(next, struct regmap_range_node, node);

		if (range_node->name)
			debugfs_create_file(range_node->name, 0400,
					    map->debugfs, range_node,
					    &regmap_range_fops);

		next = rb_next(&range_node->node);
	}
}

void regmap_debugfs_exit(struct regmap *map)
{
	debugfs_remove_recursive(map->debugfs);
	regmap_debugfs_free_dump_cache(map);
	kfree(map->debugfs_name);
}

void regmap_debugfs_initcall(void)
{
	regmap_debugfs_root = debugfs_create_dir("regmap", NULL);
	if (!regmap_debugfs_root) {
		pr_warn("regmap: Failed to create debugfs root\n");
		return;
	}
}
