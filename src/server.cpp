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
#include <map>
#include <sstream>
#include <zlib.h>

std::string compressGzip(const std::string &str, int compression_level = Z_BEST_COMPRESSION)
{
  z_stream zs;
  memset(&zs, 0, sizeof(zs));
  if (deflateInit2(&zs, compression_level, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
  {
    throw std::runtime_error("deflateInit2 failed.");
  }

  zs.next_in = (Bytef *)str.data();
  zs.avail_in = str.size();
  int ret;
  char outbuffer[32768];
  std::string outstring;

  do
  {
    zs.next_out = reinterpret_cast<Bytef *>(outbuffer);
    zs.avail_out = sizeof(outbuffer);
    ret = deflate(&zs, Z_FINISH);

    if (outstring.size() < zs.total_out)
    {
      outstring.append(outbuffer, zs.total_out - outstring.size());
    }
  } while (ret == Z_OK);

  deflateEnd(&zs);

  if (ret != Z_STREAM_END)
  {
    throw std::runtime_error("Exception during zlib compression.");
  }

  return outstring;
}

class HttpRequest
{
public:
  std::string method;
  std::string path;
  std::string body;
  std::map<std::string, std::string> headers;
  HttpRequest(const std::string &request)
  {
    parseRequest(request);
  }

private:
  void parseRequest(const std::string &request)
  {
    method = request.substr(0, request.find(" "));
    path = request.substr(request.find(" ") + 1, request.find("HTTP") - request.find(" ") - 2);

    // Extract body for POST requests
    if (method == "POST")
    {
      body = request.substr(request.find("\r\n\r\n") + 4);
    }

    // Extract headers
    size_t headersStart = request.find("\r\n") + 2;
    size_t headersEnd = request.find("\r\n\r\n");
    if (headersStart == std::string::npos || headersEnd == std::string::npos || headersStart >= headersEnd)
    {
      // Handle error: invalid headers section
      return;
    }
    std::string headersSection = request.substr(headersStart, headersEnd - headersStart);
    size_t pos = 0;
    while ((pos = headersSection.find("\r\n")) != std::string::npos)
    {
      std::string line = headersSection.substr(0, pos);
      size_t colonPos = line.find(": ");
      if (colonPos != std::string::npos)
      {
        std::string key = line.substr(0, colonPos);
        std::string value = line.substr(colonPos + 2);
        headers[key] = value;
      }
      headersSection.erase(0, pos + 2);
    }
    if (!headersSection.empty())
    {
      size_t colonPos = headersSection.find(": ");
      if (colonPos != std::string::npos)
      {
        std::string key = headersSection.substr(0, colonPos);
        std::string value = headersSection.substr(colonPos + 2);
        headers[key] = value;
      }
    }
  }
};
class HttpResponse
{
public:
  std::string status;
  std::map<std::string, std::string> headers;
  std::string body;

  HttpResponse() : status("HTTP/1.1 200 OK") {}

  std::string toString()
  {
    std::string response = status + "\r\n";
    for (const auto &[key, value] : headers)
    {
      response += key + ": " + value + "\r\n";
    }
    response += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    return response;
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

std::vector<std::string> split(const std::string &s, char delimiter)
{
  std::vector<std::string> tokens;
  std::string token;
  std::istringstream tokenStream(s);
  while (std::getline(tokenStream, token, delimiter))
  {
    // trim the token to remove unecessary spaces infront and back
    token.erase(0, token.find_first_not_of(" \n\r\t"));
    token.erase(token.find_last_not_of(" \n\r\t") + 1);
    tokens.push_back(token);
  }
  return tokens;
}

void handleClient(int client, int argc, char **argv)
{
  std::cout << "Client connected\n";
  char buffer[1024 * 8] = {0};
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

  bool gzip = false;
  auto contentEncodingIt = request.headers.find("Accept-Encoding");

  if (contentEncodingIt != request.headers.end())
  {
    std::vector<std::string> encodingsArray = split(contentEncodingIt->second, ',');
    for (const auto &encoding : encodingsArray)
    {
      if (encoding == "gzip")
      {
        gzip = true;
        std::cout << "Setting gzip true" << std::endl;
        response.headers["Content-Encoding"] = "gzip";
        break;
      }
    }
  }

  // Handle GET requests
  if (request.method == "GET")
  {
    if (request.path == "/")
    {
      response.body = "Welcome!";
    }
    else if (request.path.find("echo") != std::string::npos)
    {
      std::string input = request.path.substr(request.path.find("echo") + 5);
      std::cout << "Echoing: " << input << std::endl;
      std::string output = gzip ? compressGzip(input) : input;
      response.body = output;
      std::cout << "Compressed body: " << output << std::endl;
      response.headers["Content-Type"] = "text/plain";
    }
    else if (request.path == "/user-agent")
    {
      response.body = request.headers["User-Agent"];
      response.headers["Content-Type"] = "text/plain";
    }
    else if (request.path.find("file") != std::string::npos)
    {
      std::string filename = request.path.substr(request.path.find("file") + 5);
      std::cout << "Filename: " << filename << std::endl;
      std::string directory = argc > 1 ? argv[2] : "./"; // Default to current directory if no argument is provided
      std::cout << "Directory: " << directory << std::endl;

      std::fstream file(directory + filename, std::ios::in);
      if (file.is_open())
      {
        std::string content;
        std::string line;

        while (getline(file, line))
        {
          content += line; // Append a newline for line separation
        }

        response.body = content;                                             // Set the content of the response body
        response.headers["Content-Type"] = "text/plain";                     // Set appropriate content type
        response.headers["Content-Length"] = std::to_string(content.size()); // Update Content-Length

        response.status = "HTTP/1.1 200 OK"; // Set response status to OK
        response.headers["Content-Type"] = "application/octet-stream";
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
  // Handle POST requests
  else if (request.method == "POST")
  {
    std::cout << "method : post" << std::endl;
    if (request.path.find("files") != std::string::npos)
    {
      std::string filename = request.path.substr(request.path.find("files") + 6);
      std::string directory = argc > 1 ? argv[2] : "./";
      std::string fullPath = directory + filename;

      // Use std::fstream with `std::ios::out` and `std::ios::app` flags to create file if it doesn't exist
      std::fstream file(fullPath, std::ios::out | std::ios::app);
      if (file.is_open())
      {
        file << request.body;
        file.close();
        response.status = "HTTP/1.1 201 Created";
        response.headers["Content-Type"] = "application/octet-stream";
        response.body = request.body;
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
  response.headers["Content-Length"] = std::to_string(response.body.size());

  // Send the response to the client
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
