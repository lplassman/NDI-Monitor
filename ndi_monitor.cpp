/*
 * NDI Source Monitor
 * 
 * This program can be used and distrubuted without resrictions
 */

#include <cassert>
#include <cstdio>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <math.h>
#include <signal.h>
#include <ctime>
#include <atomic>
#include <unistd.h>
#include <fstream> //for reading and writing preset file
#include <mongoose.h>
#include "mjson.h"
#include <thread>
#include <vector>
#include "framebuffer.h"

#include <getopt.h> 
#include <condition_variable>

#include <Processing.NDI.Lib.h>

NDIlib_find_create_t NDI_find_create_desc; /* Default settings for NDI find */
NDIlib_find_instance_t pNDI_find;
const NDIlib_source_t* p_sources = NULL;
struct mg_mgr mgr;   

//Function Definitions
std::string convertToString(char* a);

static std::string framebuffer_path = "/dev/fb0";
static volatile sig_atomic_t stopping = 0;
struct discovered_source {
  std::string name;
  std::string url;
};
static std::vector<discovered_source> discovered_sources;


struct monitor {
 monitor(const char* source, const char* url = nullptr); //constructor
 ~monitor(void); //destructor 
 bool failed() const { return m_failed; }
 private:	
  void receive();
  framebuffer m_framebuffer;
	NDIlib_recv_instance_t m_pNDI_recv; // Create the receiver
	std::atomic<bool> m_exit;	// Are we ready to exit
  std::atomic<bool> m_failed{false};
  std::thread m_thread;
};




//Constructor
monitor::monitor(const char* source, const char* url)
    : m_framebuffer(framebuffer_path.c_str()), m_pNDI_recv(NULL), m_exit(false) {
  printf("Starting Monitor for %s\n", source);
  NDIlib_recv_create_v3_t settings = {};
  settings.source_to_connect_to.p_ndi_name = source;
  settings.source_to_connect_to.p_url_address = url;
  settings.color_format = NDIlib_recv_color_format_BGRX_BGRA;
  settings.bandwidth = NDIlib_recv_bandwidth_highest;
  settings.allow_video_fields = false;
  m_pNDI_recv = NDIlib_recv_create_v3(&settings);
  if (!m_pNDI_recv) throw std::runtime_error("Cannot create NDI receiver");
  try {
    m_thread = std::thread(&monitor::receive, this);
  } catch (...) {
    NDIlib_recv_destroy(m_pNDI_recv);
    throw;
  }
}

void monitor::receive() {
  while (!m_exit) {
    NDIlib_video_frame_v2_t video = {};
    const auto type = NDIlib_recv_capture_v3(m_pNDI_recv, &video, nullptr, nullptr, 100);
    if (type == NDIlib_frame_type_video) {
      try {
        if (video.FourCC != NDIlib_FourCC_video_type_BGRX &&
            video.FourCC != NDIlib_FourCC_video_type_BGRA)
          throw std::runtime_error("NDI receiver returned an unsupported pixel format");
        m_framebuffer.render(video.p_data, video.xres, video.yres,
                             video.line_stride_in_bytes, video.picture_aspect_ratio);
      } catch (const std::exception& error) {
        fprintf(stderr, "Monitor: %s\n", error.what());
        m_failed = true;
      }
      NDIlib_recv_free_video_v2(m_pNDI_recv, &video);
      if (m_failed) break;
    } else if (type == NDIlib_frame_type_error) {
      fprintf(stderr, "NDI receiver capture failed\n");
      m_failed = true;
      break;
    }
  }
}

// Destructor
monitor::~monitor(void){	// Wait for the thread to exit
	m_exit = true;
  if (m_thread.joinable()) m_thread.join();
  NDIlib_recv_destroy(m_pNDI_recv);
  m_framebuffer.clear();
}


static const int no_monitors = 1; //max number of monitor instances
monitor* p_monitors[no_monitors] = { 0 };
std::string ndi_running_name[no_monitors] = { "" };

static void report_error(struct mg_connection* c, const char* message) {
  fprintf(stderr, "%s\n", message);
  char* json = mjson_aprintf("{\"prefix\":\"error\",\"message\":%Q}", message);
  if (json) {
    mg_ws_send(c, json, strlen(json), WEBSOCKET_OP_TEXT);
    free(json);
  }
}

static bool parse_id(const std::string& text, size_t limit, size_t& id) {
  if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
    return false;
  errno = 0;
  char* end = nullptr;
  const unsigned long value = strtoul(text.c_str(), &end, 10);
  if (errno || *end || value >= limit) return false;
  id = value;
  return true;
}

static void fn(struct mg_connection *c, int ev, void *ev_data, void *fn_data){
  if(ev == MG_EV_WS_OPEN){
    c->label[0] = 'W';  // Mark this connection as an established WS client
  }
  if (ev == MG_EV_HTTP_MSG){
  struct mg_http_message *hm = (struct mg_http_message *) ev_data;
  struct mg_connection *c2 = mgr.conns;
   if(mg_http_match_uri(hm, "/ws")){ //upgrade to WebSocket
      mg_ws_upgrade(c, hm, NULL);
   }else if(mg_http_match_uri(hm, "/rest")) { //handle REST events
      mg_http_reply(c, 200, "", "{\"result\": %d}\n", 123);
   }else{ // Serve static files
      struct mg_http_serve_opts opts = {.root_dir = "/opt/ndi_monitor/assets/"};
      mg_http_serve_dir(c, (mg_http_message*)ev_data, &opts);
   }
  }else if (ev == MG_EV_WS_MSG){
    // Got websocket frame. Received data is wm->data. Echo it back!
    struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
    //std::cout << "WebSocket: " << wm->data.ptr << std::endl;
    char prefix_buf[100] = {};
    char action_buf[100] = {};
    if (mjson_get_string(wm->data.ptr, wm->data.len, "$.prefix", prefix_buf, sizeof(prefix_buf)) < 0 ||
        mjson_get_string(wm->data.ptr, wm->data.len, "$.action", action_buf, sizeof(action_buf)) < 0) {
      report_error(c, "Invalid source command");
      return;
    }
    std::string prefix_string = convertToString(prefix_buf);
    std::string action_string = convertToString(action_buf);
    if(prefix_string == "refresh"){
     if(action_string == "refresh"){

      uint32_t no_sources = 0; 
      p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);
      discovered_sources.clear();
      for (uint32_t i = 0; i < no_sources; ++i)
        discovered_sources.push_back({p_sources[i].p_ndi_name,
                                      p_sources[i].p_url_address ? p_sources[i].p_url_address : ""});
      std::string discover_json;
      std::string source_json = "";
      discover_json = "{\"prefix\":\"discover_source\",\"action\":\"display\",\"source_list\":{";
      for(uint32_t i = 0; i < no_sources; i++){
       std::string ndi_string = discovered_sources[i].name;
       std::string url_string = discovered_sources[i].url;
       std::string source_id = std::to_string(i); 
       int conflict = 0;
       for(uint32_t i = 0; i < no_monitors; i++){ //check for conflicts
        if((ndi_running_name[i] == ndi_string)&&(conflict == 0)){
         conflict = 1; //found conflict with a name that is already stored - already running this receiver
        }  
       }
       if(conflict == 0){ //since this is not running on a receiver - display
        //std::cout << "Source IP: " << p_sources[i].p_url_address << std::endl;
        if(source_json == ""){
         source_json += "\""+source_id + "\":{\"name\":\""+ndi_string+"\",\"url\":\""+url_string+"\"}";  
        }else{
         source_json += ",\""+source_id + "\":{\"name\":\""+ndi_string+"\",\"url\":\""+url_string+"\"}";  
        }
       }
      }
      discover_json += source_json;
      discover_json += "}";
      discover_json += "}";
      const char* pub_json1 = discover_json.c_str();
      for (struct mg_connection *c2 = mgr.conns; c2 != NULL; c2 = c2->next) { //traverse over all client connections
       if (c2->label[0] == 'W'){ //make sure it is a websocket connection
        mg_ws_send(c2, pub_json1, strlen(pub_json1), WEBSOCKET_OP_TEXT);
       }
      }

      std::string connected_json;
      source_json = "";
      connected_json = "{\"prefix\":\"playing_source\",\"action\":\"display\",\"source_list\":{";
      for(uint32_t i = 0; i < no_monitors; i++){
       if(ndi_running_name[i] != ""){ //make sure receiver is not empty
        std::string source_id = std::to_string(i); 
        if(source_json == ""){
         source_json += "\""+source_id + "\":{\"name\":\""+ndi_running_name[i]+"\"}";  
        }else{
         source_json += ",\""+source_id + "\":{\"name\":\""+ndi_running_name[i]+"\"}";  
        }
       }
      }
      connected_json += source_json;
      connected_json += "}";
      connected_json += "}";
      const char* pub_json2 = connected_json.c_str();
      for (struct mg_connection *c2 = mgr.conns; c2 != NULL; c2 = c2->next) { //traverse over all client connections
       if (c2->label[0] == 'W'){ //make sure it is a websocket connection
        mg_ws_send(c2, pub_json2, strlen(pub_json2), WEBSOCKET_OP_TEXT);
       }
      }
     } 
    }

    if(prefix_string == "connect_source"){
     size_t source_id;
     if (!parse_id(action_string, discovered_sources.size(), source_id)) {
       report_error(c, "Invalid or stale NDI source ID; refresh sources");
       return;
     }
     int stored = 0;
     int monitor_id = 0;
     int conflict = 0;
     std::string ndi_string = discovered_sources[source_id].name;
     for(uint32_t i = 0; i < no_monitors; i++){ //check for conflicts
      if((ndi_running_name[i] == ndi_string)&&(conflict == 0)){
       conflict = 1; //found conflict with a name that is already stored - already running this monitor
      }  
     }
     if(conflict == 0){
      for(uint32_t i = 0; i < no_monitors; i++){
       if(stored == 0){
        if(ndi_running_name[i] == ""){ //empty string array
         std::cout << "ID: " << i << std::endl;
         stored = 1;
         monitor_id = i;
        } 
       }
      }
      if (!stored) {
        report_error(c, "Stop the current monitor before connecting another source");
        return;
      }
      try {
        const auto& source = discovered_sources[source_id];
        std::unique_ptr<monitor> instance(new monitor(
            source.name.c_str(), source.url.empty() ? nullptr : source.url.c_str()));
        ndi_running_name[monitor_id] = ndi_string;
        p_monitors[monitor_id] = instance.release();
      } catch (const std::exception& error) {
        report_error(c, error.what());
      }
     }else{
      //std::cout << "Monitor already running for:  " << p_sources[source_id].p_ndi_name << std::endl; 
     }
    }

    if(prefix_string == "disconnect_source"){
     size_t source_id;
     if (!parse_id(action_string, no_monitors, source_id)) {
       report_error(c, "Invalid monitor ID");
       return;
     }
     delete p_monitors[source_id]; //delete receiver
     p_monitors[source_id] = nullptr;
     ndi_running_name[source_id] = ""; //update the running receiver
    }

    if(prefix_string == "save_streams"){
     std::ofstream preset_file("/opt/ndi_monitor/assets/presets.txt");
     for(uint32_t i = 0; i < no_monitors; i++){
      if(ndi_running_name[i] != ""){ //make sure a receiver is stored before trying to save in file
      preset_file << ndi_running_name[i];
      preset_file << std::endl;
      }
     }
     preset_file.close();
    }
    
  }
}

static void usage(FILE *fp, const char* program){
        fprintf(fp,
                 "Usage: %s [options]\n\n"
                 "Version 1.0\n"
                 "Options:\n"
                 "-h | --help          Print this message\n"
                 "-f | --framebuffer   Framebuffer device (default /dev/fb0)\n"
                 "",
                 program);
}

static const char short_options[] = "hf:";

static const struct option
long_options[] = {
        { "help",   no_argument,       NULL, 'h' },
        { "framebuffer", required_argument, NULL, 'f' },
        { 0, 0, 0, 0 }
};

int main (int argc, char *argv[]){
  for (;;) {
   int idx;
   int c;
   c = getopt_long(argc, argv,short_options, long_options, &idx);
   if (-1 == c){
    break;
   }
   switch(c){
    case 'h':
     usage(stdout, argv[0]);
     exit(EXIT_SUCCESS);          
    case 'f':
     framebuffer_path = optarg;
     break;
    default:
     usage(stderr, argv[0]);
     exit(EXIT_FAILURE);
   }
  }

  if(!NDIlib_initialize()){	
	 fprintf(stderr, "Cannot initialize NDI.\n");
	 return EXIT_FAILURE;
	}

	// Create a NDI finder	
  NDI_find_create_desc.show_local_sources = (bool)false; //don't include local sources when searching for NDI
	pNDI_find = NDIlib_find_create_v2(&NDI_find_create_desc);
	if (!pNDI_find) {
    fprintf(stderr, "Cannot create NDI finder.\n");
    NDIlib_destroy();
    return EXIT_FAILURE;
  }

  signal(SIGINT, [](int) { stopping = 1; });
  signal(SIGTERM, [](int) { stopping = 1; });

  std::string output_text; //preset file is temporary stored in this variable
  std::ifstream preset_file("/opt/ndi_monitor/assets/presets.txt"); //open the presets file
  while(getline(preset_file, output_text)){
   if (output_text.empty()) continue;
   int stored = 0;
   int monitor_id = 0;
   const char* ndi_name = output_text.c_str();;
   std::string ndi_string = ndi_name;
   for(uint32_t i = 0; i < no_monitors; i++){
    if(stored == 0){
     if(ndi_running_name[i] == ""){ //empty string array - make sure it is empty before trying to start receiver
      stored = 1;
      monitor_id = i;
     } 
    }
   }
   if (!stored) {
    fprintf(stderr, "Only one framebuffer monitor is supported; ignoring extra presets\n");
    break;
   }
   try {
    std::unique_ptr<monitor> instance(new monitor(ndi_name));
    ndi_running_name[monitor_id] = ndi_string;
    p_monitors[monitor_id] = instance.release();
   } catch (const std::exception& error) {
    fprintf(stderr, "Cannot restore preset: %s\n", error.what());
   }
  }
                               
  mg_mgr_init(&mgr);
  const bool listening = mg_http_listen(&mgr, "http://0.0.0.0:80", fn, NULL) != nullptr;
  if (!listening) fprintf(stderr, "Cannot listen on port 80\n");
  while (listening && !stopping) {
   mg_mgr_poll(&mgr, 100);
   for (uint32_t i = 0; i < no_monitors; ++i) {
    if (p_monitors[i] && p_monitors[i]->failed()) {
     for (struct mg_connection* client = mgr.conns; client; client = client->next)
      if (client->label[0] == 'W') report_error(client, "NDI monitor failed; see service logs");
     delete p_monitors[i];
     p_monitors[i] = nullptr;
     ndi_running_name[i].clear();
    }
   }
  }
  for (auto* instance : p_monitors) delete instance;
  mg_mgr_free(&mgr);
  NDIlib_find_destroy(pNDI_find);
  NDIlib_destroy();
  return listening ? EXIT_SUCCESS : EXIT_FAILURE;
}

std::string convertToString(char* a){
  std::string s = a;
  return s;
}
