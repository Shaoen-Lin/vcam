#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/iosys-map.h>
#include <linux/platform_device.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>

#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>

#include "drm.h"
#include "control.h"
#include "device.h"

#include "vcam_drm_uapi.h"

static struct drm_device *vcam_drm_dev;
static struct platform_device *vcam_drm_pdev;

static const struct drm_mode_config_funcs vcam_mode_config_funcs = {
};

// static const struct file_operations vcam_drm_fops = {
//     .owner = THIS_MODULE,
//     .open = drm_open,
//     .release = drm_release,
//     .unlocked_ioctl = drm_ioctl,
//     .compat_ioctl = drm_compat_ioctl,
//     .poll = drm_poll,
//     .read = drm_read,
//     .llseek = noop_llseek,
// };
// 換成下面的
DEFINE_DRM_GEM_FOPS(vcam_drm_fops);

static int vcam_drm_ioctl_checksum(struct drm_device *dev, void *data,
                                   struct drm_file *file)
{
    struct drm_vcam_checksum *args = data;
    struct drm_gem_object *obj;
    struct drm_gem_shmem_object *shmem;
    struct iosys_map map = {};
    unsigned char *p;
    u64 size, i;
    u32 sum = 0;
    int ret = 0;

    obj = drm_gem_object_lookup(file, args->handle);
    if (!obj)
        return -ENOENT;

    shmem = to_drm_gem_shmem_obj(obj);

    ret = drm_gem_shmem_vmap(shmem, &map);
    if (ret)
        goto out_put;

    if (!map.vaddr) {
        ret = -EFAULT;
        goto out_vunmap;
    }

    size = args->size;
    if (!size || size > obj->size)
        size = obj->size;

    p = map.vaddr;

    for (i = 0; i < size; i++)
        sum += p[i];

    args->checksum = sum;
    args->size = size;

    /* Submit DRM buffer to vcam after p/size are valid and before vunmap. */
    {
        struct vcam_device *vcam = get_vcam_device(0);

        if (vcam) {
        
            if (args->bpp == 32 && args->width && args->height && args->pitch) {
                ret = vcam_submit_xrgb8888_frame(vcam, p,
                                                args->width,
                                                args->height,
                                                args->pitch);
            } else {
                ret = vcam_submit_frame(vcam, p, size);
            }

        } else {
            pr_warn("my_vcam: DRM cannot find vcam0 for submit\n");
        }
    }

out_vunmap:
    drm_gem_shmem_vunmap(shmem, &map);
out_put:
    drm_gem_object_put(obj);
    return ret;
}

int vcam_submit_frame(struct vcam_device *dev, const void *src, size_t size)
{
    struct vcam_in_queue *q;
    struct vcam_in_buffer *buf;
    unsigned long flags = 0;
    size_t frame_size;

    // pr_warn("my_vcam: vcam_submit_frame enter dev=%px src=%px size=%zu\n",
    //         dev, src, size);

    // if (!dev) {
    //     pr_warn("my_vcam: submit fail dev is NULL\n");
    //     return -EINVAL;
    // }

    // if (!src) {
    //     pr_warn("my_vcam: submit fail src is NULL\n");
    //     return -EINVAL;
    // }

    frame_size = dev->input_format.sizeimage;
    pr_warn("my_vcam: input sizeimage=%zu fb=%ux%u pixfmt=%u\n",
            frame_size,
            dev->fb_spec.width,
            dev->fb_spec.height,
            dev->fb_spec.pix_fmt);

    if (!frame_size)
        frame_size = dev->fb_spec.width * dev->fb_spec.height * 3;

    if (size > frame_size)
        size = frame_size;

    q = &dev->in_queue;

    spin_lock_irqsave(&dev->in_q_slock, flags);

    // pr_warn("my_vcam: submit debug pending=%px ready=%px b0=%px b0data=%px b1=%px b1data=%px size=%zu\n",
    //         q->pending,
    //         q->ready,
    //         &q->buffers[0], q->buffers[0].data,
    //         &q->buffers[1], q->buffers[1].data,
    //         size);

    buf = q->pending;

    // if (!buf) {
    //     spin_unlock_irqrestore(&dev->in_q_slock, flags);
    //     pr_warn("my_vcam: submit fail pending is NULL\n");
    //     return -EINVAL;
    // }

    // if (!buf->data) {
    //     spin_unlock_irqrestore(&dev->in_q_slock, flags);
    //     pr_warn("my_vcam: submit fail pending data is NULL\n");
    //     return -EINVAL;
    // }

    memcpy(buf->data, src, size);
    buf->filled = size;
    buf->xbar = 0;
    buf->ybar = 0;
    buf->jiffies = jiffies;

    q->ready = buf;
    q->pending = (q->pending == &q->buffers[0]) ? &q->buffers[1] : &q->buffers[0];

    spin_unlock_irqrestore(&dev->in_q_slock, flags);

    pr_warn("my_vcam: submit frame success size=%zu\n", size);
    return 0;
}

static int vcam_drm_ioctl_submit(struct drm_device *dev, void *data,
                                 struct drm_file *file)
{
    struct drm_vcam_submit *args = data;
    struct drm_gem_object *obj;
    struct drm_gem_shmem_object *shmem;
    struct iosys_map map = {};
    struct vcam_device *vcam;
    unsigned char *p;
    u64 size;
    int ret = 0;

    obj = drm_gem_object_lookup(file, args->handle);
    if (!obj)
        return -ENOENT;

    shmem = to_drm_gem_shmem_obj(obj);

    ret = drm_gem_shmem_vmap(shmem, &map);
    if (ret)
        goto out_put;

    if (!map.vaddr) {
        ret = -EFAULT;
        goto out_vunmap;
    }

    p = map.vaddr;
    size = obj->size;

    vcam = get_vcam_device(0);
    if (!vcam) {
        ret = -ENODEV;
        goto out_vunmap;
    }

    if (args->bpp == 32 && args->width && args->height && args->pitch) {
        ret = vcam_submit_xrgb8888_frame(vcam, p,
                                         args->width,
                                         args->height,
                                         args->pitch);
    } else {
        ret = vcam_submit_frame(vcam, p, size);
    }

out_vunmap:
    drm_gem_shmem_vunmap(shmem, &map);
out_put:
    drm_gem_object_put(obj);
    return ret;
}

static const struct drm_ioctl_desc vcam_drm_ioctls[] = {
    DRM_IOCTL_DEF_DRV(VCAM_CHECKSUM, vcam_drm_ioctl_checksum,
                      DRM_RENDER_ALLOW),
    DRM_IOCTL_DEF_DRV(VCAM_SUBMIT, vcam_drm_ioctl_submit,
                      DRM_RENDER_ALLOW),
};

static struct drm_driver vcam_drm_driver = {
    // .driver_features = DRIVER_MODESET,
    .driver_features = DRIVER_GEM,
    .fops = &vcam_drm_fops,

    DRM_GEM_SHMEM_DRIVER_OPS,

    .ioctls = vcam_drm_ioctls,
    .num_ioctls = ARRAY_SIZE(vcam_drm_ioctls),

    .name = "vcam_drm",
    .desc = "Virtual DRM device for vcam",
    .date = "20260523",
    .major = 0,
    .minor = 1,
};

int vcam_drm_init(void)
{
    int ret;

    // pr_info("my_vcam: DRM device init\n");

    vcam_drm_pdev = platform_device_register_simple("vcam-drm", -1, NULL, 0);
    if (IS_ERR(vcam_drm_pdev))
        return PTR_ERR(vcam_drm_pdev);

    vcam_drm_dev = drm_dev_alloc(&vcam_drm_driver, &vcam_drm_pdev->dev);
    if (IS_ERR(vcam_drm_dev)) {
        ret = PTR_ERR(vcam_drm_dev);
        platform_device_unregister(vcam_drm_pdev);
        vcam_drm_pdev = NULL;
        return ret;
    }

    drm_mode_config_init(vcam_drm_dev);

    vcam_drm_dev->mode_config.min_width = 1;
    vcam_drm_dev->mode_config.min_height = 1;
    vcam_drm_dev->mode_config.max_width = 4096;
    vcam_drm_dev->mode_config.max_height = 4096;
    vcam_drm_dev->mode_config.funcs = &vcam_mode_config_funcs;

    ret = drm_dev_register(vcam_drm_dev, 0);

    if (ret) {
        drm_mode_config_cleanup(vcam_drm_dev);
        drm_dev_put(vcam_drm_dev);
        vcam_drm_dev = NULL;
        platform_device_unregister(vcam_drm_pdev);
        vcam_drm_pdev = NULL;
        return ret;
    }

    // pr_info("my_vcam: DRM device registered\n");
    return 0;
}

void vcam_drm_exit(void)
{
    // pr_info("my_vcam: DRM device exit\n");

    if (vcam_drm_dev) {
        drm_dev_unregister(vcam_drm_dev);
        drm_mode_config_cleanup(vcam_drm_dev);
        drm_dev_put(vcam_drm_dev);
        vcam_drm_dev = NULL;
    }

    if (vcam_drm_pdev) {
        platform_device_unregister(vcam_drm_pdev);
        vcam_drm_pdev = NULL;
    }
}