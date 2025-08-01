/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/slab.h> // kmalloc
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("amasini0"); /** DONE: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;

    PDEBUG("open");
    /**
     * DONE: handle open
     */
    // Pass aesd_device to subsequent file operations.
    // Actually, this is not needed, since we have a single, stack-allocated
    // device. We do it since it is a best practice.
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * DONE: handle release
     */
    // Nothing to deallocate

    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t entry_pos = 0;
    ssize_t retval = 0;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * DONE: handle read
     */
    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    // find buffer entry and position in entry corresponding to f_pos
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(dev->buffer, *f_pos, &entry_pos);
    if (!entry) {
        // if no suitable pos exists simpy return 0
        goto finalize;
    }

    // read only up to the end of entry
    if (count > entry->size - entry_pos) {
        count = entry->size - entry_pos;
    }

    if (copy_to_user(buf, entry->buffptr + entry_pos, count)) {
        retval = -EFAULT;
        goto finalize;
    }
    *f_pos += count;
    retval = count;

  finalize:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *wip_entry = &dev->wip_entry;
    char* bptr;
    const char* oldbuf;
    uint8_t index;
    
    ssize_t retval = -ENOMEM;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * DONE: handle write
     */

    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    // expand or allocate working entry buffer to accept new content
    PDEBUG("alloc %zu(+1) bytes for entry", count + wip_entry->size);
    bptr = kmalloc(wip_entry->size + count + 1, GFP_KERNEL);
    if (!bptr) {
        retval = -ENOMEM;
        goto finalize;
    }
    bptr[wip_entry->size + count] = '\0';

    if (wip_entry->buffptr) {
        memcpy(bptr, wip_entry->buffptr, wip_entry->size);
    }

    if (copy_from_user(bptr + wip_entry->size, buf, count)) {
        retval = -EFAULT;
        goto finalize;
    }

    // update working entry
    kfree(wip_entry->buffptr);
    wip_entry->buffptr = bptr;
    wip_entry->size += count;

    // add working entry to buffer and free returned buffer, reset wip_entry
    if (wip_entry->buffptr[wip_entry->size - 1] == '\n') {
        PDEBUG("flush entry to buffer: %s", wip_entry->buffptr);
        oldbuf = aesd_circular_buffer_add_entry(dev->buffer, wip_entry); 

        PDEBUG("free old entry buffer: %s", oldbuf);
        kfree(oldbuf);

        wip_entry->buffptr = NULL;
        wip_entry->size = 0;
    }
    
    // Set f_pos to end of buffer, since we appended to end
    *f_pos = aesd_circular_buffer_size(dev->buffer) - 1;

    retval = count;

  finalize:
    mutex_unlock(&dev->lock);
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t buffer_size, new_pos;

    PDEBUG("llseek");

    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    buffer_size = aesd_circular_buffer_size(dev->buffer);
    new_pos = fixed_size_llseek(filp, off, whence, buffer_size);
    
    mutex_unlock(&dev->lock);
    return new_pos;
}

static long aesd_adjust_file_offset(struct file *filp, unsigned int write_cmd, unsigned int write_cmd_offset)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_circular_buffer *buffer = dev->buffer;

    struct aesd_buffer_entry *entry;
    uint8_t current_pos, index;
    size_t offset = 0;

    long retval = 0;

    PDEBUG("adjust_file_offset to (%d,%d)", write_cmd, write_cmd_offset);

    // check valid write cmd 
    if (write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        return -EINVAL;
    }

    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    // compute write_cmd entry global offset in bytes
    for (index = 0; index <= write_cmd; ++index) {
        current_pos = (buffer->out_offs + index) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        entry = &buffer->entry[current_pos];

        // entry not yet written
        if (!entry->buffptr) {
            retval = -EINVAL;
            goto finalize;
        }

        if (index != write_cmd) { // previous entry, add size to offset
            offset += entry->size;
        } else { // requested entry, check if given offset is valid
            if (write_cmd_offset >= entry->size) { 
                retval = -EINVAL;
                goto finalize;
            }
            offset += write_cmd_offset;
        }
    }

    filp->f_pos = offset;

  finalize:
    mutex_unlock(&dev->lock);
    return retval;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *def = filp->private_data;
    long retval;

    PDEBUG("ioctl");

    switch (cmd) {

        case AESDCHAR_IOCSEEKTO:
            struct aesd_seekto seekto;
            if (copy_from_user(&seekto, (const void __user *) arg, sizeof(seekto))) {
                return -EFAULT;
            }
            retval = aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset);
            break;

        default:
            return -ENOTTY;
    }

    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .open =     aesd_open,
    .release =  aesd_release,
    .read =     aesd_read,
    .write =    aesd_write,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;

    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }

    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * DONE: initialize the AESD specific portion of the device
     */
    aesd_device.buffer = kmalloc(sizeof(struct aesd_circular_buffer), GFP_KERNEL);
    if (!aesd_device.buffer) {
        printk(KERN_ERR "Could not allocate circular buffer");
        return -ENOMEM;
    }
    aesd_circular_buffer_init(aesd_device.buffer);
    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);
    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

void aesd_cleanup_module(void)
{
    struct aesd_buffer_entry *entry;
    uint8_t index;

    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * DONE: cleanup AESD specific poritions here as necessary
     */
    if (aesd_device.buffer) {
        AESD_CIRCULAR_BUFFER_FOREACH(entry, aesd_device.buffer, index){
            if (entry->buffptr) {
                kfree(entry->buffptr);
            }
        }
        kfree(aesd_device.buffer);
    }

    if (aesd_device.wip_entry.buffptr) {
        kfree(aesd_device.wip_entry.buffptr);
    }

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
