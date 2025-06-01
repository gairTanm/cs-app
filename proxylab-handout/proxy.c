/*
 * proxy.c - A simple proxy
 *
 */

// TODO: get it working using csapp.c, then implement your own helpers to
// understand it better

#include <stdio.h>
#include <string.h>

/*#include <cstdio>*/

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAXHEADERS 20

char *read_requesthdrs(rio_t *rp);
void routeit(int connfd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
int parse_url(const char *url, char *hostname, char *port, char *path);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux arm64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3";

int main(int argc, char **argv) {
    int listenfd, connfd;
    struct sockaddr_storage clientaddr;
    socklen_t clientlen;
    char hostname[MAXLINE], port[MAXLINE];

    printf("%s", user_agent_hdr);
    if (argc < 2) {
        printf("Usage: $%s <port>\n", argv[0]);
        exit(1);
    }

    if ((listenfd = Open_listenfd(argv[1])) == -1) {
        printf("Unable to open a port");
        exit(1);
    }

    while (1) {
        clientlen = sizeof(clientaddr);

        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port,
                    MAXLINE, 0);
        printf("Routing connection for (%s, %s)\n", hostname, port);
        routeit(connfd);
        Close(connfd);
    }

    return 0;
}

void routeit(int connfd) {
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    char uri_with_port[MAXLINE];
    char *hosthdr;
    int bytes_read, bytes_sent;
    rio_t rio, rio_target;
    char hostname[256], port[6], path[256];

    /* Read request line and headers */
    Rio_readinitb(&rio, connfd);
    if (!Rio_readlineb(&rio, buf, MAXLINE)) return;
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) {
        clienterror(connfd, method, "501", "Not Implemented",
                    "proxy does not implement this method (yet)");
        return;
    }
    hosthdr = read_requesthdrs(&rio);

    /*modify_request();*/
    parse_url(uri, hostname, port, path);

    printf("hostname: %s uri: %s port: %s path: %s\n", hostname, uri, port,
           path);
    int clientfd = Open_clientfd(hostname, port);
    printf("Forwarding request to %s\n", hostname);
    Rio_readinitb(&rio_target, clientfd);
    char targetbuf[MAXLINE];

    sprintf(targetbuf, "%s %s HTTP/1.0\r\n", method, path);
    Rio_writen(clientfd, targetbuf, strlen(targetbuf));
    sprintf(targetbuf, "Host: %s:%s\r\n", hostname, port);
    Rio_writen(clientfd, targetbuf, strlen(targetbuf));
    sprintf(targetbuf, "%s\r\n", user_agent_hdr);
    Rio_writen(clientfd, targetbuf, strlen(targetbuf));
    sprintf(targetbuf, "Connection: close\r\n");
    Rio_writen(clientfd, targetbuf, strlen(targetbuf));
    sprintf(targetbuf, "Proxy-Connection: close\r\n\r\n");
    Rio_writen(clientfd, targetbuf, strlen(targetbuf));

    printf("Wrote request to %s %d, sending response\n", hostname, strlen(buf));
    while ((bytes_read = Rio_readlineb(&rio_target, buf, MAXLINE)) != 0) {
        if (bytes_read <= 0) break;
        Rio_writen(connfd, buf, bytes_read);
    }
    printf("Closing connection");
    Close(clientfd);
}

int parse_url(const char *url, char *hostname, char *port, char *path) {
    char *start = strstr(url, "://");
    start = start ? start + 3 : (char *)url;

    char *host_end = strchr(start, ':');
    if (!host_end) host_end = strchr(start, '/');
    if (!host_end) host_end = start + strlen(start);
    strncpy(hostname, start, host_end - start);
    hostname[host_end - start] = '\0';

    if (*host_end == ':') {
        start = host_end + 1;
        char *port_end = strchr(start, '/');
        if (!port_end) port_end = start + strlen(start);
        strncpy(port, start, port_end - start);
        port[port_end - start] = '\0';
    } else {
        strcpy(port, "80");
    }

    char *path_start = strchr(host_end, '/');
    if (path_start)
        strcpy(path, path_start);
    else
        strcpy(path, "/");

    return 0;
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg) {
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf,
            "<body bgcolor="
            "ffffff"
            ">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}

char *read_requesthdrs(rio_t *rp) {
    char buf[MAXLINE];
    char *hosthdr;
    /*char *headers[MAXHEADERS];*/
    int i = 0;

    printf("Reading headers:\n");
    Rio_readlineb(rp, buf, MAXLINE);
    /*printf("%s %u", buf, strlen(buf));*/
    hosthdr = malloc(strlen(buf) + 1);
    strcpy(hosthdr, buf);
    /*while (strcmp(buf, "\r\n")) {  // line:netp:readhdrs:checkterm*/
    /*    Rio_readlineb(rp, buf, MAXLINE);*/
    /*    printf("%s", buf);*/
    /*}*/
    return hosthdr;
}
