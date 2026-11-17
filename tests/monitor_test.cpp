#define main ndi_monitor_main
#include "../ndi_monitor.cpp"
#undef main
#include <chrono>
#include <cstdarg>

static const int fake_fd = 100000;
static uint8_t screen[16];
static uint8_t video_pixels[] = {
  0, 0, 255, 0, 0, 255, 0, 0,
  255, 0, 0, 0, 255, 255, 255, 0
};
static int receiver_token;
static int finder_token;
static int creates = 0, destroys = 0, opens = 0, closes = 0, unmaps = 0;
static bool create_fails = false, query_fails = false, map_fails = false;
static std::atomic<int> frames_freed{0};
static std::atomic<int> captures{0};
enum class capture_mode { video, bad_format, bad_stride, error };
static capture_mode mode = capture_mode::video;
static NDIlib_source_t sources[] = {
  {"Test source", "127.0.0.1:5960"},
  {"Second source", "127.0.0.1:5961"}
};

extern "C" int __real_open(const char*, int, ...);
extern "C" int __real_close(int);
extern "C" int __real_ioctl(int, unsigned long, ...);
extern "C" void* __real_mmap(void*, size_t, int, int, int, off_t);
extern "C" int __real_munmap(void*, size_t);

extern "C" int __wrap_open(const char* path, int flags, ...) {
  if (std::strcmp(path, "/test-fb") == 0) {
    assert(flags == (O_RDWR | O_CLOEXEC));
    ++opens;
    return fake_fd;
  }
  if (flags & O_CREAT) {
    va_list args;
    va_start(args, flags);
    const auto permissions = va_arg(args, int);
    va_end(args);
    return __real_open(path, flags, permissions);
  }
  return __real_open(path, flags);
}

extern "C" int __wrap_close(int fd) {
  if (fd != fake_fd) return __real_close(fd);
  ++closes;
  return 0;
}

extern "C" int __wrap_ioctl(int fd, unsigned long request, ...) {
  va_list args;
  va_start(args, request);
  void* value = va_arg(args, void*);
  va_end(args);
  if (fd != fake_fd) return __real_ioctl(fd, request, value);
  if (query_fails) { errno = EIO; return -1; }
  if (request == FBIOGET_FSCREENINFO) {
    auto* fixed = static_cast<fb_fix_screeninfo*>(value);
    *fixed = {};
    fixed->type = FB_TYPE_PACKED_PIXELS;
    fixed->visual = FB_VISUAL_TRUECOLOR;
    fixed->line_length = 8;
    fixed->smem_len = sizeof(screen);
  } else {
    assert(request == FBIOGET_VSCREENINFO);
    auto* variable = static_cast<fb_var_screeninfo*>(value);
    *variable = {};
    variable->xres = variable->xres_virtual = 2;
    variable->yres = variable->yres_virtual = 2;
    variable->bits_per_pixel = 32;
    variable->red = {16, 8, 0};
    variable->green = {8, 8, 0};
    variable->blue = {0, 8, 0};
  }
  return 0;
}

extern "C" void* __wrap_mmap(void* address, size_t size, int protection,
                           int flags, int fd, off_t offset) {
  if (fd != fake_fd) return __real_mmap(address, size, protection, flags, fd, offset);
  assert(size == sizeof(screen) && flags == MAP_SHARED && offset == 0);
  if (map_fails) { errno = ENOMEM; return MAP_FAILED; }
  return screen;
}

extern "C" int __wrap_munmap(void* address, size_t size) {
  if (address != screen) return __real_munmap(address, size);
  assert(size == sizeof(screen));
  ++unmaps;
  return 0;
}

extern "C" bool NDIlib_initialize() { return true; }
extern "C" void NDIlib_destroy() {}
extern "C" NDIlib_find_instance_t NDIlib_find_create_v2(const NDIlib_find_create_t*) {
  return reinterpret_cast<NDIlib_find_instance_t>(&finder_token);
}
extern "C" void NDIlib_find_destroy(NDIlib_find_instance_t) {}
extern "C" const NDIlib_source_t* NDIlib_find_get_current_sources(
    NDIlib_find_instance_t, uint32_t* count) {
  *count = 2;
  return sources;
}

extern "C" NDIlib_recv_instance_t NDIlib_recv_create_v3(
    const NDIlib_recv_create_v3_t* settings) {
  assert(settings->color_format == NDIlib_recv_color_format_BGRX_BGRA);
  assert(settings->bandwidth == NDIlib_recv_bandwidth_highest);
  assert(!settings->allow_video_fields);
  assert(std::strcmp(settings->source_to_connect_to.p_ndi_name, "Test source") == 0);
  ++creates;
  captures = 0;
  return create_fails ? nullptr : reinterpret_cast<NDIlib_recv_instance_t>(&receiver_token);
}

extern "C" void NDIlib_recv_destroy(NDIlib_recv_instance_t receiver) {
  assert(receiver == reinterpret_cast<NDIlib_recv_instance_t>(&receiver_token));
  ++destroys;
}

extern "C" NDIlib_frame_type_e NDIlib_recv_capture_v3(
    NDIlib_recv_instance_t, NDIlib_video_frame_v2_t* video,
    NDIlib_audio_frame_v3_t* audio, NDIlib_metadata_frame_t* metadata, uint32_t timeout) {
  assert(!audio && !metadata && timeout <= 100);
  if (captures++ == 0) {
    if (mode == capture_mode::error) return NDIlib_frame_type_error;
    video->xres = video->yres = 2;
    video->line_stride_in_bytes = mode == capture_mode::bad_stride ? 1 : 8;
    video->picture_aspect_ratio = 1;
    video->p_data = video_pixels;
    video->FourCC = mode == capture_mode::bad_format
        ? NDIlib_FourCC_video_type_UYVY : NDIlib_FourCC_video_type_BGRX;
    return NDIlib_frame_type_video;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  return NDIlib_frame_type_none;
}

extern "C" void NDIlib_recv_free_video_v2(
    NDIlib_recv_instance_t, const NDIlib_video_frame_v2_t* frame) {
  assert(frame->p_data == video_pixels);
  ++frames_freed;
}

template <typename Predicate>
static void wait_for(Predicate ready) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!ready() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  assert(ready());
}

static void command(mg_connection& client, const char* json) {
  mg_ws_message message = {};
  message.data.ptr = json;
  message.data.len = std::strlen(json);
  fn(&client, MG_EV_WS_MSG, &message, nullptr);
}

int main() {
  framebuffer_path = "/test-fb";
  {
    monitor receiver("Test source");
    wait_for([] { return frames_freed == 1; });
    assert(!receiver.failed());
    assert(std::memcmp(screen, video_pixels, sizeof(screen)) == 0);
  }
  assert(destroys == 1 && closes == 1 && unmaps == 1);
  for (auto byte : screen) assert(byte == 0);

  for (auto failure : {capture_mode::bad_format, capture_mode::bad_stride, capture_mode::error}) {
    const int previous_frees = frames_freed;
    mode = failure;
    {
      monitor receiver("Test source");
      wait_for([&] { return receiver.failed(); });
    }
    assert(frames_freed == previous_frees + (failure == capture_mode::error ? 0 : 1));
    for (auto byte : screen) assert(byte == 0);
  }
  const int previous_destroys = destroys;
  create_fails = true;
  try { monitor receiver("Test source"); assert(false); }
  catch (const std::runtime_error&) {}
  assert(destroys == previous_destroys && opens == closes);
  create_fails = false;
  query_fails = true;
  try { monitor receiver("Test source"); assert(false); }
  catch (const std::runtime_error&) {}
  assert(opens == closes);
  query_fails = false;
  map_fails = true;
  try { monitor receiver("Test source"); assert(false); }
  catch (const std::runtime_error&) {}
  assert(opens == closes);
  map_fails = false;
  mode = capture_mode::video;

  mg_mgr_init(&mgr);
  mg_connection client = {};
  client.mgr = &mgr;
  command(client, "{\"prefix\":\"refresh\",\"action\":\"refresh\"}");
  assert(discovered_sources.size() == 2);
  // The finder owns its strings; selections must retain a copy after a refresh.
  sources[0].p_ndi_name = "Changed finder storage";
  command(client, "{\"prefix\":\"connect_source\",\"action\":\"0\"}");
  assert(p_monitors[0] && ndi_running_name[0] == "Test source");
  const int previous_creates = creates;
  command(client, "{\"prefix\":\"connect_source\",\"action\":\"0\"}");
  command(client, "{\"prefix\":\"connect_source\",\"action\":\"1\"}");
  assert(creates == previous_creates);
  command(client, "{\"prefix\":\"disconnect_source\",\"action\":\"0\"}");
  assert(!p_monitors[0] && ndi_running_name[0].empty());
  command(client, "{\"prefix\":\"disconnect_source\",\"action\":\"0\"}");
  command(client, "{\"prefix\":\"connect_source\",\"action\":\"999999999999999999999999\"}");
  command(client, "{\"prefix\":\"disconnect_source\",\"action\":\"-1\"}");
  command(client, "{\"prefix\":\"connect_source\"}");
  assert(!p_monitors[0] && opens == closes);
  create_fails = true;
  command(client, "{\"prefix\":\"connect_source\",\"action\":\"0\"}");
  assert(!p_monitors[0] && ndi_running_name[0].empty() && opens == closes);
  assert(std::string(reinterpret_cast<char*>(client.send.buf), client.send.len)
             .find("\"prefix\":\"error\"") != std::string::npos);
  mg_iobuf_free(&client.send);
  mg_mgr_free(&mgr);
  std::cout << "Monitor lifecycle and source control tests passed\n";
}
