#ifndef NDI_MONITOR_FRAMEBUFFER_H
#define NDI_MONITOR_FRAMEBUFFER_H

#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

class framebuffer_surface {
 public:
  framebuffer_surface(uint8_t* data, const fb_fix_screeninfo& fixed,
                      const fb_var_screeninfo& variable)
      : data_(data), fixed_(fixed), variable_(variable),
        bytes_per_pixel_(variable.bits_per_pixel / 8) {
    if (!data || fixed.type != FB_TYPE_PACKED_PIXELS ||
        fixed.visual != FB_VISUAL_TRUECOLOR || variable.nonstd ||
        variable.grayscale || (variable.bits_per_pixel != 16 &&
        variable.bits_per_pixel != 24 && variable.bits_per_pixel != 32) ||
        !variable.xres || !variable.yres ||
        uint64_t(variable.xoffset) + variable.xres > variable.xres_virtual ||
        uint64_t(variable.yoffset) + variable.yres > variable.yres_virtual ||
        (uint64_t(variable.xoffset) + variable.xres) * bytes_per_pixel_ >
            fixed.line_length ||
        (uint64_t(variable.yoffset) + variable.yres - 1) * fixed.line_length +
            (uint64_t(variable.xoffset) + variable.xres) * bytes_per_pixel_ >
            fixed.smem_len) {
      throw std::runtime_error("Unsupported or invalid framebuffer layout");
    }
    uint32_t used = 0;
    for (const auto* field : {&variable.red, &variable.green, &variable.blue,
                              &variable.transp}) {
      if (field->msb_right || field->length > 8 ||
          field->offset > variable.bits_per_pixel ||
          field->length > variable.bits_per_pixel - field->offset ||
          (field != &variable.transp && !field->length)) {
        throw std::runtime_error("Unsupported framebuffer color fields");
      }
      if (!field->length) continue;
      const uint32_t mask = ((1u << field->length) - 1) << field->offset;
      if (used & mask) throw std::runtime_error("Overlapping framebuffer color fields");
      used |= mask;
    }
  }

  void clear() {
    for (uint32_t y = 0; y < variable_.yres; ++y)
      std::memset(row(y), 0, size_t(variable_.xres) * bytes_per_pixel_);
  }

  void render(const uint8_t* pixels, int width, int height, int stride,
              double aspect_ratio = 0) {
    if (!pixels || width <= 0 || height <= 0 ||
        int64_t(stride) < int64_t(width) * 4 ||
        uint64_t(height - 1) * uint64_t(stride) + uint64_t(width) * 4 > SIZE_MAX) {
      throw std::runtime_error("Invalid NDI video frame");
    }
    if (!std::isfinite(aspect_ratio) || aspect_ratio <= 0)
      aspect_ratio = double(width) / height;

    uint32_t output_width = variable_.xres;
    uint32_t output_height = variable_.yres;
    if (double(output_width) / output_height > aspect_ratio)
      output_width = std::max(1.0, std::min(double(output_width),
                                          output_height * aspect_ratio));
    else
      output_height = std::max(1.0, std::min(double(output_height),
                                           output_width / aspect_ratio));
    const uint32_t left = (variable_.xres - output_width) / 2;
    const uint32_t top = (variable_.yres - output_height) / 2;
    if (left != last_left_ || top != last_top_ ||
        output_width != last_width_ || output_height != last_height_) {
      clear();
      last_left_ = left;
      last_top_ = top;
      last_width_ = output_width;
      last_height_ = output_height;
    }

    const uint16_t endian_check = 1;
    const bool copy_rows = *reinterpret_cast<const uint8_t*>(&endian_check) == 1 &&
        output_width == uint32_t(width) && output_height == uint32_t(height) &&
        variable_.bits_per_pixel == 32 && variable_.red.offset == 16 &&
        variable_.red.length == 8 && variable_.green.offset == 8 &&
        variable_.green.length == 8 && variable_.blue.offset == 0 &&
        variable_.blue.length == 8 && !variable_.transp.length;
    for (uint32_t y = 0; y < output_height; ++y) {
      const uint8_t* source = pixels +
          size_t(uint64_t(y) * height / output_height) * stride;
      uint8_t* destination = row(top + y) + size_t(left) * bytes_per_pixel_;
      if (copy_rows) {
        std::memcpy(destination, source, size_t(output_width) * 4);
        continue;
      }
      for (uint32_t x = 0; x < output_width; ++x) {
        const uint8_t* pixel = source + size_t(uint64_t(x) * width / output_width) * 4;
        const uint32_t packed = channel(pixel[2], variable_.red) |
            channel(pixel[1], variable_.green) | channel(pixel[0], variable_.blue) |
            channel(255, variable_.transp);
        if (*reinterpret_cast<const uint8_t*>(&endian_check) == 1)
          std::memcpy(destination, &packed, bytes_per_pixel_);
        else
          std::memcpy(destination, reinterpret_cast<const uint8_t*>(&packed) +
                      sizeof(packed) - bytes_per_pixel_, bytes_per_pixel_);
        destination += bytes_per_pixel_;
      }
    }
  }

 private:
  static uint32_t channel(uint8_t value, const fb_bitfield& field) {
    if (!field.length) return 0;
    return ((uint32_t(value) * ((1u << field.length) - 1) + 127) / 255) << field.offset;
  }
  uint8_t* row(uint32_t y) {
    return data_ + size_t(y + variable_.yoffset) * fixed_.line_length +
           size_t(variable_.xoffset) * bytes_per_pixel_;
  }

  uint8_t* data_;
  fb_fix_screeninfo fixed_;
  fb_var_screeninfo variable_;
  size_t bytes_per_pixel_;
  uint32_t last_left_ = 0, last_top_ = 0, last_width_ = 0, last_height_ = 0;
};

class framebuffer {
 public:
  explicit framebuffer(const char* path) {
    fd_ = open(path, O_RDWR | O_CLOEXEC);
    if (fd_ < 0) fail("Cannot open framebuffer");
    try {
      fb_fix_screeninfo fixed = {};
      fb_var_screeninfo variable = {};
      if (ioctl(fd_, FBIOGET_FSCREENINFO, &fixed) < 0 ||
          ioctl(fd_, FBIOGET_VSCREENINFO, &variable) < 0)
        fail("Cannot query framebuffer");
      size_ = fixed.smem_len;
      mapping_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
      if (mapping_ == MAP_FAILED) fail("Cannot map framebuffer");
      surface_.reset(new framebuffer_surface(static_cast<uint8_t*>(mapping_), fixed, variable));
      surface_->clear();
    } catch (...) {
      release();
      throw;
    }
  }
  ~framebuffer() { release(); }
  framebuffer(const framebuffer&) = delete;
  framebuffer& operator=(const framebuffer&) = delete;

  void render(const uint8_t* pixels, int width, int height, int stride, double aspect) {
    surface_->render(pixels, width, height, stride, aspect);
  }
  void clear() { surface_->clear(); }

 private:
  static void fail(const char* message) {
    throw std::runtime_error(std::string(message) + ": " + std::strerror(errno));
  }
  void release() {
    if (mapping_ != MAP_FAILED) munmap(mapping_, size_);
    if (fd_ >= 0) close(fd_);
  }
  int fd_ = -1;
  void* mapping_ = MAP_FAILED;
  size_t size_ = 0;
  std::unique_ptr<framebuffer_surface> surface_;
};

#endif
