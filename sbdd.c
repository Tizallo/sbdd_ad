#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/bio.h>
#include <linux/bvec.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/numa.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/moduleparam.h>
#include <linux/spinlock_types.h>

#define SBDD_SECTOR_SHIFT       9
#define SBDD_SECTOR_SIZE        (1 << SBDD_SECTOR_SHIFT)
#define SBDD_NAME               "sbdd"

struct bio_context {
    struct bio *orig_bio;
    atomic_t completions_remaining;
    blk_status_t status;
};

// https://elixir.bootlin.com/linux/v6.8.8/source/include/linux/blk_types.h#L264
struct sbdd {
    struct bdev_handle     *bdev_handle1;    
    struct block_device    *backing_bdev1;   
    struct bdev_handle     *bdev_handle2;    
    struct block_device    *backing_bdev2;   
    struct gendisk         *gd;             
    sector_t               capacity;        
    atomic_t               refs_cnt;        
    atomic_t               deleting;        
    wait_queue_head_t      exitwait;        
};

static struct sbdd __sbdd;
static char *sbdd_backing_dev1 = NULL;
static char *sbdd_backing_dev2 = NULL;

module_param_named(backing_dev1, sbdd_backing_dev1, charp, 0644);
MODULE_PARM_DESC(backing_dev1, "Path to first backing block device");
module_param_named(backing_dev2, sbdd_backing_dev2, charp, 0644);
MODULE_PARM_DESC(backing_dev2, "Path to second backing block device");

static void sbdd_bio_endio(struct bio *bio)
{
    struct bio_context *ctx = bio->bi_private;
    blk_status_t status = bio->bi_status;

    if (status != BLK_STS_OK && ctx->status == BLK_STS_OK)
        ctx->status = status;

    if (atomic_dec_and_test(&ctx->completions_remaining)) {
        ctx->orig_bio->bi_status = ctx->status;
        bio_endio(ctx->orig_bio);
        kfree(ctx);
    }

    if (atomic_dec_and_test(&__sbdd.refs_cnt))
        wake_up(&__sbdd.exitwait);

    bio_put(bio);
}

static void sbdd_submit_bio(struct bio *bio)
{
    struct bio *clone1 = NULL, *clone2 = NULL;
    struct bio_context *ctx;

    if (atomic_read(&__sbdd.deleting)) {
        bio_io_error(bio);
        return;
    }

    ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx) {
        bio_io_error(bio);
        return;
    }

    ctx->orig_bio = bio;
    atomic_set(&ctx->completions_remaining, 2);
    ctx->status = BLK_STS_OK;

    // https://elixir.bootlin.com/linux/v6.8.8/source/block/bio.c#L833
    clone1 = bio_alloc_clone(__sbdd.backing_bdev1, bio, GFP_KERNEL, &fs_bio_set);
    if (!clone1) goto error;

    clone2 = bio_alloc_clone(__sbdd.backing_bdev2, bio, GFP_KERNEL, &fs_bio_set);
    if (!clone2) goto error;

    clone1->bi_end_io = sbdd_bio_endio;
    clone1->bi_private = ctx;
    clone1->bi_opf = bio->bi_opf;

    clone2->bi_end_io = sbdd_bio_endio;
    clone2->bi_private = ctx;
    clone2->bi_opf = bio->bi_opf;

    atomic_add(2, &__sbdd.refs_cnt);
    submit_bio(clone1);
    submit_bio(clone2);
    return;

error:
    if (clone1) bio_put(clone1);
    if (clone2) bio_put(clone2);
    kfree(ctx);
    bio_io_error(bio);
}

/*
There are no read or write operations. These operations are performed by
the request() function associated with the request queue of the disk.
*/

static const struct block_device_operations sbdd_bdev_ops = {
    .owner = THIS_MODULE,
    .submit_bio = sbdd_submit_bio,
};

static int sbdd_create(void) {
    int ret = 0;
    blk_mode_t mode = BLK_OPEN_READ | BLK_OPEN_WRITE;
    sector_t cap1, cap2;

    if (!sbdd_backing_dev1 || !sbdd_backing_dev2) {
        pr_err("Both backing devices are required!\n");
        return -EINVAL;
    }
    // open block device
    __sbdd.bdev_handle1 = bdev_open_by_path(sbdd_backing_dev1, mode, THIS_MODULE, NULL);
    if (IS_ERR(__sbdd.bdev_handle1)) {
        ret = PTR_ERR(__sbdd.bdev_handle1);
        pr_err("Failed to open %s: %d\n", sbdd_backing_dev1, ret);
        return ret;
    }
    __sbdd.backing_bdev1 = __sbdd.bdev_handle1->bdev;

    // open block device
    __sbdd.bdev_handle2 = bdev_open_by_path(sbdd_backing_dev2, mode, THIS_MODULE, NULL);
    if (IS_ERR(__sbdd.bdev_handle2)) {
        ret = PTR_ERR(__sbdd.bdev_handle2);
        pr_err("Failed to open %s: %d\n", sbdd_backing_dev2, ret);
        bdev_release(__sbdd.bdev_handle1);
        return ret;
    }

    __sbdd.backing_bdev2 = __sbdd.bdev_handle2->bdev;

    cap1 = bdev_nr_sectors(__sbdd.backing_bdev1);
    cap2 = bdev_nr_sectors(__sbdd.backing_bdev2);
    __sbdd.capacity = min(cap1, cap2);

    // init gendisk
    __sbdd.gd = blk_alloc_disk(NUMA_NO_NODE);
    if (!__sbdd.gd) {
        ret = -ENOMEM;
        goto fail_release_bdevs;
    }

    // set queue
    blk_queue_logical_block_size(__sbdd.gd->queue, SBDD_SECTOR_SIZE);
    blk_queue_physical_block_size(__sbdd.gd->queue, SBDD_SECTOR_SIZE);

    __sbdd.gd->fops = &sbdd_bdev_ops;
    __sbdd.gd->private_data = &__sbdd;
    snprintf(__sbdd.gd->disk_name, DISK_NAME_LEN, SBDD_NAME);
    set_capacity(__sbdd.gd, __sbdd.capacity);

    atomic_set(&__sbdd.refs_cnt, 0);
    atomic_set(&__sbdd.deleting, 0);
    init_waitqueue_head(&__sbdd.exitwait);

    ret = add_disk(__sbdd.gd);
    if (ret)
        goto fail_put_disk;

    return 0;

fail_put_disk:
    put_disk(__sbdd.gd);
fail_release_bdevs:
    bdev_release(__sbdd.bdev_handle1);
    bdev_release(__sbdd.bdev_handle2);
    return ret;
}

static void sbdd_delete(void) {
    atomic_set(&__sbdd.deleting, 1);
    wait_event(__sbdd.exitwait, atomic_read(&__sbdd.refs_cnt) == 0);

    if (__sbdd.gd) {
        del_gendisk(__sbdd.gd);
        put_disk(__sbdd.gd);
    }
    bdev_release(__sbdd.bdev_handle1);
    bdev_release(__sbdd.bdev_handle2);
}

/*
Note __init is for the kernel to drop this function after
initialization complete making its memory available for other uses.
There is also __initdata note, same but used for variables.
*/

static int __init sbdd_init(void)
{
    return sbdd_create();
}

/*
Note __exit is for the compiler to place this code in a special ELF section.
Sometimes such functions are simply discarded (e.g. when module is built
directly into the kernel). There is also __exitdata note.
*/

static void __exit sbdd_exit(void)
{
    sbdd_delete();
}

/* Called on module loading. Is mandatory. */

module_init(sbdd_init);

/* Called on module unloading. Unloading module is not allowed without it. */

module_exit(sbdd_exit);

/* Note for the kernel: a free license module. A warning will be outputted without it. */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Dual Backing Block Device Driver");