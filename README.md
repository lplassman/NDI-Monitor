# NDI Monitor

NDI Monitor connects to a Full NDI stream using the native NDI SDK. The x86_64 build presents video through an accelerated SDL2 KMS/DRM renderer; other architecture builds use a Linux framebuffer.

## Features
- Manage NDI sources using the integrated web server
- Direct SDK capture with GPU-scaled KMS/DRM output on x86_64
- Uses the latest version of NDI - NDI 5


### Display requirements

No external player, X11, or Wayland dependency is required. The NDI SDK provides video decoding. Audio playback is not supported.

The x86_64 build uses SDL2's KMS/DRM video driver and an accelerated, vertical-sync renderer. It requires an Intel/Linux graphics driver with KMS enabled, access to the primary DRM device (normally through the `video` group), and exclusive use of the display. The service selects the KMSDRM SDL driver. This path scales video on the GPU, but NDI decoding and the transfer of each received frame to a GPU texture still use CPU/memory bandwidth. Other architecture builds use fbdev and require a writable `/dev/fb0` with packed true-color 16-, 24-, or 32-bit pixels.

Run on a dedicated console without a desktop: KMS/DRM takes control of the display and cannot share it with a desktop compositor. Video is scaled with nearest-neighbor sampling to fit the active mode, preserving the NDI picture aspect ratio with black bars where necessary. The x86_64 renderer requests vertical synchronization; actual latency still depends on the sender, network, SDK, and display. For fbdev builds, configure the display resolution before starting; fbdev writes are not synchronized to vertical blank.

### Download required installation files

Make sure git is installed.

```
sudo apt update
sudo apt install git
```
Clone this repository and `cd` into it.

```
git clone https://github.com/lplassman/NDI-Monitor.git && cd NDI-Monitor
```

### Install on Raspberry Pi 4 64-bit

Run this compile and install script

```
sudo bash ./easy-install-rpi4-aarch64.sh
```
Installation is now complete!


### Install on Raspberry Pi 4 32-bit

Run this compile and install script

```
sudo bash ./easy-install-rpi4-armhf.sh
```
Installation is now complete!


### Install on Raspberry Pi 3 32-bit

Run this compile and install script

```
sudo bash ./easy-install-rpi3-armhf.sh
```
Installation is now complete!



### Install on x86_64 bit (Intel/AMD)

Run this compile and install script

```
sudo bash ./easy-install-x86_64.sh
```
Installation is now complete!


### Install on generic ARM64

Compiling on generic ARM64 requires use of the NDI Advanced SDK. Due to licensing restrictions, the NDI Advanced SDK must be downloaded manually from NDI's website: [ndi.tv](https://ndi.tv)
Extract the downloaded NDI Advanced SDK .tar file and copy it to the NDI-Monitor directory on the target device. This can be achieved by using FTP, SCP, or Samba.

Compile and install

```
sudo bash ./easy-install-generic-aarch64.sh
```
Installation is now complete!


## Usage for NDI Monitor

Once the installation process is complete, it will create an executable file located at /opt/ndi_monitor/bin/ndi_monitor

The installer also creates a symlink to /usr/bin so that it can be run from a normal terminal.

Run the monitor and its web server, then open the device's IP address in a browser to select a source:

```
ndi_monitor
```

On x86_64, run from a dedicated console; the service sets `SDL_VIDEODRIVER=KMSDRM`. To run it manually, use `SDL_VIDEODRIVER=KMSDRM ndi_monitor`. The renderer requires an accelerated SDL2 KMS/DRM backend and fails rather than silently switching to CPU rendering. Use `ndi_monitor --framebuffer /dev/fb0` to explicitly select the legacy fbdev output. Only one source can be displayed at a time; stop it before selecting another. Stopping a source or shutting down clears the display.

## Install service file for starting ndi_monitor on boot

By default this service runs as the `ndi` user with supplementary `video` group access to DRM or framebuffer devices. Create that user or customize it before installing. The service selects SDL's KMSDRM driver; use `--framebuffer /dev/fb1` in `ExecStart` to use fbdev instead:

```
sudo nano ./ndi_monitor.service
```
Install the service file

```
sudo cp ./ndi_monitor.service /etc/systemd/system/
sudo systemctl enable ndi_monitor.service
sudo systemctl start ndi_monitor.service
```

## Development checks

The fbdev tests need only the system compiler and Linux headers:

```
g++ -std=c++14 -Wall -Wextra -Werror -Iinclude tests/framebuffer_test.cpp -o /tmp/ndi-framebuffer-test
/tmp/ndi-framebuffer-test
```

With the downloaded NDI SDK headers in `include/`, receiver and source-control tests use SDK and framebuffer fixtures (no NDI runtime or display hardware required):

```
g++ -std=c++14 -pthread -Iinclude tests/monitor_test.cpp mongoose.c mjson.c \
  -Wl,--wrap=open,--wrap=close,--wrap=ioctl,--wrap=mmap,--wrap=munmap -o /tmp/ndi-monitor-test
/tmp/ndi-monitor-test
```

The x86_64 build additionally requires SDL2 development files and `pkg-config`; the installer installs `libsdl2-dev`. Live KMS/DRM rendering can only be verified on a machine with a supported graphics driver and an available console display.
