# NDI Monitor

NDI Monitor connects to a Full NDI stream using the native NDI SDK and writes video directly to a Linux framebuffer device.

## Features
- Manage NDI sources using the integrated web server
- Direct SDK capture and framebuffer output, with no external player or application frame queue
- Uses the latest version of NDI - NDI 5


### Display requirements

No FFmpeg, SDL, X11, or Wayland dependency is required. The NDI SDK provides video decoding. Audio playback is not supported.

The display must expose a writable Linux fbdev device (default `/dev/fb0`) using packed true-color 16-, 24-, or 32-bit pixels. Palette, grayscale, and planar modes are not supported. Configure the display resolution before starting the monitor. Video is scaled with nearest-neighbor sampling to fit the display, preserving the NDI picture aspect ratio with black bars where necessary. Matching the display mode to the source resolution avoids scaling overhead.

Run on a dedicated console/display: desktop compositors and console output must not write to the same framebuffer. On systems that expose only DRM/KMS, enable fbdev emulation or a framebuffer driver first. The monitor does not configure display modes or synchronize writes to vertical blank; tearing is possible, and actual latency depends on the sender, network, SDK, and display.

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

Select a different framebuffer with `ndi_monitor --framebuffer /dev/fb1`. The process needs read/write access to that device, typically through membership in the `video` group. Only one source can be displayed at a time; stop it before selecting another. Stopping a source or shutting down clears the display.

## Install service file for starting ndi_monitor on boot

By default this service runs as the `ndi` user with supplementary `video` group access to the framebuffer. Create that user or customize it before installing. Adjust `ExecStart` with `--framebuffer /dev/fb1` if needed:

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

The framebuffer tests need only the system compiler and Linux headers:

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
