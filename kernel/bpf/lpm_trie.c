/*
 * Minimal BPF LPM trie map support for the Samsung 4.4.177 BPF core.
 *
 * Android 16 netd currently defines a LocalNetAccess LPM_TRIE map.  The
 * universal8895 LOS23.2 reference carries the full upstream trie, but pulling
 * the whole modern BPF stack into this 4.4 tree is too invasive.  This file
 * provides the userspace-visible semantics needed by the legacy BPF syscall
 * layer: create, update, longest-prefix lookup, delete, and key iteration.
 */
#include <linux/bpf.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

struct lpm_trie_elem {
	struct list_head list;
	struct rcu_head rcu;
	u32 prefixlen;
	u8 data[0];
};

struct lpm_trie {
	struct bpf_map map;
	struct list_head elems;
	u32 n_entries;
	u32 max_prefixlen;
	u32 data_size;
	raw_spinlock_t lock;
};

#define LPM_DATA_SIZE_MAX	256
#define LPM_DATA_SIZE_MIN	1
#define LPM_VAL_SIZE_MIN	1
#define LPM_KEY_SIZE(X)		(sizeof(struct bpf_lpm_trie_key) + (X))
#define LPM_KEY_SIZE_MAX	LPM_KEY_SIZE(LPM_DATA_SIZE_MAX)
#define LPM_KEY_SIZE_MIN	LPM_KEY_SIZE(LPM_DATA_SIZE_MIN)
#define LPM_CREATE_FLAG_MASK	(BPF_F_NO_PREALLOC | BPF_F_RDONLY | \
				 BPF_F_WRONLY | BPF_F_RDONLY_PROG | \
				 BPF_F_WRONLY_PROG)

static u32 lpm_elem_size(const struct lpm_trie *trie)
{
	return sizeof(struct lpm_trie_elem) + trie->data_size +
		trie->map.value_size;
}

static void *lpm_elem_value(const struct lpm_trie *trie,
			    struct lpm_trie_elem *elem)
{
	return elem->data + trie->data_size;
}

static bool lpm_key_equal(const struct lpm_trie *trie,
			  const struct lpm_trie_elem *elem,
			  const struct bpf_lpm_trie_key *key)
{
	return elem->prefixlen == key->prefixlen &&
		!memcmp(elem->data, key->data, trie->data_size);
}

static bool lpm_prefix_matches(const struct lpm_trie *trie,
			       const struct lpm_trie_elem *elem,
			       const struct bpf_lpm_trie_key *key)
{
	u32 bytes = elem->prefixlen / 8;
	u32 bits = elem->prefixlen % 8;
	u8 mask;

	if (elem->prefixlen > key->prefixlen ||
	    elem->prefixlen > trie->max_prefixlen)
		return false;

	if (bytes && memcmp(elem->data, key->data, bytes))
		return false;

	if (!bits)
		return true;

	mask = 0xff << (8 - bits);
	return (elem->data[bytes] & mask) == (key->data[bytes] & mask);
}

static void *trie_lookup_elem(struct bpf_map *map, void *_key)
{
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct bpf_lpm_trie_key *key = _key;
	struct lpm_trie_elem *elem, *best = NULL;

	if (key->prefixlen > trie->max_prefixlen)
		return NULL;

	list_for_each_entry_rcu(elem, &trie->elems, list) {
		if (!lpm_prefix_matches(trie, elem, key))
			continue;
		if (!best || elem->prefixlen > best->prefixlen)
			best = elem;
	}

	return best ? lpm_elem_value(trie, best) : NULL;
}

static int trie_update_elem(struct bpf_map *map, void *_key, void *value,
			    u64 flags)
{
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct bpf_lpm_trie_key *key = _key;
	struct lpm_trie_elem *elem, *new_elem = NULL;
	unsigned long irq_flags;
	int ret = 0;

	if (flags > BPF_EXIST || key->prefixlen > trie->max_prefixlen)
		return -EINVAL;

	new_elem = kmalloc(lpm_elem_size(trie), GFP_ATOMIC | __GFP_NOWARN);
	if (!new_elem)
		return -ENOMEM;

	new_elem->prefixlen = key->prefixlen;
	memcpy(new_elem->data, key->data, trie->data_size);
	memcpy(lpm_elem_value(trie, new_elem), value, trie->map.value_size);

	raw_spin_lock_irqsave(&trie->lock, irq_flags);

	list_for_each_entry(elem, &trie->elems, list) {
		if (!lpm_key_equal(trie, elem, key))
			continue;
		if (flags == BPF_NOEXIST) {
			ret = -EEXIST;
			goto out;
		}
		list_replace_rcu(&elem->list, &new_elem->list);
		kfree_rcu(elem, rcu);
		new_elem = NULL;
		goto out;
	}

	if (flags == BPF_EXIST) {
		ret = -ENOENT;
		goto out;
	}

	if (trie->n_entries >= trie->map.max_entries) {
		ret = -ENOSPC;
		goto out;
	}

	list_add_tail_rcu(&new_elem->list, &trie->elems);
	trie->n_entries++;
	new_elem = NULL;

out:
	raw_spin_unlock_irqrestore(&trie->lock, irq_flags);
	kfree(new_elem);
	return ret;
}

static int trie_delete_elem(struct bpf_map *map, void *_key)
{
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct bpf_lpm_trie_key *key = _key;
	struct lpm_trie_elem *elem;
	unsigned long irq_flags;
	int ret = -ENOENT;

	if (key->prefixlen > trie->max_prefixlen)
		return -EINVAL;

	raw_spin_lock_irqsave(&trie->lock, irq_flags);

	list_for_each_entry(elem, &trie->elems, list) {
		if (!lpm_key_equal(trie, elem, key))
			continue;
		list_del_rcu(&elem->list);
		kfree_rcu(elem, rcu);
		trie->n_entries--;
		ret = 0;
		break;
	}

	raw_spin_unlock_irqrestore(&trie->lock, irq_flags);
	return ret;
}

static int trie_get_next_key(struct bpf_map *map, void *_key, void *_next_key)
{
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct bpf_lpm_trie_key *key = _key, *next_key = _next_key;
	struct lpm_trie_elem *elem;
	bool return_next = !key;

	list_for_each_entry_rcu(elem, &trie->elems, list) {
		if (return_next)
			goto copy_key;
		if (lpm_key_equal(trie, elem, key))
			return_next = true;
	}

	return -ENOENT;

copy_key:
	next_key->prefixlen = elem->prefixlen;
	memcpy(next_key->data, elem->data, trie->data_size);
	return 0;
}

static struct bpf_map *trie_alloc(union bpf_attr *attr)
{
	struct lpm_trie *trie;
	u64 cost, cost_per_node;
	int ret;

	if (attr->max_entries == 0 ||
	    !(attr->map_flags & BPF_F_NO_PREALLOC) ||
	    (attr->map_flags & ~LPM_CREATE_FLAG_MASK) ||
	    attr->key_size < LPM_KEY_SIZE_MIN ||
	    attr->key_size > LPM_KEY_SIZE_MAX ||
	    attr->value_size < LPM_VAL_SIZE_MIN ||
	    attr->value_size > KMALLOC_MAX_SIZE - LPM_DATA_SIZE_MAX -
		sizeof(struct lpm_trie_elem))
		return ERR_PTR(-EINVAL);

	trie = kzalloc(sizeof(*trie), GFP_USER | __GFP_NOWARN);
	if (!trie)
		return ERR_PTR(-ENOMEM);

	trie->map.map_type = attr->map_type;
	trie->map.key_size = attr->key_size;
	trie->map.value_size = attr->value_size;
	trie->map.max_entries = attr->max_entries;
	trie->map.map_flags = attr->map_flags;
	trie->data_size = attr->key_size -
		offsetof(struct bpf_lpm_trie_key, data);
	trie->max_prefixlen = trie->data_size * 8;

	cost_per_node = sizeof(struct lpm_trie_elem) + trie->data_size +
		attr->value_size;
	cost = sizeof(*trie) + (u64)attr->max_entries * cost_per_node;
	if (cost >= U32_MAX - PAGE_SIZE) {
		ret = -E2BIG;
		goto out_err;
	}

	trie->map.pages = round_up(cost, PAGE_SIZE) >> PAGE_SHIFT;
	ret = bpf_map_precharge_memlock(trie->map.pages);
	if (ret)
		goto out_err;

	INIT_LIST_HEAD(&trie->elems);
	raw_spin_lock_init(&trie->lock);

	return &trie->map;

out_err:
	kfree(trie);
	return ERR_PTR(ret);
}

static void trie_free(struct bpf_map *map)
{
	struct lpm_trie *trie = container_of(map, struct lpm_trie, map);
	struct lpm_trie_elem *elem, *tmp;

	synchronize_rcu();

	list_for_each_entry_safe(elem, tmp, &trie->elems, list) {
		list_del(&elem->list);
		kfree(elem);
	}

	kfree(trie);
}

static const struct bpf_map_ops trie_ops = {
	.map_alloc = trie_alloc,
	.map_free = trie_free,
	.map_get_next_key = trie_get_next_key,
	.map_lookup_elem = trie_lookup_elem,
	.map_update_elem = trie_update_elem,
	.map_delete_elem = trie_delete_elem,
};

static struct bpf_map_type_list trie_type __read_mostly = {
	.ops = &trie_ops,
	.type = BPF_MAP_TYPE_LPM_TRIE,
};

static int __init register_trie_map(void)
{
	bpf_register_map_type(&trie_type);
	return 0;
}
late_initcall(register_trie_map);
