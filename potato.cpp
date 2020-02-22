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