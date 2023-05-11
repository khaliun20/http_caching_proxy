#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>
// #include <assert.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <stdexcept>

#include <thread>
#include <vector>
namespace http = boost::beast::http;
namespace asio = boost::asio;
namespace beast = boost::beast;
class Client {
 public:
  int socket_fd;  //for talking to client
  int id;
  std::string ip;
  /*
  *Construct a client object to communicate with one client (per request)
  *@param csfd is the input socket_fd to talk to client
  *It needs to initialize socket_fd
  *It should call receive_header(), parse it, and use the results to initialize header
  */
  Client(int csfd, int client_id, std::string client_ip) :
      socket_fd(csfd), id(client_id), ip(client_ip) {}
  /*
  *Use the recv() function to receive all header message from client using a large buffer (2^16)
  *We need to check if it receive all header content by checking "\r\n\r\n"
  *parse the header file and extract host_name, method, and path to save
  *save the whole message in to the content file of header
  */
  int directly_receive(std::vector<char> & message) {
    char buffer[65536];
    memset(&buffer, 0, sizeof(buffer));
    int actual_len = recv(socket_fd, buffer, sizeof(buffer), 0);
    // if (actual_len == -1) {
    //   std::cerr << "Cannot receive from client\n";
    // }
    if(actual_len>0){
      std::copy(buffer, buffer + actual_len, std::back_inserter(message));
    }
    return actual_len;
  }

  int receive_header(http::request<http::dynamic_body> & request,
                     std::vector<char> & message) {
    char buffer[65536];
    memset(&buffer, 0, sizeof(buffer));
    int actual_len = recv(socket_fd, buffer, sizeof(buffer), 0);
    message.insert(message.end(), buffer, buffer + actual_len);
    boost::beast::error_code ec;
    http::request_parser<http::dynamic_body> myparser;
    myparser.put(asio::buffer(message), ec);
    if(ec.value()!=0){
      throw std::invalid_argument(ec.message());
    }
    if(!myparser.is_header_done()){
      throw std::invalid_argument("Bad request from client");
    }

    if (myparser.content_length().has_value()) {
      char chunk_end[] = {'\r', '\n', '\r', '\n'};
      auto header_end =
          std::search(message.begin(), message.end(), chunk_end, chunk_end + 4);
      int has_received_len = message.end() - header_end - 4;
      char continue_buffer[myparser.content_length().value()];
      while (has_received_len < myparser.content_length().value()) {
        memset(&continue_buffer, 0, sizeof(continue_buffer));
        actual_len = recv(socket_fd, continue_buffer, sizeof(continue_buffer), 0);
        if (actual_len <= 0) {
          throw std::invalid_argument("Bad request from client: invalid length of body (less)!");
        }
        has_received_len += actual_len;
        std::copy(
            continue_buffer, continue_buffer + actual_len, std::back_inserter(message));
      }
      //Also need to disucss if content length not match after that. If not --> thorw
      if(has_received_len>myparser.content_length().value()){
        throw std::invalid_argument("Bad request from client: invalid length of body (more)!");
      }
    }
    else if (myparser.chunked()) {
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
          throw std::invalid_argument("Bad request from client: invalid chunks!");
        }
        std::copy(buffer, buffer + actual_len, std::back_inserter(message));
      }
    }
    http::request_parser<http::dynamic_body> final_parser;
    final_parser.put(asio::buffer(message), ec);
    request = final_parser.get();
    if(ec.value()!=0){
      throw std::invalid_argument(ec.message());
    }
    return actual_len;
  }


  void send_message(std::vector<char> & mesg) {
    const char * meg_c = mesg.data();
    int status = send(socket_fd, meg_c, mesg.size(), 0);
  }
  void send_ok() {
    const char * meg_c = "HTTP/1.1 200 OK\r\n\r\n";
    int status = send(socket_fd, meg_c, strlen(meg_c), 0);
  }
  void send_400(){
    std::string err_400 =
        "HTTP/1.1 400 Bad Request\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 13\r\n\r\n"
        "Bad Request\r\n";
    int status = send(socket_fd, err_400.c_str(), err_400.size(), 0);
  }
  void send_502(){
    std::string err_502="HTTP/1.1 502 Bad Gateway\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 12\r\n\r\n"
    "Bad Gateway\r\n";
    int status = send(socket_fd, err_502.c_str(), err_502.size(), 0);

  }

  /*
  *it should send body and header from web to the client
  *directly use .length method to get length of the data to send 
  */
  // void send_all(std::string message);

  /*
  *receive unspecific length of message from user for the CONNECT method
  *use while loop and fixed buffer to receive all messages until the status == 0;
  *after while loop, check if all messages are received by checking the \r\n\r\n
  *it should return the message received   
  */
  // std::string receive_all();

  /* Before destructing close the client's socket_fd*/
  ~Client() { close(socket_fd); }
};
