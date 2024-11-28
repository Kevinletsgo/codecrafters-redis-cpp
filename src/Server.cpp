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
#include <mutex>
#include <vector>
#include <sstream>
#include <unordered_map>

using std::cout;
std::mutex logMutex; // 控制日志输出的互斥锁
std::vector<std::thread> threads; // 存储所有线程
std::unordered_map<std::string, std::string> umap;

void parserRedis(std::string& input, std::vector<std::string> &result) {
  std::istringstream stream(input);
  // std::cout << "input:" << input << '\n';
  
  std::string line;
  std::getline(stream, line, '\r');
  stream.get();
  std::cout << "line:" << line << '\n';
    if (line.empty() || line[0] != '*') {
      throw std::invalid_argument("Invalid RESP input: Expected '*'");
  }
  int numEle = std::stoi(line.substr(1));//"*2\r\n$2\r\nhi\r\n$4\r\nbaby\r\n"
  for (int i = 0; i < numEle; ++i) {
    // Read the bulk string header 
    std::getline(stream, line, '\r');
    stream.get();
    if (line.empty() || line[0] != '$') {
      throw std::invalid_argument("Invalid RESP input: Expected '$'");
    }
    int strLength = std::stoi(line.substr(1)); // Extract string length
    // Read the string content
    std::getline(stream, line, '\r');
    stream.get();
    result.emplace_back(line); // Add the string to result
  }
}
void handleClient(int client_fd) {
    std::vector<std::string> result;//作为局部变量避免被复用
    char buffer[1024];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytesReceived = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) {
            std::lock_guard<std::mutex> lock(logMutex);
            std::cout << "Client disconnected." << std::endl;
            break;
        }
        std::lock_guard<std::mutex> lock(logMutex);
        std::string input = buffer;
        std::string response;
        //result.clear();//很关键
        parserRedis(input, result);
        //reply Echo
        if(strcasecmp(result[0].c_str(), "Echo") == 0) {
          cout << "Echo success!"<< '\n';
          std::ostringstream oss;
          for(int i = 1; i < result.size(); ++i) {
            auto buf = result[i];
            oss << "$"  << buf.size() << "\r\n" << buf <<"\r\n";
          }
          response = oss.str();
        }
        //reply Ping
        if(strcasecmp(result[0].c_str(),"PING") == 0) {
          response = "+PONG\r\n";
          
        }
        //reply set
        if(strcasecmp(result[0].c_str(), "SET") == 0) {
          
            //std::lock_guard<std::mutex> lock(logMutex);
            umap[result[1]] = result[2];
          response = "+OK\r\n";
        }
        //reply get
        if(strcasecmp(result[0].c_str(), "GET") == 0) {//$3\r\nbar\r\n
          std::string str = umap[result[1]];
          std::ostringstream oss;
          oss << "$" << str.size() << "\r\n" << str << "\r\n";
          response = oss.str();  
        }
        //send messages
        int ret = send(client_fd, response.c_str(), response.size(), 0); //不能用sizeof(res.c_str())因为这里计算的是指针的大小
        if(ret == -1) {
          std::cerr << "Send failed with errno: " << errno << std::endl;
        }
        result.clear();
    }
    close(client_fd); 
}
int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // You can use print statements as follows for debugging, they'll be visible when running tests.
  std::cout << "Logs from your program will appear here!\n";

  // Uncomment this block to pass the first stage
  
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
   std::cerr << "Failed to create server socket\n";
   return 1;
  }
  //
  // // Since the tester restarts your program quite often, settin./g SO_REUSEADDR
  // // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }
  
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(6379);
  
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 6379\n";
    return 1;
  }
  
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }
  std::cout << "Waiting for a client to connect...\n";
  
  // accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
  while (true) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

    threads.emplace_back(std::thread(handleClient, client_fd));
  }

  for (auto& t : threads) {
    if (t.joinable()) {
        t.join();
    }
  }
  close(server_fd);

  return 0;
}


