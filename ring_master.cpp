#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <vector>


typedef struct player_ai {
  char ip[INET_ADDRSTRLEN];
  uint16_t port;
} player_ai_t;


class RingMaster {

  int num_players;
  int socket_fd{};

  const char * master_ip;
  const char * master_port;

  std::vector<int> player_sock_fd_vec;
  std::vector<std::string> player_ip_vec;
  std::vector<uint16_t> player_port_vec;

  char *neighbor_server_ip{};
  uint16_t neighbor_server_port{};

public:

  explicit RingMaster(const char * master_port, int num_players): master_ip(nullptr), master_port(master_port), num_players(num_players) {
    player_sock_fd_vec.resize(num_players);
    player_ip_vec.resize(num_players);
    player_port_vec.resize(num_players);
  }

  ~RingMaster() {
    for (const auto &it : player_sock_fd_vec) {
      close(it);
    }
    close(socket_fd);
  }

  int init_server() {
    int status;
    struct addrinfo host_info;
    struct addrinfo *host_info_list;
    const char *hostname = master_ip;

    memset(&host_info, 0, sizeof(host_info));

    host_info.ai_family   = AF_UNSPEC;
    host_info.ai_socktype = SOCK_STREAM;
    host_info.ai_flags    = AI_PASSIVE;

    status = getaddrinfo(hostname, master_port, &host_info, &host_info_list);
    if (status != 0) {
      std::cerr << "Error: cannot get address info for host" << std::endl;
      std::cerr << "  (" << hostname << "," << master_port << ")" << std::endl;
      return -1;
    } //if

    socket_fd = socket(host_info_list->ai_family,
                       host_info_list->ai_socktype,
                       host_info_list->ai_protocol);
    if (socket_fd == -1) {
      std::cerr << "Error: cannot create socket" << std::endl;
      std::cerr << "  (" << hostname << "," << master_port << ")" << std::endl;
      return -1;
    } //if

    int yes = 1;
    status = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
    status = bind(socket_fd, host_info_list->ai_addr, host_info_list->ai_addrlen);
    if (status == -1) {
      std::cerr << "Error: cannot bind socket" << std::endl;
      std::cerr << "  (" << hostname << "," << master_port << ")" << std::endl;
      return -1;
    } //if

    freeaddrinfo(host_info_list);

    status = listen(socket_fd, 100);
    if (status == -1) {
      std::cerr << "Error: cannot listen on socket" << std::endl;
      std::cerr << "  (" << hostname << "," << master_port << ")" << std::endl;
      return -1;
    } //if

    std::cout << "Waiting for connection on port " << master_port << std::endl;
    return 0;
  }

  int accept_player_connection(std::string & player_ip, int & player_sock_fd) {
    struct sockaddr_storage socket_addr{};
    socklen_t socket_addr_len = sizeof(socket_addr);
    player_sock_fd = accept(socket_fd, (struct sockaddr *)&socket_addr, &socket_addr_len);
    if (player_sock_fd == -1) {
      std::cerr << "Error: cannot accept connection on socket" << std::endl;
      return -1;
    }
    char player_ip_c[INET_ADDRSTRLEN];
    auto *sin = (struct sockaddr_in *) &socket_addr;
    player_ip = inet_ntoa(sin->sin_addr);
    return 0;
  }

  int receive_message(int client_connection_fd) {
    char buffer[512];
    recv(client_connection_fd, buffer, 9, 0);
    buffer[9] = 0;
    std::cout << buffer << std::endl;
    return 0;
  }


  int receive_test_number(int client_connection_fd) {
    uint16_t test;
    recv(client_connection_fd, &test, sizeof(test), 0);
    std::cout << test << std::endl;
    return 0;
  }

  int collect_player_addr_info() {
    for (int i = 0; i < num_players; i++) {
      accept_player_connection(player_ip_vec[i], player_sock_fd_vec[i]);
      uint16_t player_port{};
      recv(player_sock_fd_vec[i], &player_port, sizeof(player_port), 0);
      std::cout << player_port << std::endl;
      player_port_vec[i] = player_port;
    }

    std::cout << "ip:" << std::endl;
    for (const auto& ip : player_ip_vec) {
      std::cout << ip << std::endl;
    }

    std::cout << "port:" << std::endl;
    for (const auto& port : player_port_vec) {
      std::cout << port << std::endl;
    }

    return 0;
  }

  int send_neighbour_server_info_to_player() {
    for (int i = 0; i < num_players; i++) {
      int neighbour_id = (i + 1) % num_players;
      player_ai_t player_ai;
      stpcpy(player_ai.ip, player_ip_vec[neighbour_id].c_str());
      player_ai.port = player_port_vec[neighbour_id];
      send(player_sock_fd_vec[i], &player_ai, sizeof(player_ai), 0);
    }
    return 0;
  }
};

int main(int argc, char *argv[])
{
  if (argc < 2) {
    std::cerr << "Please enter port num." << std::endl;
    return -1;
  }
  RingMaster ring_master(argv[1], std::stoi(argv[2]));

  ring_master.init_server();
//  std::string ip;
//  int sock;
//  ring_master.accept_player_connection(ip, sock);
//  ring_master.receive_test_number();
  ring_master.collect_player_addr_info();
  ring_master.send_neighbour_server_info_to_player();

  return 0;
}
