/*
An server translates dns to http using libevent2

Author: skyvense@gmail.com
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include "getopt.h"
#ifndef S_ISDIR
#define S_ISDIR(x) (((x) & S_IFMT) == S_IFDIR)

/*
* Tail queue functions.
*/
#define	TAILQ_INIT(head) do {						\
	(head)->tqh_first = NULL;					\
	(head)->tqh_last = &(head)->tqh_first;				\
} while (0)

#endif
#else
#include <sys/stat.h>
#include <sys/socket.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <getopt.h>

#include <sys/queue.h>
#endif

#include <event2/event.h>
#include <event2/http.h>
#include <event2/dns.h>
#include <event2/dns_struct.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/keyvalq_struct.h>


#ifdef _EVENT_HAVE_NETINET_IN_H
#include <netinet/in.h>
# ifdef _XOPEN_SOURCE_EXTENDED
#  include <arpa/inet.h>
# endif
#endif

#ifdef WIN32
#pragma comment(lib, "libevent.lib")
#pragma comment(lib, "libevent_core.lib")
#pragma comment(lib, "libevent_extras.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

#define MYHTTPD_SIGNATURE   "HttpDns 0.1"
#define MAX_DOMAIN_NAME		128
#define u32 ev_uint32_t
#define u8 ev_uint8_t

struct event_base *base = NULL;
struct evdns_base *evdns_base = NULL;
static int verbose = 0;
const char* listen_addr = "0.0.0.0";
const char *resolv_conf = NULL;
unsigned short listen_port = 9999;

struct http_dns_query_request
{
	struct evhttp_request *req;
	char domain_name[MAX_DOMAIN_NAME];
};

static const char *
debug_ntoa(u32 address)
{
	static char buf[32];
	u32 a = ntohl(address);
	evutil_snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
		(int)(u8)((a >> 24) & 0xff),
		(int)(u8)((a >> 16) & 0xff),
		(int)(u8)((a >> 8) & 0xff),
		(int)(u8)((a)& 0xff));
	return buf;
}

static void
main_callback(int result, char type, int count, int ttl,
void *addrs, void *orig) 
{
	http_dns_query_request *dns_request = (http_dns_query_request*)orig;
	struct evbuffer *buf = NULL;
	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(dns_request->req);
	//HTTP header
	evhttp_add_header(output_headers, "Server", MYHTTPD_SIGNATURE);
	evhttp_add_header(output_headers, "Content-Type", "text/plain; charset=UTF-8");
	evhttp_add_header(output_headers, "Connection", "close");


	if (!count)
	{
		if (verbose) fprintf(stderr, "dns query %s failed, reason: %s\n", dns_request->domain_name, evdns_err_to_string(result));
		evhttp_send_reply(dns_request->req, HTTP_OK, "OK", NULL);
		delete dns_request;
		return;
	}

	buf = evbuffer_new();

	int i;
	for (i = 0; i < count; i++) 
	{
		if (type == DNS_IPv4_A) 
		{
			//printf("%s: %s\n", n, debug_ntoa(((u32*)addrs)[i]));
			evbuffer_add_printf(buf, "%s", debug_ntoa(((u32*)addrs)[i]));
			if (i != count - 1) evbuffer_add_printf(buf, ";");
		}
		else if (type == DNS_PTR) 
		{
			//printf("%s: %s\n", n, ((char**)addrs)[i]);
			evbuffer_add_printf(buf, "%s;", ((char**)addrs)[i]);
		}
	}
	
	evhttp_send_reply(dns_request->req, HTTP_OK, "OK", buf);
	delete dns_request;
	evbuffer_free(buf);
}

/* Callback used for the /dump URI, and for every non-GET request:
* dumps all information to stdout and gives back a trivial 200 ok */
static void
query_request_cb(struct evhttp_request *req, void *arg)
{
	struct evkeyvalq query;
	TAILQ_INIT(&query);
	if (evhttp_parse_query(evhttp_request_get_uri(req), &query) != 0)
	{
		evhttp_send_reply(req, 400, "BAD REQUEST", NULL);
		evhttp_clear_headers(&query);
		return;
	}

	const char* domain_name = evhttp_find_header(&query, "dn");
	if (verbose) printf("http got query: %s\n", domain_name);
	if (strlen(domain_name) > MAX_DOMAIN_NAME)
	{
		evhttp_send_reply(req, 400, "name too long", NULL);
		evhttp_clear_headers(&query);
		return;
	}
	http_dns_query_request *dns_request = new http_dns_query_request;
	dns_request->req = req;
	strcpy(dns_request->domain_name, domain_name);

	evdns_base_resolve_ipv4(evdns_base, dns_request->domain_name, DNS_OPTIONS_ALL, main_callback, dns_request);
	evhttp_clear_headers(&query);
	
	
	//evhttp_send_reply(req, 200, "OK", NULL);
}

static void
gen_request_cb(struct evhttp_request *req, void *arg)
{
	struct evkeyvalq *output_headers = evhttp_request_get_output_headers(req);
	//HTTP header
	evhttp_add_header(output_headers, "Server", MYHTTPD_SIGNATURE);
	evhttp_add_header(output_headers, "Content-Type", "text/plain; charset=UTF-8");
	evhttp_add_header(output_headers, "Connection", "close");
	//输出的内容
	struct evbuffer *buf;
	buf = evbuffer_new();
	evbuffer_add_printf(buf, "\n%s\n", MYHTTPD_SIGNATURE);
	evbuffer_add_printf(buf, "\nExample:\n");
	if (listen_port == 80)
		evbuffer_add_printf(buf, "http://%s/d?dn=www.google.com\n", listen_addr);
	else 
		evbuffer_add_printf(buf, "http://%s:%d/d?dn=www.google.com\n", listen_addr, listen_port);
	evbuffer_add_printf(buf, "\nHave fun.\n");
	evbuffer_add_printf(buf, "\n\nAuthor: skyvense@gmail.com.\n");
	evhttp_send_reply(req, HTTP_OK, "OK", buf);
	evbuffer_free(buf);
}



static void
logfn(int is_warn, const char *msg) {
	if (!is_warn && !verbose)
		return;
#ifdef WIN32
	fprintf(stderr, "%d ", GetTickCount());
#endif
	fprintf(stderr, "%s: %s\n", is_warn ? "WARN" : "INFO", msg);
}


#ifndef WIN32
//当向进程发出SIGTERM/SIGHUP/SIGINT/SIGQUIT的时候，终止event的事件侦听循环
void signal_handler(int sig) {
	switch (sig) {
	case SIGTERM:
	case SIGHUP:
	case SIGQUIT:
	case SIGINT:
		event_base_loopexit(base, NULL);  //终止侦听event_dispatch()的事件侦听循环，执行之后的代码
		break;
	}
}
#endif

static void print_help()
{
	printf("%s\n\n", MYHTTPD_SIGNATURE);
	printf("Command line arguments with either long or short options:\n");
	printf("\t-h, --help\t show this help.\n");
	printf("\t-v, --verbose\t print log messages\n");
#ifndef WIN32
	printf("\t-d, --deamon\t put program to daemon mode.\n");
#endif
	printf("\t-r, --resolv\t use specified resolv file\n");
	printf("\t-l, --listen=<ip>\t specify your local ip address listen at, default 0.0.0.0\n");
	printf("\t-p, --port=<ip>\t specify your port listen at, default 9999\n");
}

int
main(int argc, char **argv)
{
	struct evhttp *http;
	
	struct evhttp_bound_socket *handle;
	bool bDeamon = false;
	
#ifdef WIN32
	WSADATA WSAData;
	WSAStartup(0x101, &WSAData);
#else
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		return (1);
	//自定义信号处理函数
	signal(SIGHUP, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGQUIT, signal_handler);
#endif

	static option longopts[] =
	{
		{ "help", no_argument, NULL, 'h' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "listen", required_argument, NULL, 'l' },
		{ "resolve", required_argument, NULL, 'r' },
		{ "port", required_argument, NULL, 'p' },
#ifndef WIN32
		{ "deamon", no_argument, NULL, 'd' },
#endif
		{ NULL, 0, NULL, '\0' },
	};

	signed char c;
	while ((int)(c = getopt_long(argc, argv, "r:p:hvdl:", longopts, NULL)) != -1)
	{
		//std::cout << "get opt " << (int)c << std::endl;
		switch (c)
		{
		case 'h':
			print_help();
			exit(0);
			break;
		case 'v':
			verbose = 1;
			break;
		case 'l':
			listen_addr = optarg;
			break;
		case 'r':
			resolv_conf = optarg;
			break;
		case 'p':
			listen_port = atoi(optarg);
			break;
			
#ifndef WIN32
		case 'd':
			bDeamon = true;
			daemon(0,0);
			break;
#endif
		}
	}

	base = event_base_new();
	if (!base) {
		fprintf(stderr, "Couldn't create an event_base: exiting\n");
		return 1;
	}
	evdns_base = evdns_base_new(base, 0);
	if (!evdns_base) {
		fprintf(stderr, "Couldn't create an evdns_base: exiting\n");
		return 1;
	}
	evdns_set_log_fn(logfn);
	event_set_log_callback(logfn);


	/* Create a new evhttp object to handle requests. */
	http = evhttp_new(base);
	if (!http) {
		fprintf(stderr, "couldn't create evhttp. Exiting.\n");
		return 1;
	}

	/* The /dump URI will dump all requests to stdout and say 200 ok. */
	evhttp_set_cb(http, "/d", query_request_cb, NULL);
	evhttp_set_gencb(http, gen_request_cb, NULL);

	/* Now we tell the evhttp what port to listen on */
	handle = evhttp_bind_socket_with_handle(http, listen_addr, listen_port);
	if (!handle) {
		fprintf(stderr, "couldn't bind to port %s:%d. Exiting.\n",
			listen_addr, (int)listen_port);
		return 1;
	}

	{
		int res;
#ifdef WIN32
		if (resolv_conf == NULL)
			res = evdns_base_config_windows_nameservers(evdns_base);
		else
#endif
			res = evdns_base_resolv_conf_parse(evdns_base,
			DNS_OPTION_NAMESERVERS,
			resolv_conf ? resolv_conf : "/etc/resolv.conf");

		if (res < 0) {
			fprintf(stderr, "Couldn't configure nameservers");
			return 1;
		}
	}

	evdns_base_set_option(evdns_base, "timeout:", "0.3");
	evdns_base_set_option(evdns_base, "max-timeouts:", "2");
	evdns_base_set_option(evdns_base, "attempts:", "5");
	evdns_base_set_option(evdns_base, "randomize-case:", "0");
	event_base_dispatch(base);

	return 0;
}
