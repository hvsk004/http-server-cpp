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

class ThreadPool {
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

ThreadPool::ThreadPool(size_t numThreads) : stop(false) {
    for (size_t i = 0; i < numThreads; i++) {
        workers.emplace_back([this] { this->worker(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stop = true;
    }
    condition.notify_all();
    for (std::thread &worker : workers) {
        worker.join();
    }
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        tasks.emplace(std::move(task));
    }
    condition.notify_one();
}

void ThreadPool::worker() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            condition.wait(lock, [this] { return stop || !tasks.empty(); });
            if (stop && tasks.empty()) {
                return;
            }
            task = std::move(tasks.front());
            tasks.pop();
        }
        task();
    }
}

void handleClient(int client,int argc, char **argv) {
    std::cout << "Client connected\n";
    
    char buffer[1024] = {0};
    read(client, buffer, 1024);
    std::string request(buffer);
    std::cout << "Request: " << request << std::endl;

    std::string path = request.substr(request.find("GET") + 4, request.find("HTTP") - 5);
    std::cout << "Path: " << path << std::endl;

    std::string notFoundError = "HTTP/1.1 404 Not Found\r\n\r\n";
    std::string contentResponse = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: ";
    std::string contentResponse2 = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: ";

    if (path.find("echo") != std::string::npos) {
        std::string input = path.substr(path.find("echo") + 5);
        std::cout << input << std::endl;
        std::string response = contentResponse + std::to_string(input.size()) + "\r\n\r\n" + input;
        send(client, response.c_str(), response.size(), 0);
    } else if (path == "/") {
        std::string message = "HTTP/1.1 200 OK\r\n\r\n";
        send(client, message.c_str(), message.size(), 0);
    } else if (path == "/user-agent") {
        std::string input = request.substr(request.find("User-Agent:") + 12);
        input = input.substr(0, input.find("\r\n"));
        std::cout << "User Agent: " << input << std::endl;
        std::string response = contentResponse + std::to_string(input.size()) + "\r\n\r\n" + input;
        send(client, response.c_str(), response.size(), 0);
    }
    else if(path.find("file") != std::string::npos) {
        std::string filename = path.substr(path.find("file") + 5);
        std::cout << "Filename: " << filename << std::endl;
        std::string directory = argc > 1 ? argv[2] : "./"; // Default to current directory if no argument is 
        std::cout << "Directory : " << directory << std::endl;
        std::fstream file(directory + filename, std::ios::in);
        if(file.is_open()) {
            std::string content = "";
            std::string line;
            while(getline(file,line)) {
                content += line;
            }
            content = content.substr(0,content.find("\r\n"));
            std::string response = contentResponse2 + std::to_string(content.size()) + "\r\n\r\n" + content;
            send(client, response.c_str(), response.size(), 0);
        }
        else {
            send(client, notFoundError.c_str(), notFoundError.size(), 0);
        }
    } else {
        send(client, notFoundError.c_str(), notFoundError.size(), 0);
    }

    close(client);
}

int main(int argc, char **argv) {
    
    std::cout << std::unitbuf; // Ensure all output is flushed immediately
    std::cerr << std::unitbuf;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }

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

    std::cout << "Waiting for a client to connect...\n";
    ThreadPool pool(4); // Create a thread pool with 4 worker threads

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
        if (client < 0) {
            std::cerr << "accept failed\n";
            continue; // Just continue on failure to accept
        }

        // Enqueue the client handling task
        pool.enqueue([client,argc,argv]() {
            handleClient(client,argc,argv);
        });
    }

    close(server_fd);

    return 0;
}
