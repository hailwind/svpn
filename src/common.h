#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>

#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <pthread.h>
#include <mcrypt.h>
#include <lz4.h>
#include <getopt.h>

#include <sys/time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/syscall.h>

#include <linux/if.h>
#include <linux/if_tun.h>
#include <arpa/inet.h>

#include "lib/ikcp.h"
#include "lib/queue.h"
#include "lib/hashtable.h"
#include "lib/rbtree.h"

#ifndef _SVPN_COMMON_
#define ENABLED_LOG {"notice", "warning", "error", "init_tap", "init_mcrypt"}
#define PID_PATH "/var/run/svpn_%s_%s_%d.pid"
#define FIFO_PATH "/var/run/svpn_%s_%d.fifo"
#define DEFAULT_SERVER_PORT 8888

#define SERVER 1
#define CLIENT 2

#endif

struct _kcpsess_st
{
    int dead;

    ikcpcb *kcp;
    int conv;
    int sess_id;

    char key[64];

    int dev_fd;
    int sock_fd;
    struct sockaddr_in dst;
    socklen_t dst_len;

    pthread_t writedevt;
    pthread_t readdevt;
    pthread_t writeudpt;
    pthread_mutex_t ikcp_mutex;

    sigset_t writeudp_sigset;
    sigset_t writedev_sigset;

    uint64_t last_alive_time;
    uint32_t latest_send_iclock;
};
typedef struct _kcpsess_st kcpsess_t;

struct _server_listen_st
{
	int sock_fd;
    pthread_t readudpt;
	hashtable_t *conn_map;
};
typedef struct _server_listen_st server_listen_t;

void init_ulimit();

void init_global_config(int role, int mode, int lz4, int debug, int crypt, char *crypt_algo, char *crypt_mode);

void init_server_config(char *server_addr, int server_port, ...);

void start_thread(pthread_t *tid, char *name, void *func, void *param);

void stop_thread(pthread_t tid);

void logging(char *name, char *message, ...);

void set_debug(int enabled);

void usr_signal(int signo);

void exit_signal(int signo);

int init_tap(int conv);

kcpsess_t *init_kcpsess(int conv, int dev_fd, char *key, int sock_fd);

void *alive(void *data);

void *readudp_client(void *data);

void *readudp_server(void *data);

void *writedev(void *data);

void *readdev(void *data);

void *writeudp(void *data);
