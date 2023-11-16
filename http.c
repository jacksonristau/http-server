#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include "http.h"

#define BUFSIZE 512

const char *get_mime_type(const char *file_extension) {
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

    return NULL;
}

int read_http_request(int fd, char *resource_name) {
    char buf[BUFSIZE];
    int nbytes;

    // read request
    while ((nbytes = read(fd, buf, BUFSIZE)) > 0){
        // grab operation, file, and version
        char operation[16], resource[256], version[16];
        
        sscanf(buf, "%s %s %s", operation, resource, version);
        version[6] = '\0';

        // make sure the request is valid
        if (strcmp(operation, "GET") != 0 || strcmp(version, "HTTP/1") != 0){
            return -1;
        }
        else{
            // copy file name
            strcpy(resource_name, resource);
            return 0;
        }
    }
    if (nbytes == -1){
        perror("read");
        return -1;
    }
    // consume the rest of the request
    while (read(fd, buf, BUFSIZE) > 0){

    }

    return 0;
}

int write_http_response(int fd, const char *resource_path) {
    struct stat statbuf;

    // get resource info
    if (stat(resource_path, &statbuf) == -1){
        if (errno == ENOENT){
            // file not found
            char *buf = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            int res = write(fd, buf, BUFSIZE);
            if (res == -1){
                perror("write");
                return -1;
            }
            return 0;
        }
        perror("stat");
        return -1;
    }

    // open file
    int file_fd;
    if ((file_fd = open(resource_path, O_RDONLY)) == -1){
        perror("open");
        return -1;
    }

    // build http response
    char buf[BUFSIZE];
    sprintf(buf, "HTTP/1.0 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n",
     get_mime_type(resource_path), statbuf.st_size);
    int nbytes = write(fd, buf, strlen(buf));

    // write file to socket
    while ((nbytes = read(file_fd, buf, BUFSIZE)) > 0) {
        if ((write(fd, buf, nbytes)) < 0){
            perror("write");
            close(file_fd);
            return 1;
        }
          
    }
    close(file_fd);
    return 0;
}
