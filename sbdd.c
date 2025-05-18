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
#define SBDD_MIB_SECTORS        (1 << (20 - SBDD_SECTOR_SHIFT))
#define SBDD_NAME               "sbdd"


// https://elixir.bootlin.com/linux/v6.8.8/source/include/linux/blk_types.h#L264
struct sbdd {
    struct bdev_handle     *bdev_handle;    
    struct block_device    *backing_bdev;   
    struct gendisk         *gd;             
    sector_t               capacity;        
    atomic_t               refs_cnt;        
    atomic_t               deleting;        
    wait_queue_head_t      exitwait;        
};

static struct sbdd __sbdd;
static char *sbdd_backing_dev = NULL;

module_param_named(backing_dev, sbdd_backing_dev, charp, 0644);
MODULE_PARM_DESC(backing_dev, "Path to backing block device");

static void sbdd_bio_endio(struct bio *bio)
{
    struct bio *orig_bio = bio->bi_private;

    orig_bio->bi_status = bio->bi_status;
    bio_endio(orig_bio);

    if (atomic_dec_and_test(&__sbdd.refs_cnt))
        wake_up(&__sbdd.exitwait);

    bio_put(bio);
}

static void sbdd_submit_bio(struct bio *bio)
{
    struct bio *clone_bio;

    if (atomic_read(&__sbdd.deleting)) {
        bio_io_error(bio);
        return;
    }

    atomic_inc(&__sbdd.refs_cnt);

    // https://elixir.bootlin.com/linux/v6.8.8/source/block/bio.c#L833
    clone_bio = bio_alloc_clone(__sbdd.backing_bdev, bio, GFP_KERNEL, &fs_bio_set);
    if (!clone_bio) {
        atomic_dec(&__sbdd.refs_cnt);
        bio_io_error(bio);
        return;
    }

    clone_bio->bi_end_io = sbdd_bio_endio;
    clone_bio->bi_private = bio;
    clone_bio->bi_opf = bio->bi_opf; 
    submit_bio(clone_bio);
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

    if (!sbdd_backing_dev) {
        pr_err("backing_dev parameter is required!\n");
        return -EINVAL;
    }

    // open block device
    __sbdd.bdev_handle = bdev_open_by_path(sbdd_backing_dev, mode, THIS_MODULE, NULL);
    if (IS_ERR(__sbdd.bdev_handle)) {
        ret = PTR_ERR(__sbdd.bdev_handle);
        pr_err("Failed to open %s: %d\n", sbdd_backing_dev, ret);
        return ret;
    }
    __sbdd.backing_bdev = __sbdd.bdev_handle->bdev;

    // set capacity
    __sbdd.capacity = bdev_nr_sectors(__sbdd.backing_bdev);

    // init gendisk
    __sbdd.gd = blk_alloc_disk(NUMA_NO_NODE);
    if (!__sbdd.gd) {
   		ret = PTR_ERR(__sbdd.gd);
   		__sbdd.gd = NULL;
        goto fail_release_bdev;
    }

    // set queue
    blk_queue_logical_block_size(__sbdd.gd->queue, SBDD_SECTOR_SIZE);
    blk_queue_physical_block_size(__sbdd.gd->queue, SBDD_SECTOR_SIZE);
  
    __sbdd.gd->fops = &sbdd_bdev_ops;
  
    __sbdd.gd->private_data = &__sbdd;
    snprintf(__sbdd.gd->disk_name, DISK_NAME_LEN, SBDD_NAME);
    set_capacity(__sbdd.gd, __sbdd.capacity);

    // init sync
    atomic_set(&__sbdd.refs_cnt, 0);
    atomic_set(&__sbdd.deleting, 0);
    init_waitqueue_head(&__sbdd.exitwait);

    // disk registration
    ret = add_disk(__sbdd.gd);
    if (ret) {
        pr_err("add_disk() failed\n");
        goto fail_put_disk;
    }

    return 0;

fail_put_disk:
    put_disk(__sbdd.gd);
fail_release_bdev:
    bdev_release(__sbdd.bdev_handle);
    return ret;
}

static void sbdd_delete(void) {
    atomic_set(&__sbdd.deleting, 1);
    wait_event(__sbdd.exitwait, atomic_read(&__sbdd.refs_cnt) == 0);

    if (__sbdd.gd) {
   		pr_info("deleting disk\n");
        del_gendisk(__sbdd.gd);
        put_disk(__sbdd.gd);
    }
    if (__sbdd.bdev_handle) {
        bdev_release(__sbdd.bdev_handle);
    }
}

/*
Note __init is for the kernel to drop this function after
initialization complete making its memory available for other uses.
There is also __initdata note, same but used for variables.
*/

static int __init sbdd_init(void)
{
    int ret = sbdd_create();
    if (ret)
        pr_err("Initialization failed\n");
    return ret;
}

/*
Note __exit is for the compiler to place this code in a special ELF section.
Sometimes such functions are simply discarded (e.g. when module is built
directly into the kernel). There is also __exitdata note.
*/

static void __exit sbdd_exit(void)
{
    pr_info("exiting...\n");
    sbdd_delete();
  	pr_info("exiting complete\n");


}

/* Called on module loading. Is mandatory. */

module_init(sbdd_init);

/* Called on module unloading. Unloading module is not allowed without it. */
module_exit(sbdd_exit);


/* Note for the kernel: a free license module. A warning will be outputted without it. */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simple Block Device Driver (Proxy)");
