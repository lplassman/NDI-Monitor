#ifndef NDI_MONITOR_GPU_OUTPUT_H
#define NDI_MONITOR_GPU_OUTPUT_H

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

class gpu_output {
 public:
  gpu_output() {
    if (SDL_VideoInit("KMSDRM") != 0) fail("Cannot initialize SDL KMS/DRM video");
    video_initialized_ = true;
    try {
      SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
      SDL_DisplayMode mode = {};
      if (SDL_GetDesktopDisplayMode(0, &mode) != 0 || mode.w <= 0 || mode.h <= 0)
        fail("Cannot query KMS/DRM display mode");
      window_ = SDL_CreateWindow("NDI Monitor", SDL_WINDOWPOS_UNDEFINED,
                                 SDL_WINDOWPOS_UNDEFINED, mode.w, mode.h,
                                 SDL_WINDOW_FULLSCREEN_DESKTOP);
      if (!window_) fail("Cannot create KMS/DRM display");
      renderer_ = SDL_CreateRenderer(
          window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
      if (!renderer_) fail("Cannot create accelerated KMS/DRM renderer");
      SDL_RendererInfo info = {};
      if (SDL_GetRendererInfo(renderer_, &info) != 0 ||
          !(info.flags & SDL_RENDERER_ACCELERATED) ||
          !(info.flags & SDL_RENDERER_PRESENTVSYNC)) {
        throw std::runtime_error(
            "KMS/DRM renderer must support GPU acceleration and vertical sync");
      }
      std::printf("Using accelerated KMS/DRM renderer: %s\n", info.name);
      if (SDL_GetRendererOutputSize(renderer_, &width_, &height_) != 0 ||
          width_ <= 0 || height_ <= 0) {
        fail("Cannot query KMS/DRM renderer size");
      }
    } catch (...) {
      release();
      throw;
    }
  }

  ~gpu_output() { release(); }
  gpu_output(const gpu_output&) = delete;
  gpu_output& operator=(const gpu_output&) = delete;

  void render(const uint8_t* pixels, int width, int height, int stride,
              double aspect_ratio) {
    if (!pixels || width <= 0 || height <= 0 ||
        int64_t(stride) < int64_t(width) * 4) {
      throw std::runtime_error("Invalid NDI video frame");
    }
    if (!std::isfinite(aspect_ratio) || aspect_ratio <= 0)
      aspect_ratio = double(width) / height;
    if (!texture_ || texture_width_ != width || texture_height_ != height) {
      if (texture_) SDL_DestroyTexture(texture_);
      texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_BGRA32,
                                   SDL_TEXTUREACCESS_STREAMING, width, height);
      if (!texture_) fail("Cannot create GPU video texture");
      texture_width_ = width;
      texture_height_ = height;
      if (SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_NONE) != 0)
        fail("Cannot configure GPU video texture");
    }
    if (SDL_UpdateTexture(texture_, nullptr, pixels, stride) != 0)
      fail("Cannot upload NDI video frame to GPU");
    if (SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255) != 0 ||
        SDL_RenderClear(renderer_) != 0) {
      fail("Cannot clear GPU display");
    }

    SDL_Rect destination = {0, 0, width_, height_};
    if (double(width_) / height_ > aspect_ratio) {
      destination.w = std::max(
          1, std::min(width_, int(std::lround(height_ * aspect_ratio))));
      destination.x = (width_ - destination.w) / 2;
    } else {
      destination.h = std::max(
          1, std::min(height_, int(std::lround(width_ / aspect_ratio))));
      destination.y = (height_ - destination.h) / 2;
    }
    if (SDL_RenderCopy(renderer_, texture_, nullptr, &destination) != 0)
      fail("Cannot render GPU video frame");
    SDL_RenderPresent(renderer_);
  }

  void clear() {
    if (!renderer_) return;
    if (SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255) == 0 &&
        SDL_RenderClear(renderer_) == 0) {
      SDL_RenderPresent(renderer_);
    }
  }

 private:
  static void fail(const char* message) {
    throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
  }

  void release() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    if (video_initialized_) SDL_VideoQuit();
    texture_ = nullptr;
    renderer_ = nullptr;
    window_ = nullptr;
    video_initialized_ = false;
  }

  bool video_initialized_ = false;
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  int texture_width_ = 0;
  int texture_height_ = 0;
};

#endif
