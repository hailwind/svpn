#include "common.h"

#ifndef _COMMON_
#define _COMMON_ 1
#define TABLE_SIZE 64
#define KV_LEN 64
#endif

static hashtable_t *thread_table;
static rbt_t *log_table;

struct main_st
{
    int pid;
    char pid_path[128];
    int debug;
    int lz4;
    int role;
    int mode;
    char address[128];
    int port;
};
typedef struct main_st main_t;
static main_t *global_main;

struct crypt_st
{
    int crypt;
    char crypt_algo[128];
    char crypt_mode[128];
};
typedef struct crypt_st crypt_t;
static crypt_t *global_crypt;

struct server_st
{
    char fifo_path[128];
    int fifo_fd;
};
typedef struct server_st server_t;
static server_t *global_server;

void _init_logging()
{
    char arr[TABLE_SIZE][KV_LEN] = ENABLED_LOG;
    int i = 0;
    log_table = rbt_create(libhl_cmp_keys_int16, free);
    for (i = 0; i < TABLE_SIZE; i++)
    {
        if (strlen(arr[i]) > 0)
        {
            rbt_add(log_table, arr[i], strlen(arr[i]), NULL);
        }
    }
}

int _enabled_log(char *log_name)
{
    if (log_table == NULL)
    {
        _init_logging();
    }
    void *data;
    if (rbt_find(log_table, log_name, strlen(log_name), &data) == 0)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

void _reg_thread(pthread_t p_tid, char *name)
{
    if (thread_table == NULL)
    {
        thread_table = ht_create(TABLE_SIZE, 0, NULL);
    }
    char key[KV_LEN];
    bzero(key, KV_LEN);
    sprintf(key, "t_%ld", p_tid);
    void *data;
    size_t data_len;
    ht_set_copy(thread_table, key, strlen(key), name, strlen(name), &data, &data_len);
}

void _un_reg_thread(pthread_t p_tid)
{
    if (thread_table == NULL)
    {
        thread_table = ht_create(TABLE_SIZE, 0, NULL);
    }
    char key[KV_LEN];
    bzero(key, KV_LEN);
    sprintf(key, "t_%ld", p_tid);
    void *prev;
    size_t prev_len;
    ht_delete(thread_table, key, strlen(key), &prev, &prev_len);
    free(prev);
}

char *_thread_name(pthread_t p_tid)
{
    if (thread_table == NULL)
    {
        thread_table = ht_create(TABLE_SIZE, 0, NULL);
    }
    char key[KV_LEN];
    bzero(key, KV_LEN);
    sprintf(key, "t_%ld", p_tid);
    if (ht_exists(thread_table, key, strlen(key)) == 1)
    {
        size_t data_len;
        void *data = ht_get(thread_table, key, strlen(key), &data_len);
        return (char *)data;
    }
    return "default";
}

void _create_pid(char *role, char *ipaddr, int id)
{
    global_main->pid = getpid();
    FILE *pid_fd;
    char f_name[256];
    bzero(f_name, 256);
    sprintf(f_name, PID_PATH, role, ipaddr, id);
    if ((pid_fd = fopen(f_name, "wt+")) == NULL)
    {
        logging("notice", "create pid file: %s fd: %d failed.", f_name, pid_fd);
        exit(1);
    }
    fprintf(pid_fd, "%d", global_main->pid);
    fclose(pid_fd);
}

void _erase_pid(char *role, char *ipaddr, int id)
{
    char f_name[256];
    bzero(f_name, 256);
    sprintf(f_name, PID_PATH, role, ipaddr, id);
    unlink(f_name);
}

void init_global_config(int role, int mode, int lz4, int debug, int crypt, char *crypt_algo, char *crypt_mode)
{
    global_main = malloc(sizeof(main_t));
    global_main->debug = debug;
    global_main->lz4 = lz4;
    global_main->role = role;
    global_main->mode = mode;

    global_crypt = malloc(sizeof(crypt_t));
    global_crypt->crypt = crypt;
    strcpy(global_crypt->crypt_algo, crypt_algo);
    strcpy(global_crypt->crypt_mode, crypt_mode);
    srand(time(NULL));
}

void init_server_config(char *server_addr, int server_port, ...) {
    strcpy(global_main->address, server_addr);
    global_main->port = server_port;
    if (global_main->role==SERVER) {
        global_server = malloc(sizeof(server_t));
        _create_pid("server", server_addr, server_port);
    }else{
        _create_pid("client", server_addr, server_port);
    }
}

void init_ulimit()
{
    struct rlimit nofile_rlmt;
    nofile_rlmt.rlim_cur = nofile_rlmt.rlim_max = 8192;
    int rv = setrlimit(RLIMIT_NOFILE, &nofile_rlmt);
    if (rv != 0)
    {
        logging("error", "Failed to set nofile rlimit.\n");
    }

    struct rlimit core_rlmt;
    core_rlmt.rlim_cur = core_rlmt.rlim_max = RLIM_INFINITY;
    rv = setrlimit(RLIMIT_CORE, &core_rlmt);
    if (rv != 0)
    {
        logging("error", "Failed to set core rlimit.\n");
    }
}

void start_thread(pthread_t *tid, char *name, void *func, void *param)
{
    if (pthread_create(tid, NULL, func, param) == 0)
    {
        pthread_t x = *tid;
        _reg_thread(x, name);
        pthread_detach(x);
        logging("notice", "create %s thread: %ld", name, tid);
    }
}

void stop_thread(pthread_t tid)
{
    if (tid <= 0)
    {
        return;
    }
    if (pthread_cancel(tid) == 0)
    {
        _un_reg_thread(tid);
        logging("notice", "stop thread: %ld", tid);
        // sleep(1);
    }
}

void logging(char *log_name, char *message, ...)
{
    if (global_main->debug == 1 || _enabled_log(log_name) == 1)
    {
        time_t now = time(NULL);
        char timestr[KV_LEN];
        bzero(timestr, KV_LEN);
        strftime(timestr, KV_LEN, "%Y-%m-%d %H:%M:%S", localtime(&now));
        pthread_t t_id = pthread_self();
        char *t_name = _thread_name(t_id);
        long int tid = syscall(SYS_gettid);
        if (strcmp(log_name, "warning") == 0)
        {
            printf("\033[33m[%s] [%s-%ld] [%s] ", timestr, t_name, tid, log_name);
        }
        else if (strcmp(log_name, "error") == 0)
        {
            printf("\033[31m[%s] [%s-%ld] [%s] ", timestr, t_name, tid, log_name);
        }
        else
        {
            printf("[%s] [%s-%ld] [%s] ", timestr, t_name, tid, log_name);
        }
        va_list argptr;
        va_start(argptr, message);
        vfprintf(stdout, message, argptr);
        va_end(argptr);
        printf("\033[0m\n");
        fflush(stdout);
    }
}

void set_debug(int enabled)
{
    global_main->debug = enabled;
}

void _erase_fifo(int fd, char *ip_addr, int port)
{
    if (fd > 0)
    {
        close(fd);
    }
    char fifo_file[50];
    bzero(&fifo_file, 50);
    sprintf(fifo_file, FIFO_PATH, ip_addr, port);
    unlink(fifo_file);
}

void usr_signal(int signo)
{
    if (signo == SIGUSR2)
        set_debug(true);
    if (signo == SIGUSR1)
        set_debug(false);
}

void exit_signal(int signo)
{
    if (signo == SIGINT || signo == SIGQUIT || signo == SIGTERM)
    {
        if (global_main->role == SERVER)
        {
            _erase_pid("server", global_main->address, global_main->port);
            _erase_fifo(global_server->fifo_fd, global_main->address, global_main->port);
        }
        else
        {
            _erase_pid("client", global_main->address, global_main->port);
        }
        logging("notice", "exit.");
        exit(0);
    }
}

int init_tap(int conv)
{
    int dev, err;
    char tun_device[] = "/dev/net/tun";
    char devname[20];
    bzero(devname, 20);
    sprintf(devname, "tap%d", conv);
    logging("init_tap", "devname: %s", devname);
    struct ifreq ifr;
    bzero(&ifr, sizeof(struct ifreq));
    if ((dev = open(tun_device, O_RDWR)) < 0)
    {
        logging("init_tap", "open(%s) failed: %s", tun_device, strerror(errno));
        exit(2);
    }
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    strncpy(ifr.ifr_name, devname, strlen(devname));
    if ((err = ioctl(dev, TUNSETIFF, (void *)&ifr)) < 0)
    {
        logging("init_tap", "ioctl(TUNSETIFF) failed");
        exit(3);
    }
    int flags;
    if (-1 == (flags = fcntl(dev, F_GETFL, 0)))
    {
        flags = 0;
    }
    // fcntl(dev, F_SETFL, flags | O_NONBLOCK);
    logging("init_tap", "init tap dev success. fd: %d", dev);
    return dev;
}

kcpsess_t *init_kcpsess(int conv, int dev_fd, char *key, int sock_fd)
{
    kcpsess_t *ps = (kcpsess_t *)malloc(sizeof(kcpsess_t));
    bzero(ps, sizeof(kcpsess_t));
    ps->sock_fd = sock_fd;
    ps->dev_fd = dev_fd;
    ps->conv = conv;
    ps->kcp = NULL;
    ps->sess_id = 0;
    ps->dead = 0;
    strncpy(ps->key, key, strlen(key));
    pthread_mutex_t ikcp_mutex = PTHREAD_MUTEX_INITIALIZER;
    ps->ikcp_mutex = ikcp_mutex;
    logging("init_kcpsess", "kcps: %p", ps);
    return ps;
}

void *alive(void *data)
{
}

void *readudp_client(void *data)
{
}

void *readudp_server(void *data)
{
}

void *writedev(void *data)
{
}

void *readdev(void *data)
{
}

void *writeudp(void *data)
{
}
