## HTTP Caching Proxy

### Overview

This project involves developing an HTTP caching proxy as part of the Robust Server Software class coursework at Duke. The proxy server forwards requests to the origin server on behalf of the client and caches responses to improve efficiency and performance. The proxy handles multiple concurrent requests, supports GET, POST, and CONNECT methods, and logs detailed information about each request and response.

### Tech Stack

- **Programming Language**: C++
- **Build System**: GNU Make
- **Threading**: C++ Standard Library `<thread>`
- **Networking**: POSIX Sockets API
- **Logging**: Custom logging to file
- **Containers**: Docker
- **Orchestration**: Docker Compose

### Features

**HTTP Methods:** Supports GET, POST, and CONNECT methods.

**Caching:** Caches responses for GET requests with 200-OK status. Implements expiration and re-validation rules.

**Concurrency:** Handles multiple concurrent requests using multi-threading. Shared cache synchronized between connections.

**Logging:** Produces a detailed log of all activities in /var/log/erss/proxy.log.

**Robustness:** Handles external failures gracefully, and responds with appropriate error codes for corrupted or malformed requests/responses.

**Docker:** Includes Dockerfile and docker-compose.yml for easy setup and testing.




