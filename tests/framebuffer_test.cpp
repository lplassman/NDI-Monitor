#include "framebuffer.h"
#include <cassert>
#include <iostream>
#include <vector>

struct display {
  fb_fix_screeninfo fixed = {};
  fb_var_screeninfo variable = {};
  std::vector<uint8_t> memory;

  display(unsigned width, unsigned height, unsigned bits, unsigned padding = 0,
          unsigned xoffset = 0, unsigned yoffset = 0) {
    variable.xres = width;
    variable.yres = height;
    variable.xoffset = xoffset;
    variable.yoffset = yoffset;
    variable.xres_virtual = width + xoffset;
    variable.yres_virtual = height + yoffset;
    variable.bits_per_pixel = bits;
    variable.red = {bits == 16 ? 11u : 16u, bits == 16 ? 5u : 8u, 0};
    variable.green = {bits == 16 ? 5u : 8u, bits == 16 ? 6u : 8u, 0};
    variable.blue = {0, bits == 16 ? 5u : 8u, 0};
    fixed.type = FB_TYPE_PACKED_PIXELS;
    fixed.visual = FB_VISUAL_TRUECOLOR;
    fixed.line_length = variable.xres_virtual * (bits / 8) + padding;
    fixed.smem_len = fixed.line_length * variable.yres_virtual;
    memory.resize(fixed.smem_len, 0xa5);
  }

  uint32_t pixel(unsigned x, unsigned y) const {
    uint32_t value = 0;
    std::memcpy(&value, &memory[(y + variable.yoffset) * fixed.line_length +
                (x + variable.xoffset) * (variable.bits_per_pixel / 8)],
                variable.bits_per_pixel / 8);
    return value;
  }
};

template <typename Function>
void expect_error(Function fn) {
  bool failed = false;
  try { fn(); } catch (const std::runtime_error&) { failed = true; }
  assert(failed);
}

int main() {
  const uint8_t colors[] = {
    0, 0, 255, 0, 0, 255, 0, 0, 99, 99, 99, 99,
    255, 0, 0, 0, 255, 255, 255, 0, 99, 99, 99, 99
  };
  for (unsigned bits : {16u, 24u, 32u}) {
    display output(2, 2, bits, 7, 1, 1);
    framebuffer_surface surface(output.memory.data(), output.fixed, output.variable);
    surface.render(colors, 2, 2, 12);
    assert(output.pixel(0, 0) == (bits == 16 ? 0xf800u : 0xff0000u));
    assert(output.pixel(1, 0) == (bits == 16 ? 0x07e0u : 0x00ff00u));
    assert(output.pixel(0, 1) == (bits == 16 ? 0x001fu : 0x0000ffu));
    assert(output.pixel(1, 1) == (bits == 16 ? 0xffffu : 0xffffffu));
    for (unsigned y = 0; y < output.variable.yres_virtual; ++y) {
      for (unsigned x = 0; x < output.fixed.line_length; ++x) {
        const bool visible = y >= 1 && x >= bits / 8 && x < 3 * (bits / 8);
        if (!visible) assert(output.memory[y * output.fixed.line_length + x] == 0xa5);
      }
    }
    surface.clear();
    assert(output.pixel(0, 0) == 0 && output.pixel(1, 1) == 0);
    expect_error([&] { surface.render(nullptr, 2, 2, 8); });
    expect_error([&] { surface.render(colors, 2, 2, 7); });
    expect_error([&] { surface.render(colors, 0, 2, 8); });
    expect_error([&] { surface.render(colors, 2, -1, 8); });
  }

  display alpha(2, 2, 32);
  alpha.variable.transp = {24, 8, 0};
  framebuffer_surface alpha_surface(alpha.memory.data(), alpha.fixed, alpha.variable);
  alpha_surface.render(colors, 2, 2, 12);
  assert(alpha.pixel(0, 0) == 0xffff0000);
  std::swap(alpha.variable.red, alpha.variable.blue);
  framebuffer_surface rgb_surface(alpha.memory.data(), alpha.fixed, alpha.variable);
  rgb_surface.render(colors, 2, 2, 12);
  assert(alpha.pixel(0, 0) == 0xff0000ff);

  display scaled(4, 4, 32);
  framebuffer_surface surface(scaled.memory.data(), scaled.fixed, scaled.variable);
  surface.render(colors, 2, 2, 12, 2.0);
  assert(scaled.pixel(0, 0) == 0 && scaled.pixel(3, 3) == 0);
  assert(scaled.pixel(0, 1) == 0xff0000 && scaled.pixel(1, 1) == 0xff0000);
  assert(scaled.pixel(2, 2) == 0xffffff);
  surface.render(colors, 2, 2, 12, 0.5);
  assert(scaled.pixel(0, 1) == 0 && scaled.pixel(3, 1) == 0);
  assert(scaled.pixel(1, 0) == 0xff0000 && scaled.pixel(2, 3) == 0xffffff);
  surface.render(colors, 2, 2, 12);
  assert(scaled.pixel(0, 0) == 0xff0000 && scaled.pixel(3, 3) == 0xffffff);
  surface.render(colors, 2, 2, 12, NAN);
  assert(scaled.pixel(3, 3) == 0xffffff);
  surface.render(colors, 2, 2, 12, 1e100);
  assert(scaled.pixel(0, 0) == 0);

  display downscaled(1, 1, 16);
  framebuffer_surface small(downscaled.memory.data(), downscaled.fixed, downscaled.variable);
  small.render(colors, 2, 2, 12);
  assert(downscaled.pixel(0, 0) == 0xf800);

  display invalid(2, 2, 32);
  const auto check_layout = [&](fb_fix_screeninfo fixed, fb_var_screeninfo variable) {
    expect_error([&] { framebuffer_surface bad(invalid.memory.data(), fixed, variable); });
  };
  auto fixed = invalid.fixed;
  fixed.smem_len = 15;
  check_layout(fixed, invalid.variable);
  fixed = invalid.fixed;
  fixed.line_length = 7;
  check_layout(fixed, invalid.variable);
  fixed = invalid.fixed;
  fixed.visual = FB_VISUAL_PSEUDOCOLOR;
  check_layout(fixed, invalid.variable);
  auto variable = invalid.variable;
  variable.xoffset = 1;
  check_layout(invalid.fixed, variable);
  variable = invalid.variable;
  variable.red.offset = 32;
  check_layout(invalid.fixed, variable);
  variable = invalid.variable;
  variable.red.offset = 8;
  check_layout(invalid.fixed, variable);
  variable = invalid.variable;
  variable.red.msb_right = 1;
  check_layout(invalid.fixed, variable);
  variable = invalid.variable;
  variable.bits_per_pixel = 8;
  check_layout(invalid.fixed, variable);
  expect_error([&] { framebuffer missing("/nonexistent/ndi-monitor-framebuffer"); });
  expect_error([&] { framebuffer not_fb("/dev/null"); });
  std::cout << "Framebuffer tests passed\n";
}
