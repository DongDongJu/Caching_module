#include <linux/bcc.h>


extern struct bc bc_set[MAX_NUM_SECS];
extern volatile int num_of_sector;
extern volatile int load_bio_flag;
dev_t dev = 0;
static struct class *dev_class;
static struct cdev bbc_cdev;
struct kobject *kobj_ref;

static int __init bbc_driver_init(void);
static void __exit bbc_driver_exit(void);

// driver functions

static int bbc_open(struct inode *inode, struct file *file);
static int bbc_release(struct inode *inode, struct file * file);
static ssize_t bbc_read(struct file *filp, char __user *buf, size_t len, loff_t * off);
static ssize_t bbc_write(struct file *filp, const char *buf, size_t len, loff_t * off);

// Sysfs functions
static ssize_t sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t sysfs_store(struct kobject *kobj, struct kobj_attribute *attr, char *buf, size_t count);
void store_sector_and_size(unsigned long target_sector,int size);

static ssize_t free_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t free_store(struct kobject *kobj, struct kobj_attribute *attr, char *buf, size_t count);
void free_buffer(void);

static ssize_t load_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t load_store(struct kobject *kobj, struct kobj_attribute *attr, char *buf, size_t count);
int load_bio(unsigned long target_sector,int target_size, void* data);
extern void submit_bio_wait_endio(struct bio *bio);
struct device *find_dev(const char *name);

struct kobj_attribute bbc_attr = __ATTR(insert, 0660, sysfs_show, sysfs_store);
struct kobj_attribute bbc_free_attr = __ATTR(free, 0660,free_show, free_store);
struct kobj_attribute bbc_load_attr = __ATTR(load_buffer, 0660,load_show,load_store);

static struct file_operations fops =
{
    .owner          = THIS_MODULE,
    .read           = bbc_read,
    .write          = bbc_write,
    .open           = bbc_open,
    .release        = bbc_release,
};

static ssize_t sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    int i = 0;
    for(i=0;i<num_of_sector;i++)
        printk(KERN_INFO"sectors = %lu size = %d\n",bc_set[i].sector,bc_set[i].size);
    return sprintf(buf, "end\n");
}

static ssize_t sysfs_store(struct kobject *kobj, struct kobj_attribute *attr, char *buf, size_t count)
{
    unsigned long target_sector;
    int size;
    sscanf(buf,"%lu %d",&target_sector,&size);
    store_sector_and_size(target_sector,size);
    return count;
}
void store_sector_and_size(unsigned long target_sector,int size)
{
    //TODO : 맥스 값에 대한 예외처리 해줘야함
    bc_set[num_of_sector].sector = target_sector;
    bc_set[num_of_sector].size   = size;
    num_of_sector++;
}

static ssize_t free_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf,"Can't read this file\n");
}
static ssize_t free_store(struct kobject *kobj, struct kobj_attribute *attr, char *buf, size_t count)
{
    free_buffer();
    return count;
}
void free_buffer(void)
{
    int i = 0;
    for(i=0;i<MAX_NUM_SECS;i++)
    {
        bc_set[i].sector=-1;
        bc_set[i].size=-1;
    }
    num_of_sector=0;
}

static ssize_t load_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    int i = 0;
    for(i=0;i<num_of_sector;i++)
        printk(KERN_INFO"sectors = %lu size = %d data = %s\n",bc_set[i].sector,bc_set[i].size,bc_set[i].data);
    return sprintf(buf, "end\n");
}
static ssize_t load_store(struct kobject *kobj, struct kobj_attribute *attr, char *buf, size_t count)
{
    int i;
    load_bio_flag=1;
    for(i=0;i<num_of_sector;i++)
        load_bio(bc_set[i].sector,bc_set[i].size,bc_set[i].data);
    load_bio_flag=0;
    return count;
}
struct submit_bio_ret{
    struct completion event;
    int error;
};
static void custom_complete(struct bio *bio)
{
    struct submit_bio_ret *ret = bio->bi_private;
    ret->error = bio->bi_error;
    complete(&ret->event);
}
int load_bio(unsigned long target_sector,int target_size, void* data)
{
    struct bio bio;
    struct bio_vec bio_vec;
    struct page *page;

    printk("%s\n", __FUNCTION__);

    page = alloc_page(GFP_TEMPORARY);
    kmap(page);
    if (!page) {
        printk(KERN_INFO"failed: alloc page\n");
        goto error_page;
    }
    else
        printk(KERN_INFO"success: alloc page\n");

    bio_init(&bio);
    bio.bi_bdev = blkdev_get_by_path("/dev/sdc1", FMODE_READ | FMODE_WRITE, NULL);
    if(IS_ERR(bio.bi_bdev))
        printk(KERN_INFO"error: %l\n", PTR_ERR(bio.bi_bdev));
    else
        printk(KERN_INFO"success: finding bi_bdev\n");

    bio.bi_max_vecs = 1;

    // do things done in bio_add_page()
    bio.bi_io_vec = &bio_vec;
    bio_vec.bv_page = page;
    bio_vec.bv_len = target_size << 9;
    bio_vec.bv_offset = 0;
    bio.bi_vcnt = 1;

    // manual setting
    bio.bi_iter.bi_sector = target_sector - bio.bi_bdev->bd_part->start_sect;
    bio.bi_iter.bi_size = target_size << 9;

    submit_bio_wait(READ, &bio);

    printk(KERN_INFO"%s\n", page_address(page) );

//    data = kmalloc(GFP_DMA,target_size);
//    if(!data)
//        printk("error\n");
//    else
//	    memcpy(data,page->virtual,target_size);
//    data = kmemdup(page->virtual,target_size<<9,GFP_DMA);
//    data = &page->virtual;
    memcpy(data,page->virtual,4096);
    printk(KERN_INFO"%s\n", data );
    printk(KERN_INFO"pointer page : %d data %d\n",page->virtual,data);
    return 0;

error_page:
    __free_page(page);
    return -1;
}

static int bbc_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "Device File Opened...!!!\n");
    return 0;
}

static int bbc_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "Device File Closed...!!!\n");
    return 0;
}

static ssize_t bbc_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    printk(KERN_INFO "Read function\n");
    return 0;
}
static ssize_t bbc_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
    printk(KERN_INFO "Write Function\n");
    return 0;
}


static int __init bbc_driver_init(void)
{
    /*Allocating Major number*/
    if((alloc_chrdev_region(&dev, 0, 1, "bbc_Dev")) <0){
        printk(KERN_INFO "Cannot allocate major number\n");
        return -1;
    }
    printk(KERN_INFO "Major = %d Minor = %d \n",MAJOR(dev), MINOR(dev));

    /*Creating cdev structure*/
    cdev_init(&bbc_cdev,&fops);
    bbc_cdev.owner = THIS_MODULE;
    bbc_cdev.ops = &fops;

    /*Adding character device to the system*/
    if((cdev_add(&bbc_cdev,dev,1)) < 0){
        printk(KERN_INFO "Cannot add the device to the system\n");
        goto r_class;
    }

    /*Creating struct class*/
    if((dev_class = class_create(THIS_MODULE,"bbc_class")) == NULL){
        printk(KERN_INFO "Cannot create the struct class\n");
        goto r_class;
    }

    /*Creating device*/
    if((device_create(dev_class,NULL,dev,NULL,"bbc_device")) == NULL){
        printk(KERN_INFO "Cannot create the Device 1\n");
        goto r_device;
    }

    /*Creating a directory in /sys/kernel/ */
    kobj_ref = kobject_create_and_add("bbc",kernel_kobj);

    if(sysfs_create_file(kobj_ref,&bbc_attr.attr)){
        printk(KERN_INFO"Cannot create sysfs file......\n");
        goto r_sysfs;
    }
    if(sysfs_create_file(kobj_ref,&bbc_free_attr.attr)){
        printk(KERN_INFO"Cannot create sysfs file......\n");
        goto r_sysfs;
    }
    if(sysfs_create_file(kobj_ref,&bbc_load_attr.attr)){
        printk(KERN_INFO"Cannot create sysfs file......\n");
        goto r_sysfs;
    }
    printk(KERN_INFO "Device Driver Insert...Done!!!\n");
    return 0;

r_sysfs:
    kobject_put(kobj_ref);
    sysfs_remove_file(kernel_kobj, &bbc_attr.attr);
    sysfs_remove_file(kernel_kobj, &bbc_free_attr.attr);
    sysfs_remove_file(kernel_kobj, &bbc_load_attr.attr);
r_device:
    class_destroy(dev_class);
r_class:
    unregister_chrdev_region(dev,1);
    cdev_del(&bbc_cdev);
    return -1;
}

void __exit bbc_driver_exit(void)
{
    kobject_put(kobj_ref);
    sysfs_remove_file(kernel_kobj, &bbc_attr.attr);
    sysfs_remove_file(kernel_kobj, &bbc_free_attr.attr);
    sysfs_remove_file(kernel_kobj, &bbc_load_attr.attr);
    device_destroy(dev_class,dev);
    class_destroy(dev_class);
    cdev_del(&bbc_cdev);
    unregister_chrdev_region(dev, 1);
    printk(KERN_INFO "Device Driver Remove...Done!!!\n");
}

module_init(bbc_driver_init);
module_exit(bbc_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("DongDongJu <commisori28@gmail.com>");
MODULE_DESCRIPTION("block cache controller");
MODULE_VERSION("0.1");
