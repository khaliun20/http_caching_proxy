#include <arpa/inet.h>
#include <assert.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include "cache.hpp"
#include "client.hpp"
// #include "webServer.hpp"
namespace http = boost::beast::http;
namespace asio = boost::asio;
namespace beast = boost::beast;
class Proxy {
  int total_id;
  std::ofstream log_stream;
  int socket_fd;
  pthread_mutex_t log_lock;
  Cache proxy_cache;

  void handle_exit_error(const char * mess) {
    std::cerr << "Error from building up proxy: " << mess << std::endl;
    exit(EXIT_FAILURE);
  }

  std::string get_first_line(std::vector<char> & mes) {
    char chunk_end[] = {'\r', '\n'};
    auto first_line_ptr = std::search(mes.begin(), mes.end(), chunk_end, chunk_end + 2);
    std::string first_line(mes.begin(), first_line_ptr);
    return first_line;
  }

 public:
  Proxy(const char * port) :
      total_id(0),
      log_stream(std::ofstream("/var/log/erss/proxy.log")),
      log_lock(PTHREAD_MUTEX_INITIALIZER),
      proxy_cache(&log_lock) {
    //Set up listening proxy
    int status;
    struct addrinfo host_info;
    struct addrinfo * host_info_list;
    const char * hostname = NULL;
    memset(&host_info, 0, sizeof(host_info));
    memset(&host_info_list, 0, sizeof(host_info_list));
    host_info.ai_family = AF_UNSPEC;
    host_info.ai_socktype = SOCK_STREAM;
    host_info.ai_flags = AI_PASSIVE;

    status = getaddrinfo(hostname, port, &host_info, &host_info_list);
    if (status != 0) {
      handle_exit_error("Cannot get address info for proxy");
    }
    socket_fd = socket(host_info_list->ai_family,
                       host_info_list->ai_socktype,
                       host_info_list->ai_protocol);
    if (socket_fd == -1) {
      handle_exit_error("Cannot create a listening socket");
    }
    int yes = 1;
    status = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
    if (status == -1) {
      handle_exit_error("Address is already in use");
    }
    status = bind(socket_fd, host_info_list->ai_addr, host_info_list->ai_addrlen);
    if (status == -1) {
      handle_exit_error("Cannot bind socket");
    }
    status = listen(socket_fd, 100);
    if (status == -1) {
      handle_exit_error("Cannot listen on socket");
    }
    freeaddrinfo(host_info_list);
  }

  int try_accept(std::string & ip_add) {
    struct sockaddr_storage socket_addr;
    socklen_t socket_addr_len = sizeof(socket_addr);
    int client_fd = accept(socket_fd, (struct sockaddr *)&socket_addr, &socket_addr_len);
    if (client_fd == -1) {
      std::cerr << "Error on Accept: cannot accept a connection" << std::endl;
    }
    ip_add = inet_ntoa(((struct sockaddr_in *)&socket_addr)->sin_addr);
    return client_fd;
  }

  std::string get_port(beast::string_view & req) {
    std::string port;
    if (req.starts_with("http:")) {
      port = "http";
    }
    else if (req.starts_with("https:")) {
      port = "https";
    }
    else if (req.find(":") != std::string::npos) {
      port = req.substr(req.find(":") + 1).to_string();
    }
    else {
      port = "http";
    }
    return port;
  }

  void post_method(WebServer & to_server,
                   Client & client,
                   std::vector<char> & mes_from_client,
                   std::string & command) {
    http::response<http::dynamic_body> response;
    std::vector<char> mes_from_server;
    to_server.send_message(mes_from_client);

    pthread_mutex_lock(&log_lock);
    log_stream << client.id << ": Requesting " << command << " from "
               << to_server.hostname << std::endl;
    pthread_mutex_unlock(&log_lock);

    to_server.receive_all(response, mes_from_server);
    std::string res_first = get_first_line(mes_from_server);
    pthread_mutex_lock(&log_lock);
    log_stream << client.id << ": Received " << res_first << " from "
               << to_server.hostname << std::endl;
    pthread_mutex_unlock(&log_lock);

    client.send_message(mes_from_server);

    pthread_mutex_lock(&log_lock);
    log_stream << client.id << ": Responding " << res_first << std::endl;
    pthread_mutex_unlock(&log_lock);
  }

  void connect_method(WebServer & to_server, Client & client) {
    std::vector<char> mes_from_client;
    std::vector<char> mes_from_server;
    client.send_ok();
    pthread_mutex_lock(&log_lock);
    log_stream << client.id << ": Responding \"HTTP/1.1 200 OK\"" << std::endl;
    pthread_mutex_unlock(&log_lock);
    fd_set select_fds;
    int max_fd = std::max(client.socket_fd, to_server.socket_fd);
    while (true) {
      FD_ZERO(&select_fds);
      FD_SET(client.socket_fd, &select_fds);
      FD_SET(to_server.socket_fd, &select_fds);
      int status = select(max_fd + 1, &select_fds, NULL, NULL, NULL);
      if (status > 0) {
        if (FD_ISSET(client.socket_fd, &select_fds)) {
          mes_from_client.clear();
          if (client.directly_receive(mes_from_client) <= 0) {
            break;
          }
          to_server.send_message(mes_from_client);
        }
        if (FD_ISSET(to_server.socket_fd, &select_fds)) {
          mes_from_server.clear();
          if (to_server.directly_receive(mes_from_server) <= 0) {
            break;
          }
          client.send_message(mes_from_server);
        }
      }
      else {
        std::cerr << "Error invalid select!" << std::endl;
      }
    }
    pthread_mutex_lock(&log_lock);
    log_stream << client.id << ": Tunnel closed\n";
    pthread_mutex_unlock(&log_lock);
  }

  void get_method(Client & client,
                  http::request<http::dynamic_body> & request,
                  std::vector<char> & mes_from_client,
                  std::string & port,
                  std::string & host_name,
                  std::string & command) {
    std::vector<char> mes_from_server;
    if (proxy_cache.find_valid(
            request, mes_from_client, mes_from_server, client.id, log_stream)) {
      client.send_message(mes_from_server);
      std::string res_first = get_first_line(mes_from_server);
      pthread_mutex_lock(&log_lock);
      log_stream << client.id << ": Responding " << res_first << std::endl;
      pthread_mutex_unlock(&log_lock);
    }
    else {  //connect to server to recieve message
      http::response<http::dynamic_body> response;
      WebServer to_server(host_name.c_str(), port.c_str());
      to_server.send_message(mes_from_client);
      pthread_mutex_lock(&log_lock);
      log_stream << client.id << ": Requesting " << command << " from "
                 << to_server.hostname << std::endl;
      pthread_mutex_unlock(&log_lock);

      to_server.receive_all(response, mes_from_server);
      std::string res_first = get_first_line(mes_from_server);

      pthread_mutex_lock(&log_lock);
      log_stream << client.id << ": Received " << res_first << " from "
                 << to_server.hostname << std::endl;
      pthread_mutex_unlock(&log_lock);

      client.send_message(mes_from_server);

      pthread_mutex_lock(&log_lock);
      log_stream << client.id << ": Responding " << res_first << std::endl;
      pthread_mutex_unlock(&log_lock);
      proxy_cache.try_add(request, response, mes_from_server, client.id, log_stream);
    }
  }

  void log_begion(Client & client,
                  std::vector<char> & mes_from_client,
                  std::string & command) {
    command = get_first_line(mes_from_client);
    std::time_t current_time = time(NULL);
    char * current_gmt = asctime(gmtime(&current_time));
    pthread_mutex_lock(&log_lock);
    log_stream << client.id << ": " << command << " from " << client.ip << " @ "
               << current_gmt;
    pthread_mutex_unlock(&log_lock);
  }

  void one_connection(int client_socket, std::string client_ip, int client_id) {
    http::request<http::dynamic_body> request;
    std::vector<char> mes_from_client;
    Client client(client_socket, client_id, client_ip);
    try {
      client.receive_header(request, mes_from_client);
      std::string command;
      log_begion(client, mes_from_client, command);

      beast::string_view req = request.target();
      std::string port = get_port(req);
      std::string host_name = request[http::field::host].to_string();
      if (host_name.find(":") != std::string::npos) {
        host_name = host_name.substr(0, host_name.find(":"));
      }
      if (request.method_string() == "POST") {
        WebServer to_server(host_name.c_str(), port.c_str());
        post_method(to_server, client, mes_from_client, command);
      }
      else if (request.method_string() == "CONNECT") {
        WebServer to_server(host_name.c_str(), port.c_str());
        connect_method(to_server, client);
      }
      else if (request.method_string() == "GET") {
        get_method(client, request, mes_from_client, port, host_name, command);
      }
      else {
        throw std::invalid_argument("Undefined Method");
      }
    }
    catch (std::invalid_argument & ex) {
      client.send_400();
      pthread_mutex_lock(&log_lock);
      log_stream << client.id << ": Responding \"HTTP/1.1 400 Bad Request\"" << std::endl;
      pthread_mutex_unlock(&log_lock);
      // std::cerr<<ex.what()<<std::endl;
    }
    catch (std::range_error & ex) {
      client.send_502();
      pthread_mutex_lock(&log_lock);
      log_stream << client.id << ": Responding \"HTTP/1.1 502 Bad Gateway\"" << std::endl;
      pthread_mutex_unlock(&log_lock);
      //  std::cerr<<ex.what()<<std::endl;
    }
    catch (std::exception & e) {
      std::cerr << "Exception from One Connection: " << e.what() << std::endl;
    }
  }

  void run_service() {
    while (true) {
      std::string client_ip;
      int client_socket = try_accept(client_ip);
      if (client_socket != -1) {
        int client_id = total_id;
        total_id++;
        try {
          std::thread t(
              &Proxy::one_connection, this, client_socket, client_ip, client_id);
          t.detach();
        }
        catch (std::exception & e) {
          std::cerr << "Exception from creating thread: " << e.what() << std::endl;
        }
      }
    }
  }
};