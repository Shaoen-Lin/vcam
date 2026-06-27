# vcam: Virtual camera device driver for Linux

`vcam` is a simplified virtual V4L2-compatible camera device driver for Linux.

This version extends the original fbdev-based input path with a DRM GEM dumb-buffer input path. Userspace can either write raw frames through the legacy `/dev/fbX` interface, or submit frames through a DRM GEM buffer exposed via `/dev/dri/cardX`.

The main output device is still a V4L2 video device:

```text
/dev/videoX
```

This driver is intended to behave as a virtual camera, not as a display device.

---

## Prerequisites

Install Linux kernel headers that match the currently running kernel:

```bash
sudo apt install linux-headers-$(uname -r)
```

Install V4L2 utilities:

```bash
sudo apt install v4l-utils
```

Install VLC if you want to view the virtual camera output:

```bash
sudo apt install vlc
```

Install build tools if needed:

```bash
sudo apt install build-essential
```

This implementation does not require `libdrm` for the current userspace test program.
The test program directly uses DRM ioctls and `mmap()`.

---

## Build

Build the kernel module and utility:

```bash
make clean
make
```

This generates:

```text
vcam.ko
vcam-util
```

Build the DRM producer test program:

```bash
gcc -Wall -O2 drm-dumb-test.c -o drm-dumb-test
```

---

## Kernel module dependencies

Before inserting the module manually with `insmod`, load the required dependencies:

```bash
sudo modprobe videodev
sudo modprobe videobuf2-common
sudo modprobe videobuf2-memops
sudo modprobe videobuf2-v4l2
sudo modprobe videobuf2-vmalloc
sudo modprobe videobuf2-dma-contig
sudo modprobe drm
sudo modprobe drm_shmem_helper
```

---

## Module parameters

Available parameters:

| Parameter              | Default | Description                                                            |
| ---------------------- | ------: | ---------------------------------------------------------------------- |
| `devices_max`          |     `8` | Maximum number of virtual camera devices.                              |
| `create_devices`       |     `1` | Number of virtual camera devices created during module initialization. |
| `allow_pix_conversion` |     `0` | Allow pixel format conversion by default.                              |
| `allow_scaling`        |     `0` | Allow image scaling by default.                                        |
| `allow_cropping`       |     `0` | Allow image cropping by default.                                       |
| `enable_fbdev`         |  `true` | Enable legacy `/dev/fbX` input path. Set to `0` for DRM-only input.    |

Example:

```bash
sudo insmod ./vcam.ko enable_fbdev=0
```

---

## Device nodes

When the module is loaded, the following device nodes may be created:

```text
/dev/videoX
/dev/vcamctl
/dev/dri/cardX
/dev/fbX
```

Their meanings are:

| Device node      | Description                                                    |
| ---------------- | -------------------------------------------------------------- |
| `/dev/videoX`    | V4L2 virtual camera output device.                             |
| `/dev/vcamctl`   | Control device used by `vcam-util`.                            |
| `/dev/dri/cardX` | DRM GEM input device.                                          |
| `/dev/fbX`       | Legacy fbdev input device. Created only when `enable_fbdev=1`. |

In DRM-only mode:

```bash
sudo insmod ./vcam.ko enable_fbdev=0
```

`vcam` creates:

```text
/dev/videoX
/dev/vcamctl
/dev/dri/cardX
```

It does not create a new `/dev/fbX`.

In legacy-compatible mode:

```bash
sudo insmod ./vcam.ko enable_fbdev=1
```

`vcam` keeps the original fbdev input path and also enables the DRM input path.

---

## Original fbdev input path

The original `vcam` input path uses fbdev:

```text
userspace
   |
   | write raw RGB24 frame
   v
/dev/fbX
   |
   v
vcam input queue
   |
   v
/dev/videoX
```

The default image format is:

```text
640x480 RGB24
```

Userspace can write raw RGB24 frame data to `/dev/fbX`, and the resulting stream appears on `/dev/videoX`.

List available virtual camera devices:

```bash
sudo ./vcam-util -l
```

Example output:

```text
Available virtual V4L2 compatible devices:
1. fbX(640,480,rgb24,mmap) -> /dev/video0
```

---

## DRM GEM input path

This version adds a DRM GEM based input path:

```text
userspace producer
   |
   | DRM_IOCTL_MODE_CREATE_DUMB
   | DRM_IOCTL_MODE_MAP_DUMB
   | mmap()
   | DRM_IOCTL_VCAM_SUBMIT
   v
/dev/dri/cardX
   |
   v
DRM GEM shmem buffer
   |
   | XRGB8888 -> RGB24
   v
vcam input queue
   |
   v
/dev/videoX
```

The DRM input path uses DRM only as a mmap-capable buffer management interface.

It does not implement a full KMS display pipeline. `vcam` is a virtual V4L2 camera, not a display device, so it does not need:

```text
plane
CRTC
encoder
connector
```

The current design uses:

```text
DRM GEM
DRM shmem helper
DRM dumb buffer
custom VCAM_SUBMIT ioctl
```

---

## Why this does not use KMS

KMS is used by display drivers to scan out framebuffers to monitors.

A typical KMS display pipeline is:

```text
framebuffer
   |
   v
plane
   |
   v
CRTC
   |
   v
encoder
   |
   v
connector
   |
   v
monitor
```

`vcam` does not send frames to a monitor.
It receives frames from userspace and exposes them as a virtual camera stream through `/dev/videoX`.

Therefore, the DRM extension only uses GEM dumb buffers as a modern userspace input interface.

---

## Current userspace producer

The test producer is:

```text
drm-dumb-test.c
```

It does the following:

```text
1. Open /dev/dri/card0
2. Create a DRM dumb buffer
3. Map the DRM buffer to userspace
4. Write XRGB8888 moving color bars
5. Submit the buffer to vcam using DRM_IOCTL_VCAM_SUBMIT
6. Repeat for multiple frames
```

Build it with:

```bash
gcc -Wall -O2 drm-dumb-test.c -o drm-dumb-test
```

Run it:

```bash
sudo ./drm-dumb-test
```

Expected output:

```text
handle=1 pitch=2560 size=1228800
stream submit done
```

Note: the current test program opens `/dev/dri/card0` by default.
If your DRM device appears as another card, such as `/dev/dri/card1` or `/dev/dri/card2`, update the path in `drm-dumb-test.c`.

---

## View output with VLC

Load the module in DRM-only mode:

```bash
sudo insmod ./vcam.ko enable_fbdev=0
```

Open the V4L2 device with VLC:

```bash
vlc v4l2:///dev/video0 :v4l2-width=640 :v4l2-height=480 :v4l2-chroma=RV24
```

In another terminal, run:

```bash
sudo ./drm-dumb-test
```

The VLC window should show moving color bars.

---

## V4L2 validation

Use `v4l2-compliance` to validate the virtual camera device:

```bash
sudo v4l2-compliance -d /dev/video0 -f
```

A healthy result should end with:

```text
Failed: 0
Warnings: 0
```

Check device information:

```bash
sudo v4l2-ctl -d /dev/video0 --all
```

---

## DRM-only quick test

```bash
sudo rmmod vcam 2>/dev/null
sudo dmesg -C

sudo insmod ./vcam.ko enable_fbdev=0

ls -l /dev/video* /dev/vcamctl /dev/dri 2>&1

sudo ./drm-dumb-test

sudo rmmod vcam

sudo dmesg | grep -i "warning\|oops\|bug\|fail\|vcam\|drm" | tail -n 80
```

Expected result:

```text
stream submit done
```

There should be no warning, oops, bug, or failure messages.

---

## Basic stability test

Run repeated load, submit, and unload cycles:

```bash
for i in $(seq 1 20); do
    echo "=== round $i ==="
    sudo rmmod vcam 2>/dev/null
    sudo insmod ./vcam.ko enable_fbdev=0 || break
    sudo ./drm-dumb-test || break
    sudo rmmod vcam || break
done
```

After the test:

```bash
sudo dmesg | grep -i "warning\|oops\|bug\|fail"
```

No warning, oops, or bug should appear.

---

## Userspace dependency

The current DRM producer does not depend on `libdrm`.

It directly uses:

```c
DRM_IOCTL_MODE_CREATE_DUMB
DRM_IOCTL_MODE_MAP_DUMB
DRM_IOCTL_VCAM_SUBMIT
mmap()
```

Future userspace tools may use `libdrm` wrappers, but `libdrm` is not required for the current implementation.

---

## Current limitations

1. The DRM input path is copy-based.
2. The userspace DRM buffer format is XRGB8888.
3. The vcam input format is RGB24.
4. The driver converts XRGB8888 to RGB24 before submitting the frame to the vcam input queue.
5. This is not zero-copy.
6. The current test program opens `/dev/dri/card0` by default.
7. The current submit path assumes the target vcam device already exists.
8. `DRM_IOCTL_VCAM_SUBMIT` can select the target vcam device by `vcam_index`, but stronger lifetime synchronization is still needed.
9. Device removal during active DRM submit is not fully protected yet.
10. A future version should add reference counting or another lifetime protection mechanism for `vcam_device`.

---

## Development notes

The current DRM path is intentionally not a KMS implementation.

The goal is:

```text
/dev/dri/cardX
   |
   | mmap-capable DRM GEM dumb buffer
   v
vcam input queue
   |
   v
/dev/videoX
```

not:

```text
/dev/dri/cardX
   |
   | KMS scanout
   v
monitor
```

This keeps the driver closer to its role as a virtual camera.

---

## Troubleshooting

### `insmod: Device or resource busy`

Check whether an older module instance is still loaded:

```bash
lsmod | grep vcam
```

Remove it:

```bash
sudo rmmod vcam
```

If the module reference count is abnormal or cannot be removed, reboot before testing again.

### Duplicate `/class/vcamctl`

If dmesg shows:

```text
sysfs: cannot create duplicate filename '/class/vcamctl'
```

an older instance may not have been cleaned up correctly.

Check:

```bash
lsmod | grep vcam
ls -l /sys/class | grep vcam
```

Then remove the module or reboot.

### VLC does not show output

Make sure `/dev/video0` exists:

```bash
ls -l /dev/video0
```

Make sure the module is loaded:

```bash
lsmod | grep vcam
```

Open VLC first:

```bash
vlc v4l2:///dev/video0 :v4l2-width=640 :v4l2-height=480 :v4l2-chroma=RV24
```

Then run the producer:

```bash
sudo ./drm-dumb-test
```

### `/dev/dri/card0` does not exist

Check all DRM nodes:

```bash
ls -l /dev/dri
```

If the virtual DRM device is not `card0`, update the path in `drm-dumb-test.c`.

---

## Related Projects

* [akvcam](https://github.com/webcamoid/akvcam)
* [v4l2loopback](https://github.com/umlaeute/v4l2loopback)
* [vivid: The Virtual Video Test Driver](https://www.kernel.org/doc/html/latest/media/v4l-drivers/vivid.html)

---

## License

`vcam` is released under the MIT License. Use of this source code is governed by the MIT License that can be found in the LICENSE file.
