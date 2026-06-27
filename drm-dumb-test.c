#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <drm/drm.h>
#include "vcam_drm_uapi.h"

int main(void)
{
    int ret = 0;
    int fd;
    void *map;
    struct drm_mode_create_dumb create = {0};
    struct drm_mode_map_dumb map_req = {0};
    struct drm_mode_destroy_dumb destroy = {0};

    fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    create.width = 640;
    create.height = 480;
    create.bpp = 32;

    if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
        perror("CREATE_DUMB");
        close(fd);
        return 1;
    }

    printf("handle=%u pitch=%u size=%llu\n",
           create.handle, create.pitch,
           (unsigned long long)create.size);

    map_req.handle = create.handle;
    if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req) < 0) {
        perror("MAP_DUMB");
        destroy.handle = create.handle;
        ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        close(fd);
        return 1;
    }

    map = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED,
               fd, map_req.offset);
    if (map == MAP_FAILED) {
        perror("mmap");
        destroy.handle = create.handle;
        ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
        close(fd);
        return 1;
    }

    unsigned int *pix = map;
    unsigned int x, y;

    struct drm_vcam_submit submit = {0};

    submit.handle = create.handle;
    submit.vcam_index = 0;
    submit.width = create.width;
    submit.height = create.height;
    submit.pitch = create.pitch;
    submit.bpp = 32;

    for (unsigned int frame = 0; frame < 300; frame++) {
        unsigned int shift = (frame * 4) % create.width;

        for (y = 0; y < create.height; y++) {
            for (x = 0; x < create.width; x++) {
                unsigned int xx = (x + shift) % create.width;

                if (xx < create.width / 3)
                    pix[y * (create.pitch / 4) + x] = 0x00ff0000;
                else if (xx < create.width * 2 / 3)
                    pix[y * (create.pitch / 4) + x] = 0x0000ff00;
                else
                    pix[y * (create.pitch / 4) + x] = 0x000000ff;
            }
        }

        if (ioctl(fd, DRM_IOCTL_VCAM_SUBMIT, &submit) < 0) {
            perror("VCAM_SUBMIT");
            ret = 1;
            goto out;
        }

        usleep(33000);
    }

    printf("stream submit done\n");

out:
    munmap(map, create.size);

    destroy.handle = create.handle;
    if (ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy) < 0)
        perror("DESTROY_DUMB");

    close(fd);
    return ret;
}