#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUF_SIZE 4096

char *serve_dir;

void reap_children(int sig) {
    (void) sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

const char *get_mime_type(const char *filepath) {
    const char *ext = strrchr(filepath, '.');
    if (!ext) return "application/octet-stream";

    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".txt") == 0) return "text/plain";

    return "application/octet-stream";
}

void send_response(int client_fd, int status_code, const char *status_text,
                    const char *content_type, const char *body, size_t body_len) {
    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_code, status_text, content_type, body_len);

    write(client_fd, header, header_len);
    write(client_fd, body, body_len);
}

void send_404(int client_fd) {
    const char *body = "<html><body><h1>404 Not Found</h1></body></html>";
    send_response(client_fd, 404, "Not Found", "text/html", body, strlen(body));
}

void send_405(int client_fd) {
    const char *body = "<html><body><h1>405 Method Not Allowed</h1></body></html>";
    send_response(client_fd, 405, "Method Not Allowed", "text/html", body, strlen(body));
}

void handle_request(int client_fd) {
    char buf[BUF_SIZE];
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        close(client_fd);
        exit(0);
    }
    buf[n] = '\0';

    char method[8], url[1024], proto[16];
    if (sscanf(buf, "%7s %1023s %15s", method, url, proto) != 3) {
        send_404(client_fd);
        close(client_fd);
        exit(0);
    }

    if (strcmp(method, "GET") != 0) {
        send_405(client_fd);
        close(client_fd);
        exit(0);
    }

    if (strstr(url, "..") != NULL) {
        send_404(client_fd);
        close(client_fd);
        exit(0);
    }

    char filepath[2048];
    if (strcmp(url, "/") == 0) {
        snprintf(filepath, sizeof(filepath), "%s/index.html", serve_dir);
    } else {
        snprintf(filepath, sizeof(filepath), "%s%s", serve_dir, url);
        size_t len = strlen(filepath);
        if (len > 0 && filepath[len - 1] == '/') {
            strncat(filepath, "index.html", sizeof(filepath) - len - 1);
        }
    }

    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        send_404(client_fd);
        close(client_fd);
        exit(0);
    }

    struct stat st;
    fstat(fd, &st);
    size_t file_size = st.st_size;

    char *file_buf = malloc(file_size);
    read(fd, file_buf, file_size);
    close(fd);

    send_response(client_fd, 200, "OK", get_mime_type(filepath), file_buf, file_size);

    free(file_buf);
    close(client_fd);
    exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <port> <directory>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    serve_dir = argv[2];

    signal(SIGCHLD, reap_children);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    bind(server_fd, (struct sockaddr *) &addr, sizeof(addr));
    listen(server_fd, 10);

    printf("Server listening on port %d, serving: %s\n", port, serve_dir);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;

        pid_t pid = fork();
        if (pid == 0) {
            close(server_fd);
            handle_request(client_fd);
        } else {
            close(client_fd);
        }
    }

    return 0;
}
