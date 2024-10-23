#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <fstream>

class HttpRequest
{
public:
  std::string method;
  std::string path;
  std::string userAgent;
  std::string body;

  HttpRequest(const std::string &request)
  {
    parseRequest(request);
  }

private:
  void parseRequest(const std::string &request)
  {
    method = request.substr(0, request.find(" "));
    path = request.substr(request.find(method) + method.size() + 1, request.find("HTTP") - method.size() - 2);

    auto uaPos = request.find("User-Agent:");
    if (uaPos != std::string::npos)
    {
      userAgent = request.substr(uaPos + 12);
      userAgent = userAgent.substr(0, userAgent.find("\r\n"));
    }

    if (method == "POST")
    {
      body = request.substr(request.find("\r\n\r\n") + 4);
    }
  }
};

class HttpResponse
{
public:
  std::string status;
  std::string headers;
  std::string body;

  HttpResponse() : status("HTTP/1.1 200 OK"), headers("Content-Type: text/plain\r\n") {}

  std::string toString()
  {
    return status + "\r\n" + headers + "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
  }
};

class ThreadPool
{
public:
  ThreadPool(size_t numThreads);
  ~ThreadPool();
  void enqueue(std::function<void()> task);

private:
  std::vector<std::thread> workers;
  std::queue<std::function<void()>> tasks;
  std::mutex queueMutex;
  std::condition_variable condition;
  std::atomic<bool> stop;

  void worker();
};

ThreadPool::ThreadPool(size_t numThreads) : stop(false)
{
  for (size_t i = 0; i < numThreads; i++)
  {
    workers.emplace_back([this]
                         { this->worker(); });
  }
}

ThreadPool::~ThreadPool()
{
  {
    std::unique_lock<std::mutex> lock(queueMutex);
    stop = true;
  }
  condition.notify_all();
  for (std::thread &worker : workers)
  {
    worker.join();
  }
}

void ThreadPool::enqueue(std::function<void()> task)
{
  {
    std::unique_lock<std::mutex> lock(queueMutex);
    tasks.emplace(std::move(task));
  }
  condition.notify_one();
}

void ThreadPool::worker()
{
  while (true)
  {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(queueMutex);
      condition.wait(lock, [this]
                     { return stop || !tasks.empty(); });
      if (stop && tasks.empty())
      {
        return;
      }
      task = std::move(tasks.front());
      tasks.pop();
    }
    task();
  }
}

void handleClient(int client, int argc, char **argv)
{
  std::cout << "Client connected\n";
  char buffer[4096] = {0};
  ssize_t bytesRead = read(client, buffer, sizeof(buffer) - 1);

  if (bytesRead < 0)
  {
    std::cerr << "Failed to read from client\n";
    close(client);
    return;
  }

  buffer[bytesRead] = '\0';
  HttpRequest request(buffer);
  HttpResponse response;

  std::cout << "Request: " << request.method << " " << request.path << std::endl;

  if (request.method == "GET")
  {
    if (request.path == "/")
    {
      response.body = "Welcome!";
    }
    else if (request.path.find("echo") != std::string::npos)
    {
      response.body = request.path.substr(request.path.find("echo") + 5);
    }
    else if (request.path == "/user-agent")
    {
      response.body = request.userAgent;
    }
    else if (request.path.find("file") != std::string::npos)
    {
      std::string filename = request.path.substr(request.path.find("file") + 5);
      std::string directory = argc > 1 ? argv[2] : "./";
      std::ifstream file(directory + filename);

      if (file.is_open())
      {
        response.body.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        response.headers = "Content-Type: application/octet-stream\r\n";
        file.close();
      }
      else
      {
        response.status = "HTTP/1.1 404 Not Found";
      }
    }
    else
    {
      response.status = "HTTP/1.1 404 Not Found";
    }
  }
  else if (request.method == "POST")
  {
    if (request.path.find("files") != std::string::npos)
    {
      std::string filename = request.path.substr(request.path.find("files") + 6);
      std::string directory = argc > 1 ? argv[2] : "./";
      std::ofstream file(directory + filename);

      if (file.is_open())
      {
        file << request.body;
        file.close();
        response.status = "HTTP/1.1 201 Created";
        response.headers = "Content-Type: application/octet-stream\r\n";
      }
      else
      {
        response.status = "HTTP/1.1 404 Not Found";
      }
    }
  }
  else
  {
    response.status = "HTTP/1.1 404 Not Found";
  }

  std::string res = response.toString();
  send(client, res.c_str(), res.size(), 0);
  close(client);
}

int main(int argc, char **argv)
{
  std::cout << std::unitbuf; // Ensure all output is flushed immediately
  std::cerr << std::unitbuf;

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0)
  {
    std::cerr << "Failed to create server socket\n";
    return 1;
  }

  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
  {
    std::cerr << "setsockopt failed\n";
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(4221);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
  {
    std::cerr << "Failed to bind to port 4221\n";
    return 1;
  }

  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0)
  {
    std::cerr << "listen failed\n";
    return 1;
  }

  std::cout << "Waiting for a client to connect...\n";
  ThreadPool pool(4);

  while (true)
  {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client < 0)
    {
      std::cerr << "accept failed\n";
      continue;
    }

    pool.enqueue([client, argc, argv]()
                 { handleClient(client, argc, argv); });
  }

  close(server_fd);
  return 0;
}
