#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>

typedef struct player_ai {
  char ip[INET_ADDRSTRLEN];
  uint16_t port;
} player_ai_t;


class Player {

  int socket_fd{};
  int client_connection_fd{};

  const char * master_ip;
  const char * master_port;
  int socket_fd_ringmaster;

  uint16_t player_server_port{};
  player_ai_t neighbor_server_ai{};
  int neighbor_server_fd;

public:

  Player(const char * master_ip, const char * master_port): master_ip(master_ip), master_port(master_port) {}

  ~Player() {
    close(socket_fd_ringmaster);
    close(socket_fd);
  }

  int init_server() {
    int status;
    struct addrinfo host_info;
    struct addrinfo *host_info_list;
    const char *hostname = nullptr;
    memset(&host_info, 0, sizeof(host_info));

    host_info.ai_family   = AF_UNSPEC;
    host_info.ai_socktype = SOCK_STREAM;
    host_info.ai_flags    = AI_PASSIVE;

    status = getaddrinfo(hostname, "", &host_info, &host_info_list);
    if (status != 0) {
      std::cerr << "Error: cannot get address info for host" << std::endl;
      return -1;
    } //if

    // get an available port assigned by os
    auto *ai = (struct sockaddr_in *)(host_info_list->ai_addr);
    ai->sin_port = 0;

    socket_fd = socket(host_info_list->ai_family,
                       host_info_list->ai_socktype,
                       host_info_list->ai_protocol);

    if (socket_fd == -1) {
      std::cerr << "Error: cannot create socket" << std::endl;
      return -1;
    }

    int yes = 1;
    status = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
    status = bind(socket_fd, host_info_list->ai_addr, host_info_list->ai_addrlen);
    if (status == -1) {
      std::cerr << "Error: cannot bind socket" << std::endl;
      return -1;
    } //if

    freeaddrinfo(host_info_list);

    status = listen(socket_fd, 100);
    if (status == -1) {
      std::cerr << "Error: cannot listen on socket" << std::endl;
      return -1;
    } //if

    struct sockaddr_in sin{};
    socklen_t len = sizeof(sin);
    if (getsockname(socket_fd, (struct sockaddr *)&sin, &len) == -1) {
      std::cerr << "Error: cannot get sock name" << std::endl;
    } else {
      player_server_port = ntohs(sin.sin_port);
    }

    std::cout << "Waiting for connection on port " << player_server_port << std::endl;
    return 0;
  }

  int accept_connection() {
    struct sockaddr_storage socket_addr;
    socklen_t socket_addr_len = sizeof(socket_addr);
    int neighbor_player_fd = accept(socket_fd, (struct sockaddr *)&socket_addr, &socket_addr_len);
    if (neighbor_player_fd == -1) {
      std::cerr << "Error: cannot accept connection on socket" << std::endl;
      return -1;
    }
    auto *sin = (struct sockaddr_in *) &socket_addr;
    char neighbor_server_ip[INET_ADDRSTRLEN];
    inet_ntop(socket_addr.ss_family, sin, neighbor_server_ip, sizeof neighbor_server_ip);
    std::cout << "accept connection from: " << neighbor_server_ip << std::endl;

    char message[100] = "Hello, neighbor";
    send(neighbor_server_fd, &message, sizeof(message), 0);

    return 0;
  }

  int connect_ring_master() {
    int status;
    struct addrinfo host_info;
    struct addrinfo *host_info_list;

    memset(&host_info, 0, sizeof(host_info));
    host_info.ai_family   = AF_UNSPEC;
    host_info.ai_socktype = SOCK_STREAM;

    status = getaddrinfo(master_ip, master_port, &host_info, &host_info_list);
    if (status != 0) {
      std::cerr << "Error: cannot get address info for host" << std::endl;
      std::cerr << "  (" << master_ip << "," << master_port << ")" << std::endl;
      return -1;
    }

    socket_fd_ringmaster = socket(host_info_list->ai_family,
                       host_info_list->ai_socktype,
                       host_info_list->ai_protocol);
    if (socket_fd_ringmaster == -1) {
      std::cerr << "Error: cannot create socket" << std::endl;
      std::cerr << "  (" << master_ip << "," << master_port << ")" << std::endl;
      return -1;
    }

    std::cout << "Connecting to " << master_ip << " on port " << master_port << "..." << std::endl;

    status = connect(socket_fd_ringmaster, host_info_list->ai_addr, host_info_list->ai_addrlen);
    if (status == -1) {
      std::cerr << "Error: cannot connect to socket" << std::endl;
      std::cerr << "  (" << master_ip << "," << master_port << ")" << std::endl;
      return -1;
    }

    send(socket_fd_ringmaster, &player_server_port, sizeof(player_server_port), 0);

//    uint16_t test = 123;
//    send(socket_fd, &test, sizeof(test), 0);

    freeaddrinfo(host_info_list);

    return 0;
  }

  int receive_neighbor_server_ai() {
    recv(socket_fd_ringmaster, &neighbor_server_ai, sizeof(neighbor_server_ai), 0);
    std::cout << "neighbor server ip: " << neighbor_server_ai.ip << std::endl;
    std::cout << "neighbor server port: " << neighbor_server_ai.port << std::endl;
    return 0;
  }

  int connect_neighbor_server() {
    int status;
    struct addrinfo host_info;
    struct addrinfo *host_info_list;

    memset(&host_info, 0, sizeof(host_info));
    host_info.ai_family   = AF_UNSPEC;
    host_info.ai_socktype = SOCK_STREAM;

    const char * host_ip = neighbor_server_ai.ip;
    const char * host_port = std::to_string(neighbor_server_ai.port).c_str();

    status = getaddrinfo(host_ip, host_port, &host_info, &host_info_list);
    if (status != 0) {
      std::cerr << "Error: cannot get address info for host" << std::endl;
      std::cerr << "  (" << host_ip << "," << host_port << ")" << std::endl;
      return -1;
    }

    neighbor_server_fd = socket(host_info_list->ai_family,
                                  host_info_list->ai_socktype,
                                  host_info_list->ai_protocol);
    if (neighbor_server_fd == -1) {
      std::cerr << "Error: cannot create socket" << std::endl;
      std::cerr << "  (" << host_ip << "," << host_port << ")" << std::endl;
      return -1;
    }

    std::cout << "Connecting to " << host_ip << " on port " << host_port << "..." << std::endl;

    status = connect(neighbor_server_fd, host_info_list->ai_addr, host_info_list->ai_addrlen);
    if (status == -1) {
      std::cerr << "Error: cannot connect to socket" << std::endl;
      std::cerr << "  (" << host_ip << "," << host_port << ")" << std::endl;
      return -1;
    }

//    uint16_t test = 123;
//    send(socket_fd, &test, sizeof(test), 0);

    freeaddrinfo(host_info_list);

    return 0;
  }

  int receive_message() {
    char buffer[100];
    recv(socket_fd, buffer, 9, 0);
    std::cout << buffer << std::endl;
    return 0;
  }


};

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << "Please enter ip and port num." << std::endl;
    return -1;
  }

  Player player{argv[1], argv[2]};
  player.init_server();
  player.connect_ring_master();
  player.receive_neighbor_server_ai();
  player.connect_neighbor_server();
  player.accept_connection();
  player.receive_message();

  return 0;
}
