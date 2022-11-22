# NDI Monitor

NDI Monitor is an application that connects to any Full NDI stream and displays the output on a connected display

## Features
- Manage NDI sources using the integrated web server
- Nearly zero latency
- Uses the latest version of NDI - NDI 5


### Ensure a version of FFMPEG with NDI support is installed on the system
- If FFMPEG not installed, follow instructions here to do so: [Install FFMPEG-NDI](https://github.com/lplassman/FFMPEG-NDI)

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

To run and start the web server while specifying a path to where the recordings should be stored:

```
ndi_monitor
```

## Install service file for starting ndi_monitor on boot

By default this service file runs ndi_monitor as the "ndi" user. Customize the default user before installing:

```
sudo nano ./ndi_monitor.service
```
Install the service file

```
sudo cp ./ndi_monitor.service /etc/systemd/system/
sudo systemctl enable ndi_monitor.service
sudo systemctl start ndi_monitor.service
```
