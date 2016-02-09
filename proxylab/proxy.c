/*
 * proxy.c
 * this is a simple proxy application.
 *
 * proxy is an intermediate point between browser and the server.
 * When the proxy.c is run, it will listen on the input port and
 * wait for request from the browser. Then the proxy program will
 * recieve request from browser again and again. The proxy will
 * process the request, extract server host from the request and
 * send processed request to server, wait for response and redirect
 * it to the browser.
 * this implementation is a multi-thread proxy with cache feature.
 * it can handle multiple request simultaneosly using posix threads.
 * I use reader prior read-write lock to deal with race conditions.
 * it can cahe web object in local memory.
 * 
 * Andrew ID : zlyu
 * Name: Zekun Lyu
 *
 */
#include <stdio.h>
#include "csapp.h"
#include "cache.h"
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif

/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";

/* helper functions */
void *doit(void *fd);
int handle_request(int client_fd);
void send_request(int client_fd, char *port, char *host_name, char *path, 
	char *host_header, char *other_header, char *uri);
void parse_uri(char* uri, char* host_name , char* path, char *port);
void read_headers(rio_t *rp, char *host_header, char *other_header);
int read_cache(cache *c, char* uri, int client_fd);
void write_cache(cache* c, char *uri, char* content, int size);


/* Global variable */
cache *c;
sem_t read_lock;              /* a lock for read count*/
sem_t write_lock;             /* a lock for writer to access cache */
int read_count;


/* lock helper routine */
/* grab the read lock */
static void grab_read_lock(){
	P(&read_lock);
	read_count++;
	if(read_count==1)
		P(&write_lock);
	V(&read_lock);
}

/* release read lock */
static void realease_read_lock(){
	P(&read_lock);
	read_count--;
	if(read_count==0)
		V(&write_lock);
	V(&read_lock);
}

/*
 * main routine of proxy. open listenfd and
 * accept request repeatedly.
 * argument: take in a port number as argument
 */
int main(int argc, char **argv){
	int listenfd, *connfdp;
	socklen_t clientlen;
    struct sockaddr_in clientaddr;
    pthread_t tid;	

    signal(SIGPIPE, SIG_IGN);

    /* lock and cache setup */
	c = cache_init();
	Sem_init(&read_lock,0,1);
	Sem_init(&write_lock,0,1);
	read_count = 0;

    printf("%s%s%s", user_agent_hdr, accept_hdr, accept_encoding_hdr);

    /* receive request from browsers, act as a server */
    if(argc != 2){
    	fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
    }
    // port = atoi(argv[1]);

    listenfd = Open_listenfd(argv[1]);
    if(listenfd<0){
    	fprintf(stderr, "can't listen to port\n");
    }
    while (1) {
		clientlen = sizeof(clientaddr);
		connfdp = Malloc(sizeof(int));
		*connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		// doit(connfdp);
		Pthread_create(&tid, NULL, doit, connfdp);
    }
    clear(c);
    return 0;
}

/*
 * doit - handle one HTTP request/response transaction
 * detach the thread and receive request from browser
 */
void *doit(void *fd){
	int client_fd = *(int*)fd;
	Pthread_detach(pthread_self());
	Free(fd); /* free fd to avoid memory leak */
	handle_request(client_fd);
	Close(client_fd);
	return NULL;
}


/*
 * handle_request - read request line and headers, extract
 * host name, path and port number from request line. read, store
 * and modify request headers. finally call send_request to send 
 * the request to server.
 */
int handle_request(int client_fd){
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	char request_line[MAXLINE], host_name[MAXLINE], path[MAXLINE], 
			host_header[MAXLINE], other_header[MAXLINE];
	rio_t rio;
	char server_port[MAXLINE];

	rio_readinitb(&rio, client_fd);
	if(rio_readlineb(&rio, buf, MAXLINE)<=0){
		return -1;
	}
	strcpy(request_line, buf);
	dbg_printf("%s\n", request_line);
	/* check invalid url */
	if(strstr(request_line, "/")==NULL||strlen(request_line)<1){
		printf("invalid request line\n");
		return -1;
	}

	sscanf(request_line, "%s %s %s", method, uri, version);
	if(strcasecmp(method, "GET")){
		printf("proxy doesn't support this method: %s\n", method);
		return -1;
	}

	parse_uri(uri, host_name, path, server_port);

	if(read_cache(c, uri, client_fd))
		return 0;

	strcpy(host_header, "Host: ");
	strcat(host_header, host_name);

	dbg_printf("finish parse_uri\n");
	read_headers(&rio, host_header, other_header);

	dbg_printf("finish read_headers\n");

	send_request(client_fd, server_port, host_name, 
					path, host_header, other_header, uri);

	return 0;
}

/* cache routine */

/*
 * read_cache - check if the requested web object is 
 * in the cache. if true, write the response data to
 * the browser and return. if not, return 1. This method
 * will not modify the cache, grab the read lock when
 * doing this operation.
 */
int read_cache(cache *c, char* uri, int client_fd){
	/* grab read lock*/
	grab_read_lock();
	block* node;	
	if((node=find_cache(c,uri))==NULL){
		realease_read_lock();	
		return 0;
	}

	if(rio_writen(client_fd, node->content, node->size)<0){
		realease_read_lock();
		return 0;
	}

	/* realease read lock */
	realease_read_lock();
	return 1;
}



/*
 * write_cache - write the web object into cache.
 * grab write_lock before wrtie to the cache
 */
void write_cache(cache* c, char *uri, char* content, int size){
	P(&write_lock);
	cache_insert(c, uri, content, size);
	V(&write_lock);
}

/*
 * send_request - send the request to the server. Given the host_name
 * and host_port, try to connect to the server. form request content
 * with request line and request header and send it to the server.
 * Then read response data from server, write data to browser and cache
 * the data if it's small.
 */
void send_request(int client_fd, char *port, char *host_name, char *path, 
	char *host_header, char *other_header, char *uri){
	dbg_printf("1\n");
	rio_t rio;
	int server_fd;

	dbg_printf("%s\n", host_name);
	dbg_printf("before open clientfd\n");
	server_fd = open_clientfd(host_name, port);
	dbg_printf("after open clientfd\n");
	if(server_fd<0){
		fprintf(stderr, "can't connect to host\n");
		// exit(1);
	}
	char buf[MAXLINE];

	sprintf(buf, "GET %s HTTP/1.0\r\n", path);
	sprintf(buf, "%s%s\r\n", buf, host_header);
	sprintf(buf, "%s%s", buf, user_agent_hdr);
	sprintf(buf, "%s%s", buf, accept_hdr);
	sprintf(buf, "%s%s", buf, accept_encoding_hdr);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sProxy-Connection: close\r\n", buf);
    sprintf(buf, "%s%s\r\n", buf, other_header);

    dbg_printf("request:\n%s\n", buf);

    /* send request to server */
    if(rio_writen(server_fd, buf, strlen(buf))<0){
    	return;
    }

    /* recieve response from server */
    char cache_buf[MAX_OBJECT_SIZE]="";
    int n,size;
    char* ptr;
    ptr = cache_buf;
    size = 0;


    Rio_readinitb(&rio, server_fd);
    while((n=Rio_readnb(&rio, buf, MAXBUF))>0){
    	size+=n;
    	if(size<MAX_OBJECT_SIZE){
    		printf("%s\n", buf);
    		memcpy(ptr, buf, n);
    		ptr+=n;
    	}
    	
    	if(rio_writen(client_fd, buf, n)<0){
	    	if(errno==EPIPE)
	    		return;
    	}
    }
    dbg_printf("cache buf: \n%s\n", (char*)cache_buf);
    if(n<0){
    	return;
    }
    if(size<MAX_OBJECT_SIZE)
    	write_cache(c, uri, cache_buf, size);
    // Close(client_fd);
}

/*
 * parse_uri - parse uri into three parts: host_name, path,
 * and port
 */
void parse_uri(char* uri, char* host_name ,char* path, char* port){
	char protocol[MAXLINE];
	char *index;
	if(strstr(uri, "://")==NULL){
		sscanf(uri, "%[^/]%s", host_name, path);
	}else{
		sscanf(uri, "%[^:]://%[^/]%s", protocol, host_name, path);
	}

	strcpy(port, "80");
	if((index=strchr(host_name,':'))!=NULL){
		index[0] = '\0';
		strcpy(port, index+1);
	}
	printf("%s\n%s\n", host_name, path);

}


/*
 * read_headers - read request headers from browser
 */
void read_headers(rio_t *rp, char *host_header, char *other_header){
	char buf[MAXLINE];
	dbg_printf("starting read headers\n");
	if(rio_readlineb(rp, buf, MAXLINE)<0){
		dbg_printf("fail to read headers from client\n");
		if(errno!=ECONNRESET)
			exit(0);
		return;
	}
	while(strcmp(buf, "\r\n")&&strcmp(buf,"\n")){
		if(!strncmp(buf,"Host: ", strlen("Host: ")))
			strcpy(host_header, buf);     /* override host header */
		if(strncmp(buf, "User-Agent:", strlen("User-Agent:"))&&
			strncmp(buf, "Accept:", strlen("Accept:"))&&
			strncmp(buf, "Accept-Encoding:", strlen("Accept-Encoding:"))&&
			strncmp(buf, "Connection:", strlen("Connection:"))&&
			strncmp(buf, "Proxy-Connection:", strlen("Proxy-Connection:"))&&
			strncmp(buf, "GET", strlen("GET"))
			){/* if the header is not specified in writeup */
			sprintf(other_header, "%s%s", other_header, buf);
		}
		if(rio_readlineb(rp, buf, MAXLINE)<0){
			if(errno!=ECONNRESET)
				exit(0);
		}
	}
}