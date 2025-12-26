// common.hpp
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

#pragma comment(lib, "Ws2_32.lib")

inline void wsa_init() {
  WSADATA wsa{};
  if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) throw std::runtime_error("WSAStartup failed");
}

inline void set_nodelay(SOCKET s, bool on=true) {
  BOOL v = on ? TRUE : FALSE;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&v, sizeof(v));
}

inline void send_all(SOCKET s, const void* data, size_t len) {
  const char* p = (const char*)data;
  while (len) {
    int n = ::send(s, p, (int)len, 0);
    if (n <= 0) throw std::runtime_error("send failed");
    p += n; len -= (size_t)n;
  }
}

inline void recv_all(SOCKET s, void* data, size_t len) {
  char* p = (char*)data;
  while (len) {
    int n = ::recv(s, p, (int)len, 0);
    if (n <= 0) throw std::runtime_error("recv failed/closed");
    p += n; len -= (size_t)n;
  }
}

struct Frame {
  nlohmann::json j;
  std::vector<uint8_t> bin; // optional
};

inline uint32_t pack_u32(uint32_t v){ return v; } // little-endian on Windows
inline uint64_t pack_u64(uint64_t v){ return v; }

inline Frame recv_frame(SOCKET s) {
  uint32_t magic{}, json_len{};
  uint64_t bin_len{};
  recv_all(s, &magic, sizeof(magic));
  recv_all(s, &json_len, sizeof(json_len));
  recv_all(s, &bin_len, sizeof(bin_len));

  constexpr uint32_t MAGIC = 0x4D52464A; // 'JFRM' little-endian
  if (magic != MAGIC) throw std::runtime_error("bad magic");

  std::vector<uint8_t> jbuf(json_len);
  if (json_len) recv_all(s, jbuf.data(), jbuf.size());
  auto j = nlohmann::json::parse(jbuf.begin(), jbuf.end());

  std::vector<uint8_t> bin((size_t)bin_len);
  if (bin_len) recv_all(s, bin.data(), bin.size());

  return Frame{ std::move(j), std::move(bin) };
}

inline void send_frame(SOCKET s, const nlohmann::json& j, const std::vector<uint8_t>& bin = {}) {
    cv::Mat img(1280, 720, CV_8UC3, cv::Scalar(50, 40, 60));

    std::vector<uint8_t> png;
    cv::imencode(".png", img, png);

  std::string js = j.dump(); // 你也可以 dump(-1,' ',false,nlohmann::json::error_handler_t::replace)
  uint32_t magic = 0x4D52464A; // 'JFRM'
  uint32_t json_len = (uint32_t)js.size();
  uint64_t bin_len  = (uint64_t)bin.size();

  if (bin_len) {
      send_all(s, &magic, sizeof(magic));
      send_all(s, &json_len, sizeof(json_len));
      send_all(s, &bin_len, sizeof(bin_len));
      if (json_len) {
          send_all(s, js.data(), js.size());
      }send_all(s, bin.data(), bin.size());
  }
  else {
      bin_len = (uint64_t)png.size();
      send_all(s, &magic, sizeof(magic));
      send_all(s, &json_len, sizeof(json_len));
      send_all(s, &bin_len, sizeof(bin_len));
      if (json_len) {
          send_all(s, js.data(), js.size());
      }
      send_all(s, png.data(), png.size());
  }
}

std::string WToUtf8(const std::wstring& w);
