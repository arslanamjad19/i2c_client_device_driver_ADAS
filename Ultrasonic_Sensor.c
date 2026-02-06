#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/param.h>
#include <linux/moduleparam.h>
#include <linux/string.h>

#define BCM2711_GPIO_BASE   0xfe200000
#define GPIO_BLOCK_SZ       0x1000
/* Offsets (bytes) from GPIO base */
#define GPFSEL0_OFFSET      0x00    /* controls GPIO[0..9] */
#define GPFSEL1_OFFSET      0x04    /* controls GPIO[10..19] */
#define GPFSEL2_OFFSET      0x08    /* controls GPIO[20..29] */
#define GPSET0_OFFSET       0x1C    /* set pins 0..31 */
#define GPCLR0_OFFSET       0x28    /* clear pins 0..31 */
#define GPLEV0_OFFSET       0x34    /* read pin levels 0..31 */

/* Default HC-SR04 pins - standard configuration */
static int trig_pin = 17;    /* GPIO17 as output (trigger) */
static int echo_pin = 27;    /* GPIO27 as input (echo) */
/* Allow pin configuration via module parameters */
module_param(trig_pin, int, 0644);
MODULE_PARM_DESC(trig_pin, "GPIO pin connected to TRIG (output)");
module_param(echo_pin, int, 0644);
MODULE_PARM_DESC(echo_pin, "GPIO pin connected to ECHO (input)");
static struct proc_dir_entry *proc_entry_text;
static struct proc_dir_entry *proc_entry_raw;
static struct proc_dir_entry *proc_dir;
static void __iomem *gpio_base;
/* Last measurement results - globally accessible */
static u64 last_distance_cm_x100 = 0;
static s64 last_echo_time_ns = 0;
static int last_status = 0; /* 0=success, -1=timeout, -2=error */
/* helper to write a 32-bit GPIO register */
static inline void gpio_writel(u32 val, u32 off)
{
    writel(val, gpio_base + off);
}
/* helper to read a 32-bit GPIO register */
static inline u32 gpio_readl(u32 off)
{
    return readl(gpio_base + off);
}
/* Set the 3-bit field for `pin` in GPFSEL to `func` (0=IN,1=OUT) */
static void gpio_set_function(unsigned pin, u8 func)
{
    u32 reg;
    unsigned offset, shift;
    
    /* Determine which GPFSEL register to use */
    if (pin < 10) {
        offset = GPFSEL0_OFFSET;
        shift = pin * 3;
    } else if (pin < 20) {
        offset = GPFSEL1_OFFSET;
        shift = (pin - 10) * 3;
    } else if (pin < 30) {
        offset = GPFSEL2_OFFSET;
        shift = (pin - 20) * 3;
    } else {
        pr_err("hcsr04: Invalid pin %u\n", pin);
        return;
    }
    reg = gpio_readl(offset);
    reg &= ~(0x7 << shift);
    reg |= ((func & 0x7) << shift);
    gpio_writel(reg, offset);
}
/* Drive an output pin high or low */
static void gpio_set_level(unsigned pin, bool high)
{
    if (high)
        gpio_writel(1u << pin, GPSET0_OFFSET);
    else
        gpio_writel(1u << pin, GPCLR0_OFFSET);
}
/* Read an input pin level (0 or 1) */
static bool gpio_get_level(unsigned pin)
{
    return !!(gpio_readl(GPLEV0_OFFSET) & (1u << pin));
}
/* Trigger a measurement and update global results */
static void trigger_measurement(void)
{
    ktime_t t0, t1;
    s64 delta_ns;
    unsigned long timeout;
    /* 1) send 10µs pulse on TRIG pin */
    gpio_set_level(trig_pin, true);
    udelay(10);
    gpio_set_level(trig_pin, false);
    
    /* 2) wait for ECHO rising edge (timeout ~ 30 ms) */
    timeout = jiffies + msecs_to_jiffies(30);
    while (!gpio_get_level(echo_pin) && time_before(jiffies, timeout))
        cpu_relax();
    if (!time_before(jiffies, timeout)) {
        pr_debug("hcsr04: timeout waiting for ECHO rising edge\n");
        last_status = -1;
        last_echo_time_ns = 0;
        last_distance_cm_x100 = 0;
        return;
    }
    t0 = ktime_get();
    /* 3) wait for ECHO falling edge (timeout ~ 30 ms) */
    timeout = jiffies + msecs_to_jiffies(30);
    while (gpio_get_level(echo_pin) && time_before(jiffies, timeout))
        cpu_relax();
    if (!time_before(jiffies, timeout)) {
        pr_debug("hcsr04: timeout waiting for ECHO falling edge\n");
        last_status = -1;
        last_echo_time_ns = 0;
        last_distance_cm_x100 = 0;
        return;
    }
    
    t1 = ktime_get();
    
    /* 4) compute delta time and distance */
    delta_ns = ktime_to_ns(ktime_sub(t1, t0));
    /* Save measurement data */
    last_echo_time_ns = delta_ns;
    /* 
     * Calculate distance:
     * Speed of sound = 343 m/s = 34300 cm/s = 0.0343 cm/µs = 0.0000343 cm/ns
     * Distance = (Time * Speed of sound) / 2
     * We use fixed-point with 2 decimal places (x100)
     */
    last_distance_cm_x100 = (delta_ns * 343) / (2 * 10000);
    last_status = 0;
}
/* Formatted text output for human readability */
static ssize_t hcsr04_read_text(struct file *file,
                              char __user *buf,
                              size_t count,
                              loff_t *ppos)
{
    char out[128];
    int len;
    if (*ppos > 0)
        return 0;  /* EOF */
    
    /* Perform a new measurement */
    trigger_measurement();
    /* Format the output according to status */
    if (last_status == 0) {
        /* Format distance as integer.fractional without using floating point */
        u64 dist_int = last_distance_cm_x100 / 100;
        u64 dist_frac = last_distance_cm_x100 % 100;
        
        len = snprintf(out, sizeof(out),
                 "status=%d\ntime_ns=%lld\ndistance_cm=%llu.%02llu\n",
                 last_status,
                 last_echo_time_ns,
                 dist_int, dist_frac);
    } else {
        len = snprintf(out, sizeof(out),
                 "status=%d\ntime_ns=0\ndistance_cm=0.00\n",
                 last_status);
    }
    if (len > count)
        len = count;
        
    if (copy_to_user(buf, out, len))
        return -EFAULT;
        
    *ppos += len;
    return len;
}

/* Raw output for programmatic access - just the distance in micrometers */
static ssize_t hcsr04_read_raw(struct file *file,
                             char __user *buf,
                             size_t count,
                             loff_t *ppos)
{
    char out[32];
    int len;
    
    if (*ppos > 0)
        return 0;  /* EOF */
    /* Perform a new measurement */
    trigger_measurement();
    /* Format output as a simple number (micrometers) or negative error code */
    if (last_status == 0) {
        len = snprintf(out, sizeof(out), "%llu\n", last_distance_cm_x100 * 100);
    } else {
        len = snprintf(out, sizeof(out), "%d\n", last_status);
    }
    if (len > count)
        len = count;
    if (copy_to_user(buf, out, len))
        return -EFAULT;
        
    *ppos += len;
    return len;
}
static const struct proc_ops hcsr04_text_fops = {
    .proc_read = hcsr04_read_text,
};
static const struct proc_ops hcsr04_raw_fops = {
    .proc_read = hcsr04_read_raw,
};
static int __init hcsr04_init(void)
{
    pr_info("hcsr04: mapping GPIO block...\n");
    gpio_base = ioremap(BCM2711_GPIO_BASE, GPIO_BLOCK_SZ);
    if (!gpio_base) {
        pr_err("hcsr04: ioremap failed\n");
        return -ENOMEM;
    }
    /* configure pins properly: TRIG=output, ECHO=input */
    gpio_set_function(trig_pin, 1);  /* output */
    gpio_set_function(echo_pin, 0);  /* input */
    /* ensure TRIG is initially low */
    gpio_set_level(trig_pin, false);
    
    pr_info("hcsr04: TRIG=%d (output), ECHO=%d (input)\n",
            trig_pin, echo_pin);

    /* create proc directory */
    proc_dir = proc_mkdir("hcsr04", NULL);
    if (!proc_dir) {
        pr_err("hcsr04: proc_mkdir failed\n");
        iounmap(gpio_base);
        return -ENOMEM;
    }
    /* create text format output */
    proc_entry_text = proc_create("text", 0444, proc_dir, &hcsr04_text_fops);
    if (!proc_entry_text) {
        pr_err("hcsr04: proc_create text failed\n");
        proc_remove(proc_dir);
        iounmap(gpio_base);
        return -ENOMEM;
    }
    /* create raw format output */
    proc_entry_raw = proc_create("raw", 0444, proc_dir, &hcsr04_raw_fops);
    if (!proc_entry_raw) {
        pr_err("hcsr04: proc_create raw failed\n");
        proc_remove(proc_entry_text);
        proc_remove(proc_dir);
        iounmap(gpio_base);
        return -ENOMEM;
    }
    pr_info("hcsr04: ready, interfaces available:\n");
    pr_info("hcsr04:   cat /proc/hcsr04/text - human readable output\n");
    pr_info("hcsr04:   cat /proc/hcsr04/raw - raw value in micrometers\n");
    
    return 0;
}
static void __exit hcsr04_exit(void)
{
    pr_info("hcsr04: unloading driver\n");
    proc_remove(proc_entry_raw);
    proc_remove(proc_entry_text);
    proc_remove(proc_dir);
    iounmap(gpio_base);
}
module_init(hcsr04_init);
module_exit(hcsr04_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arslan");
MODULE_DESCRIPTION("HC-SR04 ultrasonic distance sensor driver");
MODULE_VERSION("2.0");
