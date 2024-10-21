#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  
  // You can use print statements as follows for debugging, they'll be visible when running tests.
  std::cout << "Logs from your program will appear here!\n";

  // Uncomment this block to pass the first stage
  //
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
   std::cerr << "Failed to create server socket\n";
   return 1;
  }
  
  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }
  
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(4221);
  
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 4221\n";
    return 1;
  }
  
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }
  
  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);
  
  std::cout << "Waiting for a client to connect...\n";
  
  int client = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);

  std::cout << "Client connected\n";
  
  std::string message = "HTTP/1.1 200 OK\r\n\r\n";

  // send(client,message.c_str(),message.size(),0); 

  //Stage - Extracting URL Path from request
  char buffer[1024] = {0};

  read(client, buffer, 1024);

  std::string request(buffer);

  std::cout << "Request: " << request << std::endl;

  std::string path = request.substr(request.find("GET") + 4, request.find("HTTP") - 5);

  std::cout << "Path : " << path << std::endl;

  std::string notFounderror = "HTTP/1.1 404 Not Found\r\n\r\n";

  std::string contentResponse = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: ";

  // if(path != "/") {
  //   send(client,notFounderror.c_str(),notFounderror.size(),0);
  // }
  // else {
  //   send(client,message.c_str(),message.size(),0);
  // }

  if(path.find("echo") != std::string::npos) {
    std::string input = path.substr(path.find("echo") + 5);
    std::cout << input << std::endl;
    std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(input.size()) + "\r\n\r\n" + input;
    send(client,response.c_str(),response.size(),0);
  }
  else if(path == "/") {
    send(client,message.c_str(),message.size(),0);
  }
  else if(path == "/user-agent") {
    std::string input = request.substr(request.find("User-Agent") + 12,request.find("\r\n"));
    input = input.substr(0, input.find("\r\n"));
    std::cout << "User Agent : " << input << std::endl;
    std::string response = contentResponse + std::to_string(input.size()) + "\r\n\r\n" + input;
    send(client,response.c_str(),response.size(),0);
  }
  else {
    send(client,notFounderror.c_str(),notFounderror.size(),0);
  }
  close(server_fd);

  return 0;
}
