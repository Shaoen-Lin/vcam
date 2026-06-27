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
#include <drm/drm_atomic_helper.h>

#include "drm.h"
#include "control.h"
#include "device.h"
#include "kms.h"

#include "vcam_drm_uapi.h"

static struct drm_device *vcam_drm_dev;
static struct platform_device *vcam_drm_pdev;

bool vcam_enable_kms = false;
bool vcam_kms_connected = false;

static bool vcam_kms_active;

module_param_named(enable_kms, vcam_enable_kms, bool, 0644);
MODULE_PARM_DESC(enable_kms, "Enable experimental DRM KMS pipeline");

module_param_named(kms_connected, vcam_kms_connected, bool, 0644);
MODULE_PARM_DESC(kms_connected, "Report virtual KMS connector as connected");

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
    // {
    //     struct vcam_device *vcam = get_vcam_device(0);

    //     if (vcam) {
        
    //         if (args->bpp == 32 && args->width && args->height && args->pitch) {
    //             ret = vcam_submit_xrgb8888_frame(vcam, p,
    //                                             args->width,
    //                                             args->height,
    //                                             args->pitch);
    //         } else {
    //             ret = vcam_submit_frame(vcam, p, size);
    //         }

    //     } else {
    //         pr_warn("vcam: DRM cannot find vcam0 for submit\n");
    //     }
    // }

out_vunmap:
    drm_gem_shmem_vunmap(shmem, &map);
out_put:
    drm_gem_object_put(obj);
    return ret;
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

    vcam = get_vcam_device(args->vcam_index);
    if (!vcam) {
        pr_warn("failed to get vcam device %u\n", args->vcam_index);
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
    .date = "20260627", 
    .major = 0,
    .minor = 1,
};

int vcam_drm_init(void)
{
    int ret;

    // pr_info("vcam: DRM device init\n");

    vcam_drm_pdev = platform_device_register_simple("vcam-drm", -1, NULL, 0);
    if (IS_ERR(vcam_drm_pdev))
        return PTR_ERR(vcam_drm_pdev);

    vcam_drm_driver.driver_features = DRIVER_GEM;
    if (vcam_enable_kms)
        vcam_drm_driver.driver_features |= DRIVER_MODESET | DRIVER_ATOMIC;

    vcam_drm_dev = drm_dev_alloc(&vcam_drm_driver, &vcam_drm_pdev->dev);
    if (IS_ERR(vcam_drm_dev)) {
        ret = PTR_ERR(vcam_drm_dev);
        platform_device_unregister(vcam_drm_pdev);
        vcam_drm_pdev = NULL;
        return ret;
    }

    drm_mode_config_init(vcam_drm_dev);

    // vcam_drm_dev->mode_config.min_width = 1;
    // vcam_drm_dev->mode_config.min_height = 1;
    // vcam_drm_dev->mode_config.max_width = 4096;
    // vcam_drm_dev->mode_config.max_height = 4096;
    // vcam_drm_dev->mode_config.funcs = &vcam_mode_config_funcs;

    if (vcam_enable_kms) {
    ret = vcam_kms_init(vcam_drm_dev);
    if (ret) {
        pr_err("vcam: failed to init KMS: %d\n", ret);
        drm_dev_put(vcam_drm_dev);
        vcam_drm_dev = NULL;
        platform_device_unregister(vcam_drm_pdev);
        vcam_drm_pdev = NULL;
        return ret;
    }

    vcam_kms_active = true;
}

    ret = drm_dev_register(vcam_drm_dev, 0);

    if (ret) {
        if (vcam_kms_active) {
            vcam_kms_cleanup(vcam_drm_dev);
            vcam_kms_active = false;
        }

        drm_dev_put(vcam_drm_dev);
        vcam_drm_dev = NULL;

        platform_device_unregister(vcam_drm_pdev);
        vcam_drm_pdev = NULL;

        return ret;
    }

    // pr_info("vcam: DRM device registered\n");
    return 0;
}

void vcam_drm_exit(void)
{
    // pr_info("vcam: DRM device exit\n");

    if (vcam_drm_dev) {
        if (vcam_kms_active)
            drm_atomic_helper_shutdown(vcam_drm_dev);

        drm_dev_unregister(vcam_drm_dev);

        if (vcam_kms_active) {
            vcam_kms_cleanup(vcam_drm_dev);
            vcam_kms_active = false;
        }

        drm_dev_put(vcam_drm_dev);
        vcam_drm_dev = NULL;
    }

    if (vcam_drm_pdev) {
        platform_device_unregister(vcam_drm_pdev);
        vcam_drm_pdev = NULL;
    }
}