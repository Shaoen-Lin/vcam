#ifndef VCAM_DRM_UAPI_H
#define VCAM_DRM_UAPI_H

#include <drm/drm.h>

// struct drm_vcam_checksum {
//     __u32 handle;
//     __u32 checksum;
//     __u64 size;
// };

struct drm_vcam_checksum {
    __u32 handle;
    __u32 checksum;
    __u64 size;

    __u32 width;
    __u32 height;
    __u32 pitch;
    __u32 bpp;
};

struct drm_vcam_submit {
    __u32 handle;
    __u32 vcam_index;
    __u32 width;
    __u32 height;
    __u32 pitch;
    __u32 bpp;
};

#define DRM_VCAM_CHECKSUM 0x00
#define DRM_VCAM_SUBMIT   0x01

#define DRM_IOCTL_VCAM_CHECKSUM \
    DRM_IOWR(DRM_COMMAND_BASE + DRM_VCAM_CHECKSUM, struct drm_vcam_checksum)

#define DRM_IOCTL_VCAM_SUBMIT \
    DRM_IOW(DRM_COMMAND_BASE + DRM_VCAM_SUBMIT, struct drm_vcam_submit)

#endif