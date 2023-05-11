#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <pthread.h>

#include <chrono>
#include <ctime>
#include <iostream>
#include <map>
#include <vector>

#include "webServer.hpp"
namespace http = boost::beast::http;
namespace asio = boost::asio;
namespace beast = boost::beast;
class Cache {
  std::map<std::string, std::vector<char> > caching_dic;
  std::vector<std::string> key_order;  //record the order of use
  int max_length;                      //the maximum number of data in the cache
  pthread_rwlock_t cache_lock;
  pthread_mutex_t * log_lock;
  // compare with max-age
  bool less_than_age(std::string & response_date, long max_age) {
    std::chrono::duration<double> max_duration = std::chrono::seconds(max_age);
    std::tm tm = {};
    strptime(response_date.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    std::time_t response_time = std::mktime(&tm);
    std::time_t current_time =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::time_t current_GMT = std::mktime(std::gmtime(&current_time));
    std::chrono::duration<double> age =
        std::chrono::duration<double>(current_GMT - response_time);
    return age < max_duration;
  }

  std::string get_expired_time(std::string & response_date, long max_age) {
    std::tm tm = {};
    strptime(response_date.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &tm);    
    tm.tm_sec += max_age;
    std::time_t response_time = std::mktime(&tm);
    return ctime(&response_time);
  }

  // compare with expires
  bool is_expire(std::string & expire_time) {
    std::tm tm = {};
    strptime(expire_time.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    std::time_t expire_time_GMT = std::mktime(&tm);
    std::time_t current_time =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::time_t current_GMT = std::mktime(std::gmtime(&current_time));
    return expire_time_GMT < current_GMT;
  }

  //Extract key from request object
  std::string generate_key(http::request<http::dynamic_body> & request) {
    return request["Host"].to_string() + " " + request.target().to_string() + " " +
           std::to_string(request.version());
  }

  void add_in_cache(std::string & key_value, std::vector<char> & mes_from_server) {
    caching_dic[key_value] = mes_from_server;

    //If the key is already exist --> push to the end
    for (int i = 0; i < key_order.size(); i++) {
      if (key_order[i] == key_value) {  //the key exists
        key_order.erase(key_order.begin() + i);
        key_order.push_back(key_value);
        return;
      }
    }
    // add the new key value:
    key_order.push_back(key_value);
    //If the number of cached responses is more than the limit
    if (key_order.size() > max_length) {
      caching_dic.erase(key_order[0]);  //remove it
      key_order.erase(key_order.begin());
    }
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
  std::string get_first_line(std::vector<char> & mes) {
    char chunk_end[] = {'\r', '\n'};
    auto first_line_ptr = std::search(mes.begin(), mes.end(), chunk_end, chunk_end + 2);
    std::string first_line(mes.begin(), first_line_ptr);
    return first_line;
  }

  bool validation(http::request<http::dynamic_body> & request,
                  http::response<http::dynamic_body> & response,
                  std::vector<char> & mes_from_client,
                  std::vector<char> & saved_mes,
                  int client_id,
                  std::ofstream & log_stream) {
    beast::string_view req = request.target();
    std::string port = get_port(req);
    std::string host_name = request[http::field::host].to_string();
    if (host_name.find(":") != std::string::npos) {
      host_name = host_name.substr(0, host_name.find(":"));
    }
    WebServer to_server(host_name.c_str(), port.c_str());
    std::vector<char> mes_to_valid = mes_from_client;
    char chunk_end[] = {'\r', '\n', '\r', '\n'};
    if (response.find("Last-Modified") != response.end() &&
        request.find("If-Modified-Since") == request.end()) {
      //Insert If-Modified-Since:
      std::string mod_header =
          "If-Modified-Since: " + response["Last-Modified"].to_string() + "\r\n";
      // std::cout << mod_header;
      auto header_end =
          std::search(mes_to_valid.begin(), mes_to_valid.end(), chunk_end, chunk_end + 4);
      mes_to_valid.insert(header_end + 2, mod_header.begin(), mod_header.end());
    }
    if (response.find("ETag") != response.end() &&
        request.find("If-None-Match") == request.end()) {
      std::string match_header =
          "If-None-Match: " + response["ETag"].to_string() + "\r\n";
      // std::cout << match_header;
      auto header_end =
          std::search(mes_to_valid.begin(), mes_to_valid.end(), chunk_end, chunk_end + 4);
      mes_to_valid.insert(header_end + 2, match_header.begin(), match_header.end());
    }

    to_server.send_message(mes_to_valid);
    std::string valid_first_line = get_first_line(mes_to_valid);
    pthread_mutex_lock(log_lock);
    log_stream << client_id << ": Requesting " << valid_first_line << " from "
               << to_server.hostname << std::endl;
    pthread_mutex_unlock(log_lock);
    http::response<http::dynamic_body> server_res;
    std::vector<char> mes_from_server;
    to_server.receive_all(server_res, mes_from_server);
    std::string response_first_line = get_first_line(mes_from_server);
    pthread_mutex_lock(log_lock);
    log_stream << client_id << ": Received " << response_first_line << " from "
               << to_server.hostname << std::endl;
    pthread_mutex_unlock(log_lock);
    if (server_res.result() == http::status::not_modified) {
      // std::cout << "still valid with result int: " << server_res.result_int()
      //           << std::endl;
      return true;
    }
    else if (server_res.result() == http::status::ok) {
      saved_mes = mes_from_server;
      try_add(request, server_res, mes_from_server, client_id, log_stream);
      return true;
    }
    else {
      
      return false;
    }
  }

 public:
  Cache(pthread_mutex_t * l_lock) :
      max_length(1000), cache_lock(PTHREAD_RWLOCK_INITIALIZER), log_lock(l_lock) {}
  bool find_valid(http::request<http::dynamic_body> & request,
                  std::vector<char> & mes_from_client,
                  std::vector<char> & mes_from_cache,
                  int client_id,
                  std::ofstream & log_stream) {
    http::response_parser<http::dynamic_body> myparser;
    boost::beast::error_code ec;
    std::string target = generate_key(request);
    bool already_check = false;  //already checked time in max-age
    bool had_found = false;
    std::vector<char> saved_mes;

    pthread_rwlock_rdlock(&cache_lock);
    if(caching_dic.find(target) != caching_dic.end()){
    saved_mes= caching_dic[target];
    had_found=true;
    }  
    pthread_rwlock_unlock(&cache_lock);

    if (had_found) {       
      myparser.put(asio::buffer(saved_mes), ec);
      http::response<http::dynamic_body> response = myparser.get();

      if (response.find("Cache-Control") != response.end()) {
        std::string response_date = response["date"].to_string();
        beast::string_view cache_control = response["Cache-Control"];
        if (cache_control.find("no-cache") != beast::string_view::npos) {
          pthread_mutex_lock(log_lock);
          log_stream << client_id << ": in cache, requires validation" << std::endl;
          pthread_mutex_unlock(log_lock);
          if (!validation(request, response, mes_from_client,saved_mes, client_id, log_stream)) {
            return false;  //the result is no longer valid
          }
          already_check = true;
        }
        //check for must-revalidate and proxy-revalidate
        else if (cache_control.find("revalidate") != beast::string_view::npos) {
          pthread_mutex_lock(log_lock);
          log_stream << client_id << ": in cache, requires validation" << std::endl;
          pthread_mutex_unlock(log_lock);
          if (!validation(request, response, mes_from_client,saved_mes, client_id, log_stream)) {
            return false;  //the result is no longer valid
          }
          already_check = true;
        }
        else if (cache_control.find("max-age") != beast::string_view::npos) {
          beast::string_view max_age_bst =
              cache_control.substr(cache_control.find("max-age") + 8);
          long max_age = std::stol(max_age_bst.to_string());
          if (!less_than_age(response_date, max_age)) {
            std::string expire_time = get_expired_time(response_date, max_age);
            pthread_mutex_lock(log_lock);
            log_stream << client_id << ": in cache, but expired at " << expire_time;
            pthread_mutex_unlock(log_lock);
            if (!validation(request, response, mes_from_client, saved_mes,client_id, log_stream)) {
              return false;
            }
          }
          already_check = true;  //no need to check expires
        }
      }
      if (response.find("expires") != response.end() && (!already_check)) {
        std::string expires = response["expires"].to_string();
        if (expires != "-1") {
          already_check = true;
          if (is_expire(expires)) {
            std::string expire_time = get_expired_time(expires, 0);
            pthread_mutex_lock(log_lock);
            log_stream << client_id << ": in cache, but expired at " << expire_time;
            pthread_mutex_unlock(log_lock);
            if (!validation(request, response, mes_from_client, saved_mes,client_id, log_stream)) {
              return false;
            }
          }
        }
      }
      pthread_rwlock_wrlock(&cache_lock);
      for (int i = 0; i < key_order.size(); i++) {
        if (key_order[i] == target) {  //find the postion
          key_order.erase(key_order.begin() + i);
          key_order.push_back(target);  // push it to the end
          break;
        }
      }
      pthread_rwlock_unlock(&cache_lock);
      
      mes_from_cache = saved_mes;
      if (!already_check) {
        pthread_mutex_lock(log_lock);
        log_stream << client_id << ": in cache, valid" << std::endl;
        pthread_mutex_unlock(log_lock);
      }

      return true;
    }
    pthread_mutex_lock(log_lock);
    log_stream << client_id << ": not in cache\n";
    pthread_mutex_unlock(log_lock);
    return false;
  }
  bool try_add(http::request<http::dynamic_body> & request,
               http::response<http::dynamic_body> & response,
               std::vector<char> & mes_from_server,
               int client_id,
               std::ofstream & log_stream) {
    bool need_val = false;
    // the response has a status code that is defined as cacheable (200)
    if (response.result_int() != 200) {
      return false;
    }
    if (response.find("Cache-Control") != response.end()) {
      beast::string_view cache_control = response["Cache-Control"];
      // the "no-store" cache directive does not appear
      if (cache_control.find("no-store") != beast::string_view::npos) {
        pthread_mutex_lock(log_lock);
        log_stream << client_id << ": not cacheable because no-store" << std::endl;
        pthread_mutex_unlock(log_lock);
        return false;
      }
      // the "private" response directive does not appear
      if (cache_control.find("private") != beast::string_view::npos) {
        pthread_mutex_lock(log_lock);
        log_stream << client_id << ": not cacheable because private" << std::endl;
        pthread_mutex_unlock(log_lock);
        return false;
      }
      // when max-age is -1, it is not cacheable
      if (cache_control.find("max-age=-1") != beast::string_view::npos) {
        pthread_mutex_lock(log_lock);
        log_stream << client_id << ": not cacheable because max-age=-1" << std::endl;
        pthread_mutex_unlock(log_lock);
        return false;
      }
      if (cache_control.find("no-cache") != beast::string_view::npos) {
        pthread_mutex_lock(log_lock);
        log_stream << client_id << ": cached, but requires re-validation" << std::endl;
        pthread_mutex_unlock(log_lock);
        need_val = true;
      }
      //check for must-revalidate and proxy-revalidate
      else if (cache_control.find("revalidate") != beast::string_view::npos) {
        pthread_mutex_lock(log_lock);
        log_stream << client_id << ": cached, but requires re-validation" << std::endl;
        pthread_mutex_unlock(log_lock);
        need_val = true;
      }
      else if (cache_control.find("max-age") != beast::string_view::npos) {
        beast::string_view max_age_bst =
            cache_control.substr(cache_control.find("max-age") + 8);
        std::string response_date = response["date"].to_string();
        long max_age = std::stol(max_age_bst.to_string());
        std::string expire_time = get_expired_time(response_date, max_age);
        pthread_mutex_lock(log_lock);
        log_stream << client_id << ": cached, expires at " << expire_time;
        pthread_mutex_unlock(log_lock);
        need_val = true;
      }
    }
    //Add to cache:
    //generate key from request
    std::string target = generate_key(request);
    //add to the map to save
    pthread_rwlock_wrlock(&cache_lock);
    add_in_cache(target, mes_from_server);
    pthread_rwlock_unlock(&cache_lock);

    if (response.find("expires") != response.end() && !need_val) {
      std::string expires = response["expires"].to_string();
      if (expires != "-1") {
        std::string expire_time = get_expired_time(expires, 0);
        pthread_mutex_lock(log_lock);
        log_stream << client_id << ": cached, expires at " << expire_time;
        pthread_mutex_unlock(log_lock);
        need_val = true;
      }
    }

    if (!need_val) {
      pthread_mutex_lock(log_lock);
      log_stream << client_id << ": cached\n";
      pthread_mutex_unlock(log_lock);
    }

    return true;
  }
};