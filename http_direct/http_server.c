#include <stdio.h>
#include <string.h>
#include <unistd.h>       // close(), read(), write()
#include <sys/socket.h>   // socket(), bind(), listen(), accept()
#include <netinet/in.h>   // struct sockaddr_in, INADDR_ANY
#include <arpa/inet.h>
#include <stdlib.h>

#include "dashboard.h"

#define PORT 9090
#define BUFFER_SIZE 4096

//temporary file for direct html file reading
char *load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);          // jump to end
    long size = ftell(f);           // how many bytes?
    rewind(f);                      // jump back to start
    char *buf = malloc(size + 1);   // allocate exact size
    fread(buf, 1, size, f);         // read whole file
    buf[size] = '\0';               // null-terminate
    fclose(f);
    return buf;                     // caller must free()
}

void send_response(int client_fd, int status_code, const char *status_text,
                   const char *content_type, const char *body) {
    int body_len = strlen(body);
    // snprintf builds the header string safely into a buffer
    char headers[512];
    snprintf(headers, sizeof(headers), //formatted output to a sized character buffer
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"   // tell browser not to wait for more
        "\r\n",                   // mandatory blank line
        status_code, status_text,
        content_type,
        body_len);

    write(client_fd, headers, strlen(headers));
    write(client_fd, body, body_len);
}

void handle_api_status(int client_fd) {

    char json[512];
    snprintf(json, sizeof(json),
        "{"
        "\"cpu_temp\": %d,"
        "\"memory_used_mb\": %d,"
        "\"memory_total_mb\": %d,"
        "\"disk_used_gb\": %d,"
        "\"disk_total_gb\": %d,"
        "\"uptime_seconds\": %d,"
        "\"load_avg_1m\": %.2f"
        "}",
        55,       // cpu_temp
        3200,     // memory_used_mb
        8192,     // memory_total_mb
        45,       // disk_used_gb
        238,      // disk_total_gb
        86400,    // uptime_seconds (1 day)
        0.72      // load_avg_1m
    );
    send_response(client_fd, 200, "OK", "application/json", json);
}

int main(int argc, char *argv[]) {
    //    AF_INET  = IPv4 family
    //    SOCK_STREAM = TCP
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    // Allow reusing the port immediately after restart (avoids "Address already in use")
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,   // 0.0.0.0 — any interface
        .sin_port        = htons(PORT)   // htons = host-to-network byte order
    };

    //check for host argument
    for (int i=1;i<argc;i++) {
        if (strcmp(argv[i], "--host") == 0) {

            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --host requires an IP address argument.\n");
                return EXIT_FAILURE;
            }

            const int status = inet_pton(AF_INET, argv[++i], &address.sin_addr); //parse address string as bytes for declared interface

            if (status == 0) {
                fprintf(stderr, "Invalid address: '%s'\n", argv[i]);
                return EXIT_FAILURE;
            }
            else if (status < 0) {
                perror("Invalid address family");
                return EXIT_FAILURE;
            }
        }
    }

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));

    listen(server_fd, 10);
    printf("Listening on port %d...\n", PORT);
    printf("Ip: %s\n",inet_ntoa(address.sin_addr));

    while (1) {
        //Block here until a client connects — returns a new fd for the client
        int client_fd = accept(server_fd, NULL, NULL);

        // Read
        char buffer[BUFFER_SIZE] = {0};
        read(client_fd, buffer, BUFFER_SIZE - 1);

        char method[8], path[256], version[16] = {0};
        sscanf(buffer, "%s %s %s", method, path, version);
        printf("Method: %s    Path: %s\n", method, path);
        printf("--- Received ---\n%s\n", buffer);

        // Route
        if (strcmp(path, "/") == 0) {
            char *html = load_file("dashboard.html");
            if (!html) {
                send_response(client_fd, 500, "Internal Server Error", "text/plain", "dashboard.html not found");
            }
            send_response(client_fd, 200, "OK", "text/html",html);
            free(html);
        } else if (strcmp(path, "/favicon.ico") == 0) {
            // Browser always asks for this — just tell it there isn't one
            send_response(client_fd, 404, "Not Found", "text/plain", "");
        }else if (strcmp(path, "/api/status") == 0) {
            handle_api_status(client_fd);
        }else {
            send_response(client_fd, 404, "Not Found", "text/plain", "404 - Not Found");
        }

        // Close
        close(client_fd);
    }
}