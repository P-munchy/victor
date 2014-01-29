// Simple TCP server that only accepts one client.

#ifndef TCP_SERVER_H
#define TCP_SERVER_H

//
// Create a debug messaging macro to avoid a zillion #if/#endif directives.
// Uncomment the first one to disable debug messages.  Uncomment the second
// one to enable them.  Newline (std::endl) automatically appended.
//  Example usage: DEBUG_TCP_SERVER("Number of clients " << N);
//
#define DEBUG_TCP_SERVER(__expr__)
//#define DEBUG_TCP_SERVER(__expr__) (std::cout << __expr__ << std::endl)

class TcpServer {
public:
  TcpServer();
  ~TcpServer();

  bool StartListening(const unsigned short port);
  void StopListening();

  bool Accept();
  void DisconnectClient();
  bool HasClient();
  //bool SetBlocking()

  int Send(const char* data, int size);
  int Recv(char* data, int maxSize);

private:
  struct addrinfo *host_info_list; // Pointer to the to the linked list of host_info's.

  void set_nonblock(int socket);
  
  int socketfd; // Listening socket descripter
  int client_sd; // Client socket descripter
};

#endif
