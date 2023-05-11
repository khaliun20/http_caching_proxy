#include <arpa/inet.h>
#include <assert.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
namespace http = boost::beast::http;
namespace asio = boost::asio;
namespace beast = boost::beast;
class WebServer {
  void handle_throw_error(const char * mess) {
    // std::cerr << "Error from Connection: " << mess << std::endl;
    // std::cerr << "Hostname: " << hostname << " (" << port << ")" << std::endl;
    throw std::invalid_argument(mess);
  }

 public:
  const char * hostname;
  const char * port;
  int socket_fd;  // communicate with the web server
  WebServer(const char * hn, const char * p) : hostname(hn), port(p) {
    int status;
    struct timeval tv;
    tv.tv_sec = 120;  // 120 second timeout
    tv.tv_usec = 0;
    struct addrinfo host_info;
    struct addrinfo * host_info_list;
    memset(&host_info, 0, sizeof(host_info));
    host_info.ai_family = AF_UNSPEC;
    host_info.ai_socktype = SOCK_STREAM;
    //Get host infomation
    status = getaddrinfo(hostname, port, &host_info, &host_info_list);
    if (status != 0) {
      handle_throw_error("Get address info failed");
    }
    //Setup socket
    socket_fd = socket(host_info_list->ai_family,
                       host_info_list->ai_socktype,
                       host_info_list->ai_protocol);
    if (socket_fd == -1) {
      handle_throw_error("Socket building error");
    }
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
      handle_throw_error("Cannot set timeout for receive");
    }
    if (setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == -1) {
      handle_throw_error("Cannot set timeout for send");
    }
    //Build connection
    status = connect(socket_fd, host_info_list->ai_addr, host_info_list->ai_addrlen);
    if (status == -1) {
      handle_throw_error("Connection failed");
    }
    freeaddrinfo(host_info_list);
  }

  int directly_receive(std::vector<char> & message) {
    char buffer[65536];
    memset(&buffer, 0, sizeof(buffer));
    int actual_len = recv(socket_fd, buffer, sizeof(buffer), 0);
    if (actual_len > 0) {
      std::copy(buffer, buffer + actual_len, std::back_inserter(message));
    }
    return actual_len;
  }

  void receive_all(http::response<http::dynamic_body> & response,
                   std::vector<char> & message) {
    boost::beast::error_code ec;
    http::response_parser<http::dynamic_body> myparser;
    char buffer[65536];
    int actual_len;
    memset(&buffer, 0, sizeof(buffer));
    try {
      actual_len = recv(socket_fd, buffer, sizeof(buffer), 0);
      if (actual_len <= 0) {
        throw std::range_error("Cannot receive from server");
      }
      std::copy(buffer, buffer + actual_len, std::back_inserter(message));
      myparser.put(asio::buffer(message), ec);
      response = myparser.get();
      //If !myparser.is_header_done() --> should throw
      if (myparser.chunked()) {
        char chunk_end[] = {'0', '\r', '\n', '\r', '\n'};
        while (true) {
          auto is_end =
              std::search(message.begin(), message.end(), chunk_end, chunk_end + 5);
          if (is_end != message.end()) {           
            break;
          }
          memset(&buffer, 0, sizeof(buffer));
          actual_len = recv(socket_fd, buffer, sizeof(buffer), 0);
          if (actual_len <= 0) {
            throw std::range_error("Cannot receive valid length from server");
            // break;
          }
          std::copy(buffer, buffer + actual_len, std::back_inserter(message));
        }
      }
      else if (myparser.content_length().has_value()) {
        char chunk_end[] = {'\r', '\n', '\r', '\n'};
        auto header_end =
            std::search(message.begin(), message.end(), chunk_end, chunk_end + 4);
        int has_received_len = message.end() - header_end - 4;
        char continue_buffer[myparser.content_length().value()];
        while (has_received_len < myparser.content_length().value()) {
          memset(&continue_buffer, 0, sizeof(continue_buffer));
          actual_len = recv(socket_fd, continue_buffer, sizeof(continue_buffer), 0);
          if (actual_len <= 0) {
            throw std::range_error("Cannot receive valid length from server");
            // break;
          }
          has_received_len += actual_len;
          std::copy(
              continue_buffer, continue_buffer + actual_len, std::back_inserter(message));
        }
      }
    }
    catch (std::exception & e) {
      throw std::range_error(e.what());
    }
  }

  void send_message(std::vector<char> & mesg) {
    const char * meg_c = mesg.data();
    int status = send(socket_fd, meg_c, mesg.size(), 0);
    if (status == -1) {
       throw std::range_error("Cannot send message to server!");
    }
  }
  ~WebServer() { close(socket_fd); }
};
