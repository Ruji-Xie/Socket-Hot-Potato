#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <vector>
#include "potato.cpp"

#define DEBUG 0

class RingMaster {

  int num_players;
  int hop;
  const char * master_port;

  int socket_fd{};

  fd_set socket_read_fds{};
  const char * master_ip;

  std::vector<int> player_sock_fd_vec;
  std::vector<std::string> player_ip_vec;
  std::vector<uint16_t> player_port_vec;

  int max_fd{};

public:

  explicit RingMaster(int num_players, int hop, const char * master_port): num_players(num_players), hop(hop), master_ip(nullptr), master_port(master_port) {
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
    if (status == -1) {
      std::cerr << "setsockopt failed" << std::endl;
    }

    status = bind(socket_fd, host_info_list->ai_addr, host_info_list->ai_addrlen);

    if (status == -1) {
      std::cerr << "Error: cannot bind socket" << std::endl;
      std::cerr << errno << std::endl;
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

    if (DEBUG) {
      std::cout << "Waiting for connection on port " << master_port << std::endl;
    }

    std::cout << "Potato Ringmaster" << std::endl;
    std::cout << "Players = " << num_players << std::endl;
    std::cout << "Hops = " << hop << std::endl;

    return 0;
  }

  int accept_player_connection(std::string & player_ip, int & player_sock_fd) {
    struct sockaddr_storage socket_addr{};
    socklen_t socket_addr_len = sizeof(socket_addr);
    player_sock_fd = accept(socket_fd, (struct sockaddr *)&socket_addr, &socket_addr_len);
    max_fd = player_sock_fd;
    if (player_sock_fd == -1) {
      std::cerr << "Error: cannot accept connection on socket" << std::endl;
      return -1;
    }
    char player_ip_c[INET_ADDRSTRLEN];
    auto *sin = (struct sockaddr_in *) &socket_addr;
    player_ip = inet_ntoa(sin->sin_addr);
    return 0;
  }

  int collect_player_addr_info() {
    for (int i = 0; i < num_players; i++) {
      accept_player_connection(player_ip_vec[i], player_sock_fd_vec[i]);
      uint16_t player_port{};
      recv(player_sock_fd_vec[i], &player_port, sizeof(player_port), MSG_WAITALL);
      if (DEBUG) {
        std::cout << player_port << std::endl;
      }
      player_port_vec[i] = player_port;
    }


    if (DEBUG) {
      std::cout << "ip:" << std::endl;
      for (const auto& ip : player_ip_vec) {
        std::cout << ip << std::endl;
      }

      std::cout << "port:" << std::endl;
      for (const auto& port : player_port_vec) {
        std::cout << port << std::endl;
      }
    }

    return 0;
  }

  int send_neighbour_server_info_to_player() {
    for (int i = 0; i < num_players; i++) {
      int neighbour_id = (i + 1) % num_players;
      player_ai_t player_ai{};
      stpcpy(player_ai.ip, player_ip_vec[neighbour_id].c_str());
      player_ai.port = player_port_vec[neighbour_id];
      if (DEBUG) {
      std::cout << "send neighbor server info to player: " << i << std::endl;
      }

      int size = send(player_sock_fd_vec[i], &player_ai, sizeof(player_ai), 0);
      if (size < sizeof(player_ai)) {
        std::cerr << "player_ai is not completely sent, sent size: " << size << std::endl;
      }
    }
    return 0;
  }

  int check_which_player_is_ready() {
    init_fd_set();
    if (DEBUG) {
      std::cout << "check which one is ready" << std::endl;
    }
    int num_ready = 0;

    while (1) {
      fd_set rdfs = socket_read_fds;
      int rv = select(max_fd + 1, &rdfs, nullptr, nullptr, nullptr);
      if (DEBUG) {
        std::cout << "someone is ready" << std::endl;
      }
      if (rv == 0) { continue; }
      for (int i = 0; i < num_players; i++) {
        if (FD_ISSET(player_sock_fd_vec[i], &rdfs)) {
          int id = 0;
          int size = recv(player_sock_fd_vec[i], &id, sizeof(id), MSG_WAITALL);
          if (size < sizeof(id)) {
            std::cerr << "id is not completely recv, recv size: " << id << std::endl;
          }
          std::cout << "Player " << id << " is ready to play" << std::endl;
          if (DEBUG) {
            std::cout << "receive id from player: " << i << std::endl;
            std::cout << "ip: " << player_ip_vec[i] << std::endl;
            std::cout << "port: " << player_port_vec[i] << std::endl;
          }
          num_ready++;
          break;
        }
      }
      if (num_ready == num_players) {
        break;
      }
    }

  }

  int send_player_id_to_player() {
    for (int i = 0; i < num_players; i++) {
      if (DEBUG) {
        std::cout << "send id to player: " << i << std::endl;
        std::cout << "ip: " << player_ip_vec[i] << std::endl;
        std::cout << "port: " << player_port_vec[i] << std::endl;
      }
      int size = send(player_sock_fd_vec[i], &i, sizeof(i), 0);
      if (size != sizeof(i)) {
        std::cerr << "id is not completely sent, sent size: " << size << std::endl;
      }
    }
    return 0;
  }

  int send_player_num_to_player() {
    for (int i = 0; i < num_players; i++) {
      if (DEBUG) {
        std::cout << "send id to player: " << i << std::endl;
        std::cout << "ip: " << player_ip_vec[i] << std::endl;
        std::cout << "port: " << player_port_vec[i] << std::endl;
      }
      int size = send(player_sock_fd_vec[i], &num_players, sizeof(num_players), 0);
      if (size != sizeof(num_players)) {
        std::cerr << "id is not completely sent, sent size: " << size << std::endl;
      }
    }
    return 0;
  }

  int init_fd_set() {
    FD_ZERO(&socket_read_fds);
    for (int i = 0; i < num_players; i++) {
      FD_SET(player_sock_fd_vec[i], &socket_read_fds);
    }
    return 0;
  }

  int start_game() {
    if (DEBUG) {
      std::cout << "start game" << std::endl;
    }
    potato_t potato{};
    potato.hop = hop;
    int rand_int = rand() % num_players;

    if (hop == 0) {
      potato.end = 1;
      for (int i = 0; i < num_players; i++) {
        if (DEBUG) {
          std::cout << "send end signal to player: " << i << std::endl;
          std::cout << "ip: " << player_ip_vec[i] << std::endl;
          std::cout << "port: " << player_port_vec[i] << std::endl;
        }
        int size = send(player_sock_fd_vec[i], &potato, sizeof(potato), 0);
        if (size < sizeof(potato)) {
          std::cerr << "potato is not completely sent, sent size: " << size << std::endl;
        }
      }
      return 0;
    }

    std::cout << "Ready to start the game, sending potato to player " << rand_int << std::endl;
    if (DEBUG) {
      std::cout << "ip: " << player_ip_vec[rand_int] << std::endl;
      std::cout << "port: " << player_port_vec[rand_int] << std::endl;
    }
    int size = send(player_sock_fd_vec[rand_int], &potato, sizeof(potato), 0);
    if (size != sizeof(potato)) {
      std::cerr << "potato is not completely sent, sent size: " << size << std::endl;
    }

    int rv = select(max_fd + 1, &socket_read_fds, nullptr, nullptr, nullptr);
    int end_player_id = 0;
    potato_t received_potato{};
    for (int i = 0; i < num_players; i++) {
      if (FD_ISSET(player_sock_fd_vec[i], &socket_read_fds)) {
        int size = recv(player_sock_fd_vec[i], &received_potato, sizeof(received_potato), MSG_WAITALL);
        if (size < sizeof(received_potato)) {
          if (size == 0) {
            continue;
          }
          std::cerr << "potato is not completely recv, recv size: " << size << std::endl;
        }
        if (DEBUG) {
          std::cout << "receive end potato from player: " << i << std::endl;
          std::cout << "ip: " << player_ip_vec[i] << std::endl;
          std::cout << "port: " << player_port_vec[i] << std::endl;
        }
        end_player_id = i;
        break;
      }
    }
    if (DEBUG) {
      std::cout << "received potato " << std::endl;
      std::cout << "potato count/hop: " << received_potato.count << "/" << received_potato.hop << std::endl;
    }
    std::cout << "Trace of potato: " << std::endl;
    for (int i = 0; i < received_potato.count; i++) {
      std::cout << received_potato.trace[i] << (i == received_potato.count - 1 ?  "" : ",");
    }
    std::cout << std::endl;

    if (DEBUG) {
      std::cout << "\n potato trace ends. " << std::endl;
    }


    received_potato.end = true;
    for (int i = 0; i < num_players; i++) {
      if (DEBUG) {
        std::cout << "send end signal to player: " << i << std::endl;
        std::cout << "ip: " << player_ip_vec[i] << std::endl;
        std::cout << "port: " << player_port_vec[i] << std::endl;
      }
      int size = send(player_sock_fd_vec[i], &received_potato, sizeof(received_potato), 0);
      if (size < sizeof(received_potato)) {
        std::cerr << "potato is not completely sent, sent size: " << size << std::endl;
      }
    }

    if (DEBUG) {
      std::cout << "game ends." << std::endl;
    }
  }

};

int main(int argc, char *argv[])
{
  if (argc < 4) {
    std::cerr << "You should input 3 args." << std::endl;
    return -1;
  }
  RingMaster ring_master(std::stoi(argv[2]), std::stoi(argv[3]), argv[1]);

  ring_master.init_server();
  ring_master.collect_player_addr_info();
  ring_master.send_player_id_to_player();
  ring_master.send_player_num_to_player();
  ring_master.send_neighbour_server_info_to_player();

  ring_master.check_which_player_is_ready();
  ring_master.start_game();
  return 0;
}
