#include "net.h"
#include "string.h"

std::shared_ptr<rtc::PeerConnection> createPeerConnection(net* n, std::string id);
void receiveConnection(net* n, ServerPacket message);
void setupDataChannel(net* n, std::shared_ptr<rtc::DataChannel> dc);

const char* type_strings[] = {"unspec", "offer", "answer", "pranswer", "rollback", "candidate"};

void net_init(net* n, DataCallback callback, LoggerCallback log) {
#ifndef __EMSCRIPTEN__
  rtc::InitLogger(rtc::LogLevel::Info);
#endif
  std::string stunServer = "stun:stun4.l.google.com:19302";
  n->config.iceServers.emplace_back(stunServer);

  // TODO check that a valid function has been provided for the callbacks
  n->log = log;
  n->data_callback = callback;

  n->ws.onMessage([n](rtc::message_variant data) {
    LOG(n->log, "[server] received packet\n");
    ServerPacket message;
    std::vector<std::byte> vector_data = std::get<rtc::binary>(data);
    memcpy(&message, &vector_data[0], sizeof(message));

    if (message.command == CONNECT) {
      receiveConnection(n, message);
    } else if (message.command == NOT_FOUND) {
      LOG(n->log, "[error] provided ID not found\n");
    } else if (message.command == LIST) {
      LOG(n->log, "ID list received %s\n", message.description);
    } else {
      LOG(n->log, "[error] received unknown command from server\n");
    }
  });

  n->ws.onOpen([n]() { LOG(n->log, "[server] connection established\n"); });

  n->ws.onError(
      [n](std::string e) { LOG(n->log, "[error] failed to connect to server: %s\n", e.c_str()); });

  n->ws.onClosed([n]() { LOG(n->log, "socket to signalling server closed\n"); });
}

void net_connectToServer(net* n, const std::string server, const std::string id){
  if(id.length() > MAX_ID_LEN){
    LOG(n->log, "ID too long\n");
    return;
  }
  n->local_id = id;
  const std::string url =  server + n->local_id;
  LOG(n->log, "connecting to server at '%s'...\n", url.c_str());
  n->ws.open(url);
}

void setupDataChannel(net* n, std::shared_ptr<rtc::DataChannel> dc) {
  dc->onOpen([n, dc]() {
    LOG(n->log, "DataChannel open %s\n", dc->label().c_str());
    dc->send(n->local_id + " joined...");
  });

  dc->onClosed([n, dc]() { LOG(n->log, "data channel with %s closed\n", dc->label().c_str()); });

  dc->onMessage([n, dc](rtc::message_variant data) {
    if (std::holds_alternative<std::string>(data)) {
      // string message TODO: let user define another callback for string messages
      LOG(n->log, "[message] %s: %s\n", dc->label().c_str(), std::get<std::string>(data).c_str());
    } else {
      // binary message
      // convert vector of bytes that we get from libdatachannel to a raw pointer
      std::vector<std::byte> vector_data = std::get<rtc::binary>(data);
      // TODO: have fixed size buffering instead of doing dynamic alloaction constantly
      void* raw_data = malloc(vector_data.size() * sizeof(std::byte));
      memcpy(raw_data, &vector_data[0], vector_data.size() * sizeof(std::byte));
      n->data_callback.func(n->data_callback.userdata, raw_data,
                            vector_data.size() * sizeof(std::byte));
    }
  });
}

// on receive request for a peer connection over websocket
void receiveConnection(net* n, ServerPacket message) {

  std::string id = std::string(message.id);

  std::shared_ptr<rtc::PeerConnection> pc;
  auto jt = n->peers.find(id);
  if (jt != n->peers.end()) {
    pc = jt->second;
  } else if (message.type == OFFER) {
    LOG(n->log, "Answering to %s\n", id.c_str());
    pc = createPeerConnection(n, id);
  } else {
    return;
  }

  if (message.type == OFFER || message.type == ANSWER) {
    auto sdp = std::string(message.description);
    pc->setRemoteDescription(rtc::Description(sdp, type_strings[message.type]));
  } else if (message.type == CANDIDATE) {
    auto sdp = std::string(message.candidate);
    auto mid = std::string(message.mid);
    pc->addRemoteCandidate(rtc::Candidate(sdp, mid));
  }
}

// send a request to a peer over websocket
void net_initiateConnection(net* n, std::string id) {
  if (id.empty() || id == n->local_id) {
    LOG(n->log, "[error] Invalid remote ID\n");
    return;
  }

  LOG(n->log, "Offering connection to %s\n", id.c_str());
  auto pc = createPeerConnection(n, id);

  // We are the offerer, so create a data channel to initiate the process
  const std::string label = "all";
  LOG(n->log, "Creating data channel with %s\n", id.c_str());
  auto dc = pc->createDataChannel(label);
  setupDataChannel(n, dc);
  n->data_channels.emplace(id, dc);
}

// Create and setup a PeerConnection
std::shared_ptr<rtc::PeerConnection> createPeerConnection(net* n, std::string id) {
  auto pc = std::make_shared<rtc::PeerConnection>(n->config);
  pc->onStateChange([n](rtc::PeerConnection::State state) {
    LOG(n->log, "peer connection state change: %d\n", state);
  });

  pc->onGatheringStateChange([n](rtc::PeerConnection::GatheringState state) {
    LOG(n->log, "peer connection gathering state: %d\n", state);
  });

  pc->onLocalDescription([n, id](rtc::Description description) {
    LOG(n->log, "got sdp: %s\n", std::string(description).c_str());
    ServerPacket message = {};
    message.command = CONNECT;
    strcpy(message.id, id.c_str());
    message.type = (TYPE)description.type();
    strcpy(message.description, std::string(description).c_str());
    n->ws.send((std::byte*)&message, sizeof(message));
  });

  pc->onLocalCandidate([n, id](rtc::Candidate candidate) {
    LOG(n->log, "got ice candidate: %s\n", std::string(candidate).c_str());
    ServerPacket message = {};
    message.command = CONNECT;
    strcpy(message.id, id.c_str());
    message.type = CANDIDATE;
    strcpy(message.mid, candidate.mid().c_str());
    strcpy(message.candidate, std::string(candidate).c_str());
    n->ws.send((std::byte*)&message, sizeof(message));
  });

  pc->onDataChannel([n, id](std::shared_ptr<rtc::DataChannel> dc) {
    LOG(n->log, "DataChannel from %s received with label %s\n", id.c_str(), dc->label().c_str());
    setupDataChannel(n, dc);
    n->data_channels.emplace(id, dc);
  });

  n->peers.emplace(id, pc);
  return pc;
};

void net_requestList(net* n) {
  ServerPacket message = {};
  message.command = LIST;
  LOG(n->log, "requesting list of clients from central server %u\n", sizeof(message.command));
  n->ws.send((std::byte*)&message, sizeof(message));
}

void net_sendTo(net* n, std::string id, std::string s) {
  std::shared_ptr<rtc::DataChannel> dc;
  auto it = n->data_channels.find(id);
  if (it == n->data_channels.end()) {
    LOG(n->log, "[error] id '%s' not found in list of connected peers\n", id.c_str());
    return;
  }
  dc = it->second;
  if (!dc->isOpen()) {
    LOG(n->log, "[error] data channel to '%s' exists but is not open\n", id.c_str());
    return;
  }
  dc->send(s);
}

void net_sendAll(net* n, std::string s) {
  std::shared_ptr<rtc::DataChannel> dc;
  for (auto it = n->data_channels.begin(); it != n->data_channels.end(); it++) {
    dc = it->second;
    if (dc->isOpen()) {
      dc->send(s);
    }
  }
}

void net_sendTo(net* n, std::string id, void* data, int len) {
  std::vector<std::byte> vector_data;
  vector_data.resize(len);
  memcpy(&vector_data[0], data, len);

  std::shared_ptr<rtc::DataChannel> dc;
  auto it = n->data_channels.find(id);
  if (it == n->data_channels.end()) {
    LOG(n->log, "[error] id '%s' not found in list of connected peers\n", id.c_str());
    return;
  }
  dc = it->second;
  if (!dc->isOpen()) {
    LOG(n->log, "[error] data channel to '%s' exists but is not open\n", id.c_str());
    return;
  }
  dc->send(vector_data);
}

void net_sendAll(net* n, void* data, int len) {
  std::vector<std::byte> vector_data;
  vector_data.resize(len);
  memcpy(&vector_data[0], data, len);

  std::shared_ptr<rtc::DataChannel> dc;
  for (auto it = n->data_channels.begin(); it != n->data_channels.end(); it++) {
    dc = it->second;
    if (dc->isOpen()) {
      dc->send(vector_data);
    }
  }
}