#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/kernel.h>

#define DRIVER_NAME        "mpu6050"
#define CLASS_NAME         "mpu6050_class"
#define DEVICE_NAME        "mpu6050"
#define MPU6050_I2C_ADDR   0x68

/* MPU6050 Register Addresses */
#define WHO_AM_I           0x75
#define PWR_MGMT_1         0x6B
#define CONFIG_REG         0x1A
#define GYRO_CONFIG        0x1B
#define ACCEL_CONFIG       0x1C
#define ACCEL_XOUT_H       0x3B
#define TEMP_OUT_H         0x41
#define GYRO_XOUT_H        0x43

/* Data structure to store sensor values */
struct mpu_data {
    s16 accel_x;
    s16 accel_y;
    s16 accel_z;
    s16 temp_raw;
    s16 gyro_x;
    s16 gyro_y;
    s16 gyro_z;
};

/* Device context */
struct mpu6050_dev {
    struct i2c_client   *client;
    struct cdev         cdev;
    dev_t               devt;
    struct class        *class;
};

static struct i2c_device_id mpu_id_table[] = {
    { DEVICE_NAME, 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, mpu_id_table);

/* Forward declarations */
static int mpu_open(struct inode *inode, struct file *file);
static int mpu_release(struct inode *inode, struct file *file);
static ssize_t mpu_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
static int mpu_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int mpu_remove(struct i2c_client *client);

static const struct file_operations mpu_fops = {
    .owner   = THIS_MODULE,
    .open    = mpu_open,
    .release = mpu_release,
    .read    = mpu_read,
};

static struct i2c_driver mpu_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .owner = THIS_MODULE,
    },
    .probe    = mpu_probe,
    .remove   = mpu_remove,
    .id_table = mpu_id_table,
};

/* Read and combine two 8-bit registers into a signed 16-bit value */
static s16 read16(struct i2c_client *client, u8 reg)
{
    int hi = i2c_smbus_read_byte_data(client, reg);
    int lo = i2c_smbus_read_byte_data(client, reg + 1);
    return (s16)((hi << 8) | (lo & 0xFF));
}

/* Read all sensor data in one block */
static int read_sensor_block(struct i2c_client *client, struct mpu_data *data)
{
    u8 buf[14];
    int ret;

    ret = i2c_smbus_read_i2c_block_data(client, ACCEL_XOUT_H, sizeof(buf), buf);
    if (ret < 0)
        return ret;

    data->accel_x   = (buf[0] << 8) | buf[1];
    data->accel_y   = (buf[2] << 8) | buf[3];
    data->accel_z   = (buf[4] << 8) | buf[5];
    data->temp_raw  = (buf[6] << 8) | buf[7];
    data->gyro_x    = (buf[8] << 8) | buf[9];
    data->gyro_y    = (buf[10] << 8) | buf[11];
    data->gyro_z    = (buf[12] << 8) | buf[13];

    return 0;
}

/* Convert raw temperature to Celsius (x100) */
static int convert_temp(int raw)
{
    /* (raw/340) + 36.53, multiplied by 100 */
    return (raw * 100) / 340 + 3653;
}

static int mpu_open(struct inode *inode, struct file *file)
{
    struct mpu6050_dev *dev = container_of(inode->i_cdev, struct mpu6050_dev, cdev);
    file->private_data = dev;
    dev_info(&dev->client->dev, "mpu6050: device opened\n");
    return 0;
}

static int mpu_release(struct inode *inode, struct file *file)
{
    struct mpu6050_dev *dev = file->private_data;
    dev_info(&dev->client->dev, "mpu6050: device closed\n");
    return 0;
}

static ssize_t mpu_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    struct mpu6050_dev *dev = file->private_data;
    struct mpu_data data;
    char out[128];
    int len, temp_c;

    if (*ppos != 0)
        return 0;

    /* Read sensor block */
    if (read_sensor_block(dev->client, &data) < 0)
        return -EIO;

    temp_c = convert_temp(data.temp_raw);
    len = scnprintf(out, sizeof(out),
        "accel_x: %d\naccel_y: %d\naccel_z: %d\n"
        "temp: %d.%02d C\n"
        "gyro_x: %d\ngyro_y: %d\ngyro_z: %d\n",
        data.accel_x, data.accel_y, data.accel_z,
        temp_c / 100, abs(temp_c % 100),
        data.gyro_x, data.gyro_y, data.gyro_z);

    if (len > count)
        len = count;
    if (copy_to_user(buf, out, len))
        return -EFAULT;
    *ppos = len;
    return len;
}

static int mpu_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct mpu6050_dev *dev;
    int ret;
    u8 who;

    dev = devm_kzalloc(&client->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->client = client;
    i2c_set_clientdata(client, dev);

    /* Check WHO_AM_I */
    who = i2c_smbus_read_byte_data(client, WHO_AM_I);
    if (who != MPU6050_I2C_ADDR) {
        dev_err(&client->dev, "Unexpected WHO_AM_I=0x%02x\n", who);
        return -ENODEV;
    }

    /* Wake and configure sensor */
    i2c_smbus_write_byte_data(client, PWR_MGMT_1, 0x00);
    i2c_smbus_write_byte_data(client, CONFIG_REG, 0x03);
    i2c_smbus_write_byte_data(client, GYRO_CONFIG, 0x08);
    i2c_smbus_write_byte_data(client, ACCEL_CONFIG, 0x08);

    /* Allocate char device number */
    ret = alloc_chrdev_region(&dev->devt, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        dev_err(&client->dev, "alloc_chrdev_region failed\n");
        return ret;
    }

    cdev_init(&dev->cdev, &mpu_fops);
    dev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&dev->cdev, dev->devt, 1);
    if (ret) {
        unregister_chrdev_region(dev->devt, 1);
        dev_err(&client->dev, "cdev_add failed\n");
        return ret;
    }

    dev->class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(dev->class)) {
        cdev_del(&dev->cdev);
        unregister_chrdev_region(dev->devt, 1);
        return PTR_ERR(dev->class);
    }
    if (!device_create(dev->class, NULL, dev->devt, NULL, DEVICE_NAME)) {
        class_destroy(dev->class);
        cdev_del(&dev->cdev);
        unregister_chrdev_region(dev->devt, 1);
        dev_err(&client->dev, "device_create failed\n");
        return -EFAULT;
    }
    dev_info(&client->dev, "mpu6050 probed successfully\n");
    return 0;
}
static int mpu_remove(struct i2c_client *client)
{
    struct mpu6050_dev *dev = i2c_get_clientdata(client);
    device_destroy(dev->class, dev->devt);
    class_destroy(dev->class);
    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->devt, 1);

    dev_info(&client->dev, "mpu6050 removed\n");
    return 0;
}
module_i2c_driver(mpu_driver);
MODULE_AUTHOR("Arslan");
MODULE_DESCRIPTION("MPU6050 I2C Sensor Driver");
MODULE_LICENSE("GPL");
