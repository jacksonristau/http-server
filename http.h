#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

/*
 * Read an HTTP request from an active TCP connection socket
 * fd: The socket's file descriptor
 * resource_name: Set to the name of the requested resource on success
 * resource_size: Size of the resource_name buffer
 * Returns 0 on success, an HTTP error status (400, 405) if the request is
 * malformed or unsupported, or -1 if the connection failed
 */
int read_http_request(int fd, char *resource_name, size_t resource_size);

/*
 * Write an HTTP response to an active TCP connection socket
 * If the resource can't be served, an appropriate error response (403, 404,
 * 500) is sent instead
 * fd: The socket's file descriptor
 * resource_path: The path to the requested resource in the server's file system
 * Returns 0 on success or -1 if writing to the socket failed
 */
int write_http_response(int fd, const char *resource_path);

/*
 * Write an HTTP error response with a short text/plain body
 * fd: The socket's file descriptor
 * status: The HTTP status code to send
 * Returns 0 on success or -1 if writing to the socket failed
 */
int write_http_error(int fd, int status);

#endif // HTTP_H
