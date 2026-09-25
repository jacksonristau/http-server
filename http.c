#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include "http.h"

#define BUFSIZE 512
#define MAX_REQUEST_SIZE 8192

const char *get_mime_type(const char *resource_path) {
    const char *file_extension = strrchr(resource_path, '.');
    if (file_extension == NULL) {
        return "application/octet-stream";
    }

    if (strcmp(".txt", file_extension) == 0) {
        return "text/plain";
    } else if (strcmp(".html", file_extension) == 0) {
        return "text/html";
    } else if (strcmp(".jpg", file_extension) == 0) {
        return "image/jpeg";
    } else if (strcmp(".png", file_extension) == 0) {
        return "image/png";
    } else if (strcmp(".pdf", file_extension) == 0) {
        return "application/pdf";
    }

    return "application/octet-stream";
}

const char *get_status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        default:  return "Internal Server Error";
    }
}

// write all n bytes, retrying on short writes and interrupts
int write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t nbytes = write(fd, buf, n);
        if (nbytes == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("write");
            return -1;
        }
        buf += nbytes;
        n -= nbytes;
    }
    return 0;
}

int read_http_request(int fd, char *resource_name, size_t resource_size) {
    char buf[MAX_REQUEST_SIZE + 1];
    size_t total = 0;

    // read until the end of the headers or the size limit
    while (1) {
        if (total == MAX_REQUEST_SIZE) {
            return 400;
        }
        ssize_t nbytes = read(fd, buf + total, MAX_REQUEST_SIZE - total);
        if (nbytes == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("read");
            return -1;
        }
        if (nbytes == 0) {
            // client closed before sending a complete request
            return total == 0 ? -1 : 400;
        }
        total += nbytes;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n") != NULL) {
            break;
        }
    }

    // grab operation, resource, and version from the request line
    char operation[16], resource[BUFSIZE], version[16];
    if (sscanf(buf, "%15s %511s %15s", operation, resource, version) != 3) {
        return 400;
    }
    if (strncmp(version, "HTTP/1.", 7) != 0 || resource[0] != '/') {
        return 400;
    }
    if (strlen(resource) == sizeof(resource) - 1) {
        // resource may have been truncated
        return 400;
    }
    if (strcmp(operation, "GET") != 0) {
        return 405;
    }

    int n = snprintf(resource_name, resource_size, "%s", resource);
    if (n < 0 || (size_t) n >= resource_size) {
        return 400;
    }
    return 0;
}

int write_http_error(int fd, int status) {
    const char *text = get_status_text(status);
    char body[64];
    int body_len = snprintf(body, sizeof(body), "%d %s\n", status, text);

    char buf[BUFSIZE];
    int len = snprintf(buf, sizeof(buf),
        "HTTP/1.0 %d %s\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n%s\r\n%s",
        status, text, body_len, status == 405 ? "Allow: GET\r\n" : "", body);
    return write_all(fd, buf, len);
}

int write_http_response(int fd, const char *resource_path) {
    // open file
    int file_fd = open(resource_path, O_RDONLY);
    if (file_fd == -1) {
        if (errno == ENOENT || errno == ENOTDIR) {
            return write_http_error(fd, 404);
        }
        if (errno == EACCES) {
            return write_http_error(fd, 403);
        }
        perror("open");
        return write_http_error(fd, 500);
    }

    // get resource info
    struct stat statbuf;
    if (fstat(file_fd, &statbuf) == -1) {
        perror("fstat");
        close(file_fd);
        return write_http_error(fd, 500);
    }
    if (!S_ISREG(statbuf.st_mode)) {
        close(file_fd);
        return write_http_error(fd, 404);
    }

    // build http response
    char buf[BUFSIZE];
    int len = snprintf(buf, sizeof(buf),
        "HTTP/1.0 200 OK\r\nContent-Type: %s\r\nContent-Length: %lld\r\n\r\n",
        get_mime_type(resource_path), (long long) statbuf.st_size);
    if (write_all(fd, buf, len) == -1) {
        close(file_fd);
        return -1;
    }

    // write file to socket
    ssize_t nbytes;
    while ((nbytes = read(file_fd, buf, sizeof(buf))) != 0) {
        if (nbytes == -1) {
            if (errno == EINTR) {
                continue;
            }
            // headers are already sent, so all we can do is drop the connection
            perror("read");
            close(file_fd);
            return -1;
        }
        if (write_all(fd, buf, nbytes) == -1) {
            close(file_fd);
            return -1;
        }
    }
    close(file_fd);
    return 0;
}
