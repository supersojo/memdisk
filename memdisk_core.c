#include <linux/init.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>

/* memdisk device */
struct memdisk_dev{
	struct gendisk* disk;
	int blksize;
	int blkshift;

	struct request_queue* rq;
	spinlock_t queue_lock;

	struct workqueue_struct* wq;
	struct work_struct work;
};

static struct memdisk_dev memdisk={0};

static void* memdisk_buf;
static int k_disk_size = 4*1024*1024;

/* block device ops */
static int memdisk_open(struct block_device* bdev,fmode_t mode)
{
	return 0;
}
static void memdisk_release(struct gendisk* disk,fmode_t mode)
{

}
static int memdisk_getgeo(struct block_device* bdev,struct hd_geometry*geo)
{
	return 0;
}
static int memdisk_ioctl(struct block_device* bdev,fmode_t mode,unsigned int cmd,unsigned long arg)
{
	return 0;
}
static const struct block_device_operations memdisk_ops = {
	.owner = THIS_MODULE,
	.open = memdisk_open,
	.release = memdisk_release,
	.ioctl = memdisk_ioctl,
	.getgeo = memdisk_getgeo,
};
/* handle memdisk io */
static void memdisk_request(struct request_queue* rq)
{
	struct memdisk_dev* dev;
	struct request* req=NULL;
	dev = rq->queuedata;
	if(!dev){
		while((req=blk_fetch_request(rq))!=NULL)
			__blk_end_request_all(req,-ENODEV);
	}
	else
		queue_work(dev->wq,&dev->work);
}
/* do memdisk io */
static int do_io(struct request* req)
{
	unsigned long block,nsect;
	char* buf;

	printk("memdisk io\n");

	block = blk_rq_pos(req)<<9>>memdisk.blkshift;
	nsect = blk_rq_cur_bytes(req)>>memdisk.blkshift;
	buf = bio_data(req->bio);

	switch(rq_data_dir(req)){
	case READ:
		for(;nsect>0;nsect--,block++,buf+=memdisk.blksize)
			memcpy(buf,memdisk_buf+block*memdisk.blksize,memdisk.blksize);
		return 0;
	case WRITE:
		for(;nsect>0;nsect--,block++,buf+=memdisk.blksize)
			memcpy(memdisk_buf+block*memdisk.blksize,buf,memdisk.blksize);
		return 0;
		
	}
	return 0;
}
/* workqueue to handle io */
static void memdisk_work(struct work_struct * work)
{
	struct memdisk_dev* dev = container_of(work,struct memdisk_dev,work);
	struct request* req=NULL;

	spin_lock_irq(dev->rq->queue_lock);
	while(1){
		int res;
		// no work to do
		if(!req&&!(req=blk_fetch_request(dev->rq)))
			break;	
		spin_unlock_irq(dev->rq->queue_lock);
		//do real io
		res = do_io(req);
		spin_lock_irq(dev->rq->queue_lock);
		if(!__blk_end_request_cur(req,res))
			req=NULL;	
	}
	spin_unlock_irq(dev->rq->queue_lock);

}

/* alloc space for memdisk by vmalloc
  kmalloc has 128K limits so use vmalloc.
*/
static int alloc_memdisk(int disk_size)
{
	if(disk_size%PAGE_SIZE!=0){
		printk("disk size should be page size aligned\n");
		return -1;
	}
	memdisk_buf = vmalloc(disk_size);
	printk("%p\n",memdisk_buf);
	return 0;
}
static void free_memdisk(void)
{
	if(memdisk_buf)
		vfree(memdisk_buf);
	memdisk_buf = 0;
}

static int memdisk_init(void)
{
	struct gendisk* gd;
	printk("memdisk_init\n");
	alloc_memdisk(k_disk_size);
	memdisk.blksize = 512;//sector size
	memdisk.blkshift = 9;
	gd = alloc_disk(1);
	memdisk.disk = gd;
	gd->private_data = &memdisk;
	gd->major = 111;
	gd->first_minor = 0;
	gd->minors = 16;
	gd->fops = &memdisk_ops;
  	snprintf(gd->disk_name,sizeof(gd->disk_name),"memdisk");
	set_capacity(gd,k_disk_size>>9);//sectors
	
	memdisk.wq = alloc_workqueue("%s%d",0,0,"memdisk_wq",0);
	INIT_WORK(&memdisk.work,memdisk_work);

	/* request queue */
	spin_lock_init(&memdisk.queue_lock);
	memdisk.rq = blk_init_queue(memdisk_request,&memdisk.queue_lock);	
	memdisk.rq->queuedata = &memdisk;	
	blk_queue_logical_block_size(memdisk.rq,memdisk.blksize);
	gd->queue = memdisk.rq;
	
	add_disk(gd);

	return 0;
}
static void memdisk_exit(void)
{
	unsigned long flags;
	del_gendisk(memdisk.disk);

	spin_lock_irqsave(&memdisk.queue_lock,flags);
	memdisk.rq->queuedata = NULL;
	blk_start_queue(memdisk.rq);
	spin_unlock_irqrestore(&memdisk.queue_lock,flags);

	free_memdisk();
	printk("memdisk_exit\n");
}

module_init(memdisk_init);
module_exit(memdisk_exit);
MODULE_LICENSE("GPL");
