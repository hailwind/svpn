#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <getopt.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <netdb.h>
#include <pthread.h>
#include <mcrypt.h>
#include <lz4.h>

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
#include "lib/rqueue.h"
#include "lib/hashtable.h"
#include "lib/rbtree.h"

#ifndef _SVPN_COMMON_
#define ENABLED_LOG {"notice", "warning", "error", "init_tap", "init_mcrypt"}
#define DEFAULT_SERVER_PORT 8888

#define SND_WINDOW 16384
#define RSV_WINDOW 16384

#define MTU 1400
#define RCV_BUFF_LEN 16384
#define RX_MINRTO 20

//IKCP PARAMETERS DEFINE
//int nodelay, int interval, int resend, int nc
#define M0_MODE 0, 1, 20, 0
#define M1_MODE 0, 2, 50, 0
#define M2_MODE 0, 2, 20, 0
#define M3_MODE 0, 2, 10, 0
#define M4_MODE 0, 2, 5, 0
#define M5_MODE 0, 2, 50, 1
#define M6_MODE 0, 2, 20, 1
#define M7_MODE 0, 2, 10, 1
#define M8_MODE 0, 2, 5, 1

#define SERVER 1
#define CLIENT 2

#define PID_PATH "/var/run/svpn_%s_%s_%d.pid"
#define FIFO_PATH "/var/run/svpn_%s_%d.fifo"

#define length(c) strlen(c)+1
#endif

struct _mcrypt_st
{
    MCRYPT td;
    int blocksize;
    char enc_state[1024];;
    int enc_state_size;
};
typedef struct _mcrypt_st mcrypt_t;

struct _frame_st
{
    uint16_t len;
    char buff[RCV_BUFF_LEN];
};
typedef struct _frame_st frame_t;

struct _kcpsess_st
{
    int dead;

    ikcpcb *kcp;
    int conv;

    char key[64];

    int dev_fd;
    int sock_fd;
    struct sockaddr_in dst;
    socklen_t dst_len;

    pthread_t kcp2devt;
    pthread_t readdevt;
    pthread_t dev2kcpt;
    pthread_mutex_t ikcp_mutex;

    sigset_t dev2kcp_sigset;
    sigset_t kcp2dev_sigset;

    uint64_t last_alive_time;
    uint32_t latest_send_iclock;

    mcrypt_t de_mcrypt;
    mcrypt_t en_mcrypt;

    char write_dev_buff[RCV_BUFF_LEN];
    int write_dev_buff_len; 
    char write_dev_buff_lz4[RCV_BUFF_LEN];
    int write_dev_buff_lz4_len; 

    char write_udp_buff[RCV_BUFF_LEN];
    int write_udp_buff_len; 
    char write_udp_buff_lz4[RCV_BUFF_LEN];
    int write_udp_buff_lz4_len; 

    rqueue_t *dev2kcp_queue;
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

void init_global_config(int role, int mode, int lz4, int recombine, int debug_param, int crypt, char *crypt_algo, char *crypt_mode);

void init_server_config(char *server_addr, int server_port);

void start_thread(pthread_t *tid, char *name, void *func, void *param);

void stop_thread(pthread_t tid);

void logging(char *name, char *message, ...);

void set_debug(int debug_param);

void set_fifo_fd(int param);

void usr_signal(int signo);

void exit_signal(int signo);

void set_cpu_affinity();

int init_tap(int conv);

kcpsess_t *init_kcpsess(int conv, int dev_fd, char *key, int sock_fd);

int udp_output(const char *buf, int len, ikcpcb *kcp, void *user);

void *alive(void *data);

void *readudp_client(void *data);

void *readudp_server(void *data);

void *kcp2dev(void *data);

void *readdev(void *data);

void *dev2kcp(void *data);

void kcpupdate_client(kcpsess_t *kcps);

void *kcpupdate_server(void *data);

/* get system time */
static inline void itimeofday(long *sec, long *usec)
{
	struct timeval time;
	gettimeofday(&time, NULL);
	if (sec) *sec = time.tv_sec;
	if (usec) *usec = time.tv_usec;
}

static int64_t timstamp()
{    
	struct timeval tv;    
	gettimeofday(&tv,NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;    
}

/* get clock in millisecond 64 */
static inline IINT64 iclock64(void)
{
	long s, u;
	IINT64 value;
	itimeofday(&s, &u);
	value = ((IINT64)s) * 1000 + (u / 1000);
	return value;
}

/* get clock in millisecond 32 */
static inline IUINT32 iclock()
{
	return (IUINT32)(iclock64() & 0xfffffffful);
}

/* sleep in millisecond */
static inline void isleep(float mseconds)
{
	long us = mseconds*1000;
	usleep(us);
}