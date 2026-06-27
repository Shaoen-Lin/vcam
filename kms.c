#include <linux/kernel.h>
#include <linux/slab.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "kms.h"

struct vcam_kms {
    struct drm_simple_display_pipe pipe;
    struct drm_connector connector;
};

static inline struct vcam_kms *to_vcam_kms(struct drm_device *drm)
{
    return drm->dev_private;
}

/* connector */

static enum drm_connector_status
vcam_connector_detect(struct drm_connector *connector, bool force)
{
    if (vcam_kms_connected)
        return connector_status_connected;

    return connector_status_disconnected;
}

static int vcam_connector_get_modes(struct drm_connector *connector)
{
    struct drm_display_mode *mode;

    if (!vcam_kms_connected)
        return 0;

    mode = drm_cvt_mode(connector->dev, 640, 480, 60,
                        false, false, false);
    if (!mode)
        return 0;

    mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
    drm_mode_set_name(mode);
    drm_mode_probed_add(connector, mode);

    return 1;
}

static const struct drm_connector_funcs vcam_connector_funcs = {
    .detect = vcam_connector_detect,
    .fill_modes = drm_helper_probe_single_connector_modes,
    .destroy = drm_connector_cleanup,
    .reset = drm_atomic_helper_connector_reset,
    .atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
    .atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs vcam_connector_helper_funcs = {
    .get_modes = vcam_connector_get_modes,
};

/* simple display pipe */

static const uint32_t vcam_formats[] = {
    DRM_FORMAT_XRGB8888,
};

static int vcam_pipe_check(struct drm_simple_display_pipe *pipe,
                           struct drm_plane_state *plane_state,
                           struct drm_crtc_state *crtc_state)
{
    return 0;
}

static void vcam_pipe_enable(struct drm_simple_display_pipe *pipe,
                             struct drm_crtc_state *crtc_state,
                             struct drm_plane_state *plane_state)
{
    pr_info("vcam: KMS pipe enabled\n");
}

static void vcam_pipe_disable(struct drm_simple_display_pipe *pipe)
{
    pr_info("vcam: KMS pipe disabled\n");
}

static void vcam_pipe_update(struct drm_simple_display_pipe *pipe,
                             struct drm_plane_state *old_state)
{
    /*
     * Step 1 only creates KMS objects.
     * Do not touch GEM memory or scanout here yet.
     */
}

static const struct drm_simple_display_pipe_funcs vcam_pipe_funcs = {
    .check = vcam_pipe_check,
    .enable = vcam_pipe_enable,
    .disable = vcam_pipe_disable,
    .update = vcam_pipe_update,
};

/* mode config */

static struct drm_framebuffer *
vcam_fb_create(struct drm_device *drm,
               struct drm_file *file,
               const struct drm_mode_fb_cmd2 *mode_cmd)
{
    pr_info("vcam: KMS fb_create called\n");

    return drm_gem_fb_create(drm, file, mode_cmd);
}

static const struct drm_mode_config_funcs vcam_mode_config_funcs = {
    .fb_create = vcam_fb_create,
    .atomic_check = drm_atomic_helper_check,
    .atomic_commit = drm_atomic_helper_commit,
};

int vcam_kms_init(struct drm_device *drm)
{
    struct vcam_kms *kms;
    int ret;

    kms = kzalloc(sizeof(*kms), GFP_KERNEL);
    if (!kms)
        return -ENOMEM;

    drm->dev_private = kms;

    drm_mode_config_init(drm);

    drm->mode_config.min_width = 640;
    drm->mode_config.min_height = 480;
    drm->mode_config.max_width = 640;
    drm->mode_config.max_height = 480;
    drm->mode_config.preferred_depth = 24;
    drm->mode_config.funcs = &vcam_mode_config_funcs;

    ret = drm_connector_init(drm,
                             &kms->connector,
                             &vcam_connector_funcs,
                             DRM_MODE_CONNECTOR_VIRTUAL);
    if (ret)
        goto err_mode_config;

    drm_connector_helper_add(&kms->connector,
                             &vcam_connector_helper_funcs);

    kms->connector.interlace_allowed = false;
    kms->connector.doublescan_allowed = false;
    kms->connector.polled = 0;

    ret = drm_simple_display_pipe_init(drm,
                                       &kms->pipe,
                                       &vcam_pipe_funcs,
                                       vcam_formats,
                                       ARRAY_SIZE(vcam_formats),
                                       NULL,
                                       &kms->connector);
    if (ret)
        goto err_connector;

    drm_mode_config_reset(drm);

    pr_info("vcam: KMS initialized\n");
    return 0;

err_connector:
    drm_connector_cleanup(&kms->connector);

err_mode_config:
    drm_mode_config_cleanup(drm);
    drm->dev_private = NULL;
    kfree(kms);
    return ret;
}

void vcam_kms_cleanup(struct drm_device *drm)
{
    struct vcam_kms *kms = to_vcam_kms(drm);

    drm_mode_config_cleanup(drm);

    drm->dev_private = NULL;
    kfree(kms);

    pr_info("vcam: KMS cleaned up\n");
}