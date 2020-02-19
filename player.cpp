#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cassert>

#define DEBUG 0

typedef struct player_ai {
  char ip[INET_ADDRSTRLEN];
  uint16_t port;
} player_ai_t;

typedef struct potato {
  int trace[513];
  int count;
  int hop;
  int end;
} potato_t;

class Player {

  const char * master_ip;
  const char * master_port;
  int player_server_fd{};
  int ringmaster_fd{};
  int neighbor_server_fd{};
  int neighbor_server_id;
  int neighbor_player_connection_fd{};
  int player_connection_id;

  fd_set socket_read_fds{};

  int id;
  int num_players;

  int max_fd;
  uint16_t player_server_port{};
  player_ai_t neighbor_server_ai{};

public:

  Player(const char * master_ip, const char * master_port): master_ip(master_ip), master_port(master_port) {
  }

  ~Player() {
    close(ringmaster_fd);
    close(player_server_fd);
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

    player_server_fd = socket(host_info_list->ai_family,
                       host_info_list->ai_socktype,
                       host_info_list->ai_protocol);

    if (player_server_fd == -1) {
      std::cerr << "Error: cannot create socket" << std::endl;
      return -1;
    }

    int yes = 1;
    status = setsockopt(player_server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
    status = bind(player_server_fd, host_info_list->ai_addr, host_info_list->ai_addrlen);
    if (status == -1) {
      std::cerr << "Error: cannot bind socket" << std::endl;
      return -1;
    } //if

    freeaddrinfo(host_info_list);

    status = listen(player_server_fd, 100);
    if (status == -1) {
      std::cerr << "Error: cannot listen on socket" << std::endl;
      return -1;
    } //if

    struct sockaddr_in sin{};
    socklen_t len = sizeof(sin);
    if (getsockname(player_server_fd, (struct sockaddr *)&sin, &len) == -1) {
      std::cerr << "Error: cannot get sock name" << std::endl;
    } else {
      player_server_port = ntohs(sin.sin_port);
    }

    if (DEBUG) {
      std::cout << "Waiting for connection on port " << player_server_port << std::endl;
    }
    return 0;
  }

  int accept_connection() {
    struct sockaddr_storage socket_addr{};
    socklen_t socket_addr_len = sizeof(socket_addr);
    neighbor_player_connection_fd = accept(player_server_fd, (struct sockaddr *)&socket_addr, &socket_addr_len);
    max_fd = neighbor_player_connection_fd;
    if (neighbor_player_connection_fd == -1) {
      std::cerr << "Error: cannot accept connection on socket" << std::endl;
      return -1;
    }

    int size = send(neighbor_player_connection_fd, &id, sizeof(id), 0);
    if (size != sizeof(id)) {
      std::cerr << "id is not completely sent, sent size: " << size << std::endl;
    }

    if (DEBUG) {
      auto *sin = (struct sockaddr_in *) &socket_addr;
      const char * neighbor_server_ip = inet_ntoa(sin->sin_addr);
      std::cout << "accept connection from: " << neighbor_server_ip << std::endl;
      char message[100] = "Hello, neighbor";
      int size = send(neighbor_server_fd, &message, sizeof(message), 0);
      if (size != sizeof(message)) {
        std::cout << "message Hello neighbor send failed" << std::endl;
      }

    }
    return 0;
  }

  int connect_ring_master() {
    int status;
    struct addrinfo host_info{};
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

    ringmaster_fd = socket(host_info_list->ai_family,
                       host_info_list->ai_socktype,
                       host_info_list->ai_protocol);
    if (ringmaster_fd == -1) {
      std::cerr << "Error: cannot create socket" << std::endl;
      std::cerr << "  (" << master_ip << "," << master_port << ")" << std::endl;
      return -1;
    }

    if (DEBUG) {
      std::cout << "Connecting to " << master_ip << " on port " << master_port << "..." << std::endl;
    }

    status = connect(ringmaster_fd, host_info_list->ai_addr, host_info_list->ai_addrlen);
    if (status == -1) {
      std::cerr << "Error: cannot connect to socket" << std::endl;
      std::cerr << "  (" << master_ip << "," << master_port << ")" << std::endl;
      return -1;
    }

    int size = send(ringmaster_fd, &player_server_port, sizeof(player_server_port), 0);
    if (size != sizeof(player_server_port)) {
      std::cerr << "player_server_port is not completely sent, sent size: " << size << std::endl;
    }

    freeaddrinfo(host_info_list);

    return 0;
  }

  int receive_neighbor_server_ai() {
    recv(ringmaster_fd, &neighbor_server_ai, sizeof(neighbor_server_ai), MSG_WAITALL);
    if (DEBUG) {
      std::cout << "neighbor server ip: " << neighbor_server_ai.ip << std::endl;
      std::cout << "neighbor server port: " << neighbor_server_ai.port << std::endl;
    }
    return 0;
  }

  int receive_my_id() {
    recv(ringmaster_fd, &id, sizeof(id), MSG_WAITALL);
    if (DEBUG) {
      std::cout << "my id: " << id << std::endl;
    }
    srand((unsigned int)time(nullptr) + id);
    return 0;
  }

  int receive_num_players() {
    recv(ringmaster_fd, &num_players, sizeof(num_players), MSG_WAITALL);
    if (DEBUG) {
      std::cout << "num_players: " << num_players << std::endl;
    }
    return 0;
  }


  int connect_neighbor_server() {
    int status;
    struct addrinfo host_info{};
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

    if (DEBUG) {
      std::cout << "Connecting to " << host_ip << " on port " << host_port << "..." << std::endl;
    }

    status = connect(neighbor_server_fd, host_info_list->ai_addr, host_info_list->ai_addrlen);
    if (status == -1) {
      std::cerr << "Error: cannot connect to socket" << std::endl;
      std::cerr << "  (" << host_ip << "," << host_port << ")" << std::endl;
      return -1;
    }

    freeaddrinfo(host_info_list);

    if (DEBUG) {
      std::cout << "I have connected to the neighbor server and now wait for receiving neighbor_server_id" << std::endl;
    }

    int size = send(neighbor_server_fd, &id, sizeof(id), 0);
    if (size != sizeof(id)) {
      std::cerr << "id is not completely sent, sent size: " << size << std::endl;
    }


    return 0;
  }

  int receive_message() {
    char buffer[100];
    recv(neighbor_player_connection_fd, buffer, 100, MSG_WAITALL);
    std::cout << buffer << std::endl;
    return 0;
  }

  int receive_neighbor_ids() {
    recv(neighbor_server_fd, &neighbor_server_id, sizeof(neighbor_server_id), MSG_WAITALL);
    recv(neighbor_player_connection_fd, &player_connection_id, sizeof(player_connection_id), MSG_WAITALL);
  }


  int init_fd_set() {
    FD_ZERO(&socket_read_fds);
    if (DEBUG) {
      std::cout << "ringmaster fd: " << ringmaster_fd << std::endl;
      std::cout << "neighbor server fd: " << neighbor_server_fd << std::endl;
      std::cout << "neighbor player connection fd: " << neighbor_player_connection_fd << std::endl;
      std::cout << "max fd: " << max_fd << std::endl;
    }

    FD_SET(ringmaster_fd, &socket_read_fds);
    FD_SET(neighbor_server_fd, &socket_read_fds);
    FD_SET(neighbor_player_connection_fd, &socket_read_fds);
    max_fd = std::max(neighbor_server_fd, neighbor_player_connection_fd);
    max_fd = std::max(max_fd, ringmaster_fd);

    return 0;
  }

  int notify_master_I_am_ready() {
    int size = send(ringmaster_fd, &id, sizeof(id), 0);
    if (DEBUG) {
      std::cout << "send my id to master, tell master I am ready" << std::endl;
    }
    if (size != sizeof(id)) {
      std::cerr << "id is not completely sent, sent size: " << size << std::endl;
    }
    return 0;
  }

  int play() {
    if (DEBUG) {
      std::cout << "I am player : " << id << std::endl;
    }
    while(1) {
      potato_t potato{};
      fd_set read_fds = socket_read_fds;
      int rv = select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr);
//      assert(rv == 1);
      if (FD_ISSET(ringmaster_fd, &read_fds)) {
        potato_t tmp{};
        int size = recv(ringmaster_fd, &tmp, sizeof(tmp), MSG_WAITALL);
        if (size == sizeof(potato)) {
          potato = tmp;
        }
        if (DEBUG) {
          std::cout << "received potato from master" << std::endl;
        }
      }
      if (FD_ISSET(neighbor_server_fd, &read_fds)) {
        if (DEBUG) {
          std::cout << "received potato from player" << std::endl;
        }
        potato_t tmp{};
        int size = recv(neighbor_server_fd, &tmp, sizeof(tmp), MSG_WAITALL);
        if (size == sizeof(potato)) {
          potato = tmp;
        }
      }
      if (FD_ISSET(neighbor_player_connection_fd, &read_fds)) {
        if (DEBUG) {
          std::cout << "received potato from player" << std::endl;
        }
        potato_t tmp{};
        int size = recv(neighbor_player_connection_fd, &tmp, sizeof(tmp), MSG_WAITALL);
        if (size == sizeof(potato)) {
          potato = tmp;
        }
      }

      if (!FD_ISSET(neighbor_player_connection_fd, &read_fds) && !FD_ISSET(neighbor_server_fd, &read_fds) && !FD_ISSET(ringmaster_fd, &read_fds)) {
        std::cerr << "no idea where this shit comes from" << std::endl;
        continue;
      }

      if (DEBUG) {
        std::cout << "potato count/hop: " << potato.count << "/" << potato.hop << std::endl;
        std::cout << "potato trace: " << std::endl;
        for (int i = 0; i < potato.count; i++) {
          std::cout << potato.trace[i] << (i == potato.count - 1 ?  "" : ", ");
        }
        std::cout << "\npotato trace ends. " << std::endl;
      }

      if (potato.end) {
        if (DEBUG) {
          std::cout << "this is the end signal from master" << std::endl;
        }
        break;
      } else {
        if (DEBUG) {
          std::cout << "potato count/hop: " << potato.count << "/" << potato.hop << ", send to ringmaster" << std::endl;
        }
        if (++potato.count == potato.hop) {
          std::cout << "I am it!" << std::endl;
          int size = send(ringmaster_fd, &potato, sizeof(potato), 0);
          if (size != sizeof(potato)) {
            std::cerr << "potato is not completely sent, sent size: " << size << std::endl;
          }
          break;
        } else {
          int rand_int = rand() % 2;
          potato.trace[potato.count-1] = id;
          if (rand_int == 0) {
            if (DEBUG) {
              std::cout << "rand int: " << rand_int << ", send to who connects to me" << std::endl;
            }
            int size = send(neighbor_player_connection_fd, &potato, sizeof(potato), 0);
            if (size != sizeof(potato)) {
              std::cerr << "potato is not completely sent, sent size: " << size << std::endl;
            }
          } else {
            if (DEBUG) {
              std::cout << "rand int: " << rand_int << ", send to whom I connect to" << std::endl;
            }
            int size = send(neighbor_server_fd, &potato, sizeof(potato), 0);
            if (size != sizeof(potato)) {
              std::cerr << "potato is not completely sent, sent size: " << size << std::endl;
            }
          }
          std::cout << "Sending potato to " << (rand_int == 0 ? player_connection_id : neighbor_server_id) << std::endl;

        }
      }
    }

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
  player.receive_my_id();
  player.receive_num_players();
  player.receive_neighbor_server_ai();
  player.connect_neighbor_server();
  player.accept_connection();
  if (DEBUG) {
    player.receive_message();
  }

  player.receive_neighbor_ids();

  player.notify_master_I_am_ready();
  player.init_fd_set();
  player.play();
//  sleep(1);
  return 0;
}
