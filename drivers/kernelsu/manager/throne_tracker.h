#ifndef __KSU_H_UID_OBSERVER
#define __KSU_H_UID_OBSERVER

#include <linux/cred.h>
#include <linux/types.h>
#ifdef CONFIG_KSU_DISABLE_MANAGER
static inline void ksu_throne_tracker_init()
{
}

static inline void ksu_throne_tracker_exit()
{
}

static inline void track_throne(bool prune_only)
{
    (void)prune_only;
}

static inline void ksu_throne_tracker_set_scan_cred(const struct cred *cred)
{
    (void)cred;
}

static inline bool track_throne_sync(bool prune_only)
{
    (void)prune_only;
    return true;
}
#else
void ksu_throne_tracker_init();

void ksu_throne_tracker_exit();

void track_throne(bool prune_only);

/* Capture credentials from a post-boot ksud process so encrypted /data
 * paths can be scanned from the delayed workqueue. */
void ksu_throne_tracker_set_scan_cred(const struct cred *cred);

/* Run one scan synchronously using the captured post-boot credentials. */
bool track_throne_sync(bool prune_only);
#endif

#endif
