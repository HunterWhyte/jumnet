#pragma once

#include <cstddef>
#include "rtc/rtc.hpp"
#include "stdlib.h"
#include "log.h"

#define MAX_DESCRIPTION_LEN 512
#define MAX_MID_LEN 128
#define MAX_ID_LEN 32

typedef enum : char { LIST, CONNECT, NOT_FOUND } COMMAND;
typedef enum : char { UNSPEC, OFFER, ANSWER, PRANSWER, ROLLBACK, CANDIDATE } TYPE;

struct ServerPacket {
  COMMAND command;
  TYPE type;
  char description[MAX_DESCRIPTION_LEN] = {};
  char mid[MAX_MID_LEN] = {};
  char id[MAX_ID_LEN] = {};
  char candidate[MAX_DESCRIPTION_LEN] = {};
};

struct DataCallback {
  void* userdata;
  void (*func)(void* userdata, void* data, int len);
};

struct net {
  std::string local_id;
  std::unordered_map<std::string, std::shared_ptr<rtc::PeerConnection>> peers;
  std::unordered_map<std::string, std::shared_ptr<rtc::DataChannel>> data_channels;
  rtc::WebSocket ws;
  rtc::Configuration config;
  DataCallback data_callback;
};

void net_init(net* n, DataCallback callback, LoggerCallback log);

void net_sendTo(net* n, std::string id, void* data, int len);
void net_sendTo(net* n, std::string id, std::string s);
void net_sendAll(net* n, void* data, int len);
void net_sendAll(net* n, std::string s);

void net_requestList(net* n);
void net_initiateConnection(net* n, std::string id);
void net_connectToServer(net* n, const std::string server, const std::string id);