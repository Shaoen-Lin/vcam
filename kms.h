#ifndef VCAM_KMS_H
#define VCAM_KMS_H

#include <linux/types.h>
#include <drm/drm_device.h>

extern bool vcam_kms_connected;

int vcam_kms_init(struct drm_device *drm);
void vcam_kms_cleanup(struct drm_device *drm);

#endif