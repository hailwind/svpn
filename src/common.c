#define _GNU_SOURCE
#include "common.h"
#include <sched.h>

#ifndef _COMMON_
#define _COMMON_ 1
#define TABLE_SIZE 64
#define KV_LEN 64
#define M_LABEL 0x90ABCDEF
#endif

typedef struct ok_map_of(const char *, char *) str_map_t;

static str_map_t *thread_map;
static str_map_t *log_table;
static int fifo_fd;
static int debug = false;

struct main_st
{
    int pid;
    char pid_path[128];
    int lz4;
    int recombine;
    int role;
    char mode;
    char mode_params[128];
    char address[128];
    int port;
    int minrto;
    int cpu_affinity;
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

void _init_logging()
{
    char arr[TABLE_SIZE][KV_LEN] = ENABLED_LOG;
    int i = 0;
    log_table = malloc(sizeof(str_map_t));
    ok_map_init(log_table);
    for (i = 0; i < TABLE_SIZE; i++)
    {
        if (length(arr[i]) > 0)
        {
            ok_map_put(log_table, arr[i], arr[i]);
        }
    }
}

int _enabled_log(char *log_name)
{
    if (log_table == NULL)
    {
        _init_logging();
    }
    if (ok_map_get(log_table, log_name))
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
    if (thread_map == NULL)
    {
        thread_map = malloc(sizeof(str_map_t));
        ok_map_init(thread_map);
    }
    char key[KV_LEN];
    bzero(key, KV_LEN);
    sprintf(key, "t_%ld", p_tid);
    ok_map_put(thread_map, key, name);
}

void _un_reg_thread(pthread_t p_tid)
{
    if (thread_map == NULL)
    {
        return;
    }
    char key[KV_LEN];
    bzero(key, KV_LEN);
    sprintf(key, "t_%ld", p_tid);
    void *prev = ok_map_get(thread_map, key);
    ok_map_remove(thread_map, key);
    free(prev);
}

char *_thread_name(pthread_t p_tid)
{
    if (thread_map == NULL)
    {
        return "default";
    }
    char key[KV_LEN];
    bzero(key, KV_LEN);
    sprintf(key, "t_%ld", p_tid);

    void *data = ok_map_get(thread_map, key);
    if (data)
    {
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
        logging("error", "create pid file: %s fd: %d failed.", f_name, pid_fd);
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

void init_global_config(int role, char mode, char *mode_params, int minrto, int lz4, int recombine, int debug_param, int crypt, char *crypt_algo, char *crypt_mode, int cpu_affinity)
{
    debug = debug_param;
    global_main = malloc(sizeof(main_t));
    global_main->lz4 = lz4;
    global_main->recombine = recombine;
    global_main->role = role;
    global_main->mode = mode;
    switch (global_main->mode)
    {
    case 'd':
        strcpy(global_main->mode_params, MD_MODE);
        break;
    case '1':
        strcpy(global_main->mode_params, M1_MODE);
        break;
    case '2':
        strcpy(global_main->mode_params, M2_MODE);
        break;
    case '3':
        strcpy(global_main->mode_params, M3_MODE);
        break;
    case '4':
        strcpy(global_main->mode_params, M4_MODE);
        break;
    default:
        strcpy(global_main->mode_params, M1_MODE);
        break;
    }
    if (mode_params) strcpy(global_main->mode_params, mode_params);

    global_main->minrto = minrto;
    global_main->cpu_affinity = cpu_affinity;

    global_crypt = malloc(sizeof(crypt_t));
    global_crypt->crypt = crypt;
    strcpy(global_crypt->crypt_algo, crypt_algo);
    strcpy(global_crypt->crypt_mode, crypt_mode);
    srand(time(NULL));
}

void init_server_config(char *server_addr, int server_port)
{
    strcpy(global_main->address, server_addr);
    global_main->port = server_port;
    if (global_main->role == SERVER)
    {
        _create_pid("server", server_addr, server_port);
    }
    else
    {
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

void print_params()
{
    printf("===================parameters>>>>>>>>>>>>>>>>>>>\n");
    printf("Role              : %s\n", global_main->role == SERVER ? "Server" : "Client");
    printf("KCP Mode          : %c\n", global_main->mode);
    printf("Kcp Params        : %s\n", global_main->mode_params);
    printf("KCP Minrto        : %d\n", global_main->minrto);
    printf("LZ4               : %s\n", global_main->lz4 == 1 ? "true" : "false");
    printf("Recombine         : %s\n", global_main->recombine == 1 ? "true" : "false");
    printf("Address           : %s\n", global_main->address);
    printf("Port              : %d\n", global_main->port);
    printf("Crypt             : %s\n", global_crypt->crypt == 1 ? "true" : "false");
    printf("Crypt Algo        : %s\n", global_crypt->crypt_algo);
    printf("Crypt Mode        : %s\n", global_crypt->crypt_mode);
    printf("<<<<<<<<<<<<<<<<<<<parameters===================\n");
}
/*
readudp,(0)->kcp2dev(M)(1)
			\kcp2devd(D)(2)

readdev(M)(0)
	\dev2kcp(D)(3)

kcpupdate(3)
*/
int _set_cpu_affinity(pthread_t tid, char *name)
{
    int cpu = -1;
    if (global_main->cpu_affinity==0) {
        return cpu;
    }

    int cpus = get_nprocs();
    if (cpus == 1)
    {
        return cpu;
    }
    cpu_set_t mask;
    if (cpus == 4)
    {
        CPU_ZERO(&mask);
        if (strcmp("readudp", name) == 0)
        {
            CPU_SET(0, &mask);
            cpu = 0;
        }
        else if (strcmp("readdev", name) == 0)
        {
            CPU_SET(0, &mask);
            cpu = 0;
        }
        else if (strcmp("kcp2dev", name) == 0)
        {
            CPU_SET(1, &mask);
            cpu = 1;
        }
        else if (strcmp("kcp2devd", name) == 0)
        {
            CPU_SET(2, &mask);
            cpu = 2;
        }
        else if (strcmp("dev2kcp", name) == 0)
        {
            CPU_SET(3, &mask);
            cpu = 3;
        }else{
            CPU_SET(3, &mask);
            cpu = 3;
        }
        pthread_setaffinity_np(tid, sizeof(mask), &mask);
    }
    return cpu;
}

void start_thread(pthread_t *tid, char *name, void *func, void *param)
{
    if (pthread_create(tid, NULL, func, param) == 0)
    {
        pthread_t x = *tid;
        _reg_thread(x, name);
        pthread_detach(x);
        int cpu = _set_cpu_affinity(x, name);
        logging("notice", "create %s thread: %ld, cpu affinity: %d", name, tid, cpu);
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
        char *t_name = _thread_name(tid);
        logging("notice", "stop thread: %s", t_name);
        _un_reg_thread(tid);
        // sleep(1);
    }
}

void logging(char *log_name, char *message, ...)
{
    if (debug == true || _enabled_log(log_name) == 1)
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

void set_debug(int debug_param)
{
    debug = debug_param;
}

void _erase_fifo(char *ip_addr, int port)
{
    if (fifo_fd > 0)
    {
        close(fifo_fd);
    }
    char fifo_file[128];
    bzero(&fifo_file, 128);
    sprintf(fifo_file, FIFO_PATH, ip_addr, port);
    unlink(fifo_file);
}

void set_fifo_fd(int param)
{
    fifo_fd = param;
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
            _erase_fifo(global_main->address, global_main->port);
        }
        else
        {
            _erase_pid("client", global_main->address, global_main->port);
        }
        logging("notice", "exit normally.");
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
    strcpy(ifr.ifr_name, devname);
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
    fcntl(dev, F_SETFL, flags | O_NONBLOCK);
    logging("init_tap", "init tap dev success. fd: %d", dev);
    return dev;
}

void _init_kcp(kcpsess_t *ps)
{
    ikcpcb *kcp_ = ikcp_create(ps->conv, ps);
    logging("init_kcp", "ikcp_create, conv: %d kcps: %p, kcp: %p, buffer: %p", ps->conv, ps, kcp_, kcp_->buffer);
    char mode = global_main->mode;
    //0, 40, 0, 0
    int nodelay=0;
    int interval=40;
    int resend=0;
    int nc=0;
    int i=0;
    char *mPtr = NULL;
    mPtr = strtok(global_main->mode_params, ",");
    while (mPtr != NULL) {
        if (i==0) {
            nodelay=atoi(mPtr);
        }
        if (i==1) {
            interval=atoi(mPtr);
        }
        if (i==2) {
            resend=atoi(mPtr);
        }
        if (i==3) {
            nc=atoi(mPtr);
        }
        i++;
        mPtr = strtok(NULL, ",");
    }
    // 启动快速模式
    // 第二个参数 nodelay-启用以后若干常规加速将启动
    // 第三个参数 interval为内部处理时钟，默认设置为 10ms
    // 第四个参数 resend为快速重传指标，设置为2
    // 第五个参数 nc为是否禁用常规流控，这里禁止
    ikcp_nodelay(kcp_, nodelay, interval, resend, nc);
    ikcp_wndsize(kcp_, SND_WINDOW, RCV_WINDOW);
    ikcp_setmtu(kcp_, MTU);

    kcp_->rx_minrto = global_main->minrto;
    kcp_->output = udp_output;
    if (ps->kcp)
    {
        logging("init_kcp", "release kcp: %p, buffer: %p", ps->kcp, ps->kcp->buffer);
        ikcp_release(ps->kcp);
    }
    ps->kcp = kcp_;
}

void _init_mcrypt(mcrypt_t *mcrypt, char *key)
{
    if (global_crypt->crypt == 1)
    {
        mcrypt->td = mcrypt_module_open(global_crypt->crypt_algo, NULL, global_crypt->crypt_mode, NULL);
        if (mcrypt->td == MCRYPT_FAILED)
        {
            logging("init_mcrypt", "mcrypt_module_open failed algo=%s mode=%s key=%s keysize=%d", global_crypt->crypt_algo, global_crypt->crypt_mode, key, sizeof(key));
            exit(3);
        }
        int iv_size = mcrypt_enc_get_iv_size(mcrypt->td);
        char *IV = malloc(iv_size);
        bzero(IV, iv_size);
        int i = 0;
        for (i = 0; i < iv_size; i++)
        {
            IV[i] = rand();
        }
        mcrypt->blocksize = mcrypt_enc_get_block_size(mcrypt->td);
        logging("init_mcrypt", "mcrypt init, key:%s len:%d", key, strlen(key));
        int ret = mcrypt_generic_init(mcrypt->td, key, strlen(key), NULL);
        if (ret < 0)
        {
            mcrypt_perror(ret);
            exit(3);
        }
        mcrypt->enc_state_size = sizeof mcrypt->enc_state;
        mcrypt_enc_get_state(mcrypt->td, mcrypt->enc_state, &mcrypt->enc_state_size);
    }
}

kcpsess_t *init_kcpsess(uint32_t conv, int dev_fd, char *key)
{
    kcpsess_t *ps = (kcpsess_t *)malloc(sizeof(kcpsess_t));
    bzero(ps, sizeof(kcpsess_t));
    ps->dev_fd = dev_fd;
    ps->conv = conv;
    ps->dead = 0;
    if (key) strcpy(ps->key, key);
    pthread_mutex_t ikcp_mutex = PTHREAD_MUTEX_INITIALIZER;
    ps->ikcp_mutex = ikcp_mutex;
    _init_kcp(ps);
    logging("init_kcpsess", "kcps: %p", ps);
    return ps;
}

int _encrypt_encompress(kcpsess_t *kcps, mcrypt_t *en_mcrypt, char *write_udp_buff, int write_udp_buff_len)
{
    int len = write_udp_buff_len;
    if (global_main->lz4)
    {
        char write_udp_buff_lz4[RCV_BUFF_LEN];
        int write_udp_buff_lz4_len;
        write_udp_buff_lz4_len = LZ4_compress_default(write_udp_buff, write_udp_buff_lz4, write_udp_buff_len, RCV_BUFF_LEN);
        if (write_udp_buff_lz4_len < 0)
        {
            logging("warning", "encompress failed");
            return -1;
        }
        else
        {
            memcpy(write_udp_buff, write_udp_buff_lz4, write_udp_buff_lz4_len);
            len = write_udp_buff_lz4_len;
            logging("_encrypt_encompress", "encompress data: %d", len);
        }
    }
    if (global_crypt->crypt)
    {
        mcrypt_generic(en_mcrypt->td, write_udp_buff, len);
        mcrypt_enc_set_state(en_mcrypt->td, en_mcrypt->enc_state, en_mcrypt->enc_state_size);
        if (en_mcrypt->td == MCRYPT_FAILED)
        {
            logging("warning", "encrypt failed");
            return -1;
        }
    }
    return len;
}

int _decrypt_decompress(kcpsess_t *kcps, mcrypt_t *de_mcrypt, char *write_dev_buff, int write_dev_buff_len)
{
    int len = write_dev_buff_len;
    if (global_crypt->crypt)
    {
        mdecrypt_generic(de_mcrypt->td, write_dev_buff, write_dev_buff_len);
        mcrypt_enc_set_state(de_mcrypt->td, de_mcrypt->enc_state, de_mcrypt->enc_state_size);
        if (de_mcrypt->td == MCRYPT_FAILED)
        {
            logging("warning", "decrypt failed");
            return -1;
        }
    }
    if (global_main->lz4)
    {
        char write_dev_buff_lz4[RCV_BUFF_LEN];
        int write_dev_buff_lz4_len;
        write_dev_buff_lz4_len = LZ4_decompress_safe(write_dev_buff, write_dev_buff_lz4, write_dev_buff_len, RCV_BUFF_LEN);
        if (write_dev_buff_lz4_len < 0)
        {
            logging("warning", "decompress failed");
            return -1;
        }
        else
        {
            memcpy(write_dev_buff, write_dev_buff_lz4, write_dev_buff_lz4_len);
            len = write_dev_buff_lz4_len;
        }
    }
    return len;
}

//	|<------------ 4 bytes ------------>|
//	+--------+--------+--------+--------+
//	|  conv                             | conv：Conversation, 会话序号，用于标识收发数据包是否一致
//	+--------+--------+--------+--------+ cmd: Command, 指令类型，代表这个Segment的类型
//	|  cmd   |  frg   |			  wnd       | frg: Fragment, 分段序号，分段从大到小，0代表数据包接收完毕
//	+--------+--------+--------+--------+ wnd: Window, 窗口大小
//	|								 ts									| ts: Timestamp, 发送的时间戳
//	+--------+--------+--------+--------+
//	|								 sn									| sn: Sequence Number, Segment序号
//	+--------+--------+--------+--------+
//	|							   una								| una: Unacknowledged, 当前未收到的序号，
//	+--------+--------+--------+--------+      即代表这个序号之前的包均收到
//	|								 len                | len: Length, 后续数据的长度
//	+--------+--------+--------+--------+

uint8_t get_cmd(void *buff)
{
    uint8_t cmd;
    memcpy(&cmd, buff + 4, 1);
    return cmd;
}

uint32_t get_sn(void *buf)
{
    uint32_t sn;
    memcpy(&sn, buf + 12, 4);
    return sn;
}

uint32_t get_una(void *buf)
{
    uint32_t una;
    memcpy(&una, buf + 16, 4);
    return una;
}

uint32_t get_conv(void *buf)
{
    // uint32_t conv_id;
    // memcpy(&conv_id, buf, 4);
    // return conv_id;

    return ikcp_getconv(buf);
}

void set_conv(void *p, uint32_t l) {
    ikcp_setconv(p, l);
}

/*
ack包中的una 对端记录的本地的snd_una
ack包中的sn 对端ack发回的本地的sn

数据包中的una 对端记录的本地的snd_una
数据包中的sn 对端发来的对端的sn

本地kcp的snd_nxt 本地下一次发送的sn
本地kcp的rcv_nxt 本地记录的对端下一次的sn
本地kcp的snd_una 本地发送未被对端ack的本地的sn
*/
// 判断对端重启的逻辑:　数据包，带过来的SN归0，带过来的本地的una归0，并且本地的snd_una不为0，即不是双方刚刚新启动.
int _check_kcp_client(kcpsess_t *kcps, char *buff)
{
    if (get_cmd(buff)==81 && get_sn(buff)==0 && get_una(buff) == 0 && kcps->kcp->snd_una != 0)
    {
        logging("notice", "server restart? snd_una: %d", kcps->kcp->snd_una);
        pthread_mutex_lock(&kcps->ikcp_mutex);
        _init_kcp(kcps);
        pthread_mutex_unlock(&kcps->ikcp_mutex);
        return 1;
    }
    return 0;
}

void _check_kcp_server(kcpsess_t *kcps, int sock_fd, struct sockaddr_in *client, socklen_t client_len, char *buff)
{
    if (kcps->binds[0].sock_fd == 0)
    {
        logging("notice", "sock_fd is not initial, set kcps->sock_fd : %d", sock_fd);
        kcps->binds[0].sock_fd = sock_fd;
        kcps->binds_cnt=1;
    }
    int64_t curr = timestamp();
    if ((curr-kcps->dst_update_time)>500) {
        if (memcmp(&kcps->dst, client, client_len) != 0)
        {
            memcpy(&kcps->dst, client, client_len);
            kcps->dst_len = client_len;
            logging("notice", "client addr or port changed, addr: %s port: %d", inet_ntoa(kcps->dst.sin_addr), ntohs(kcps->dst.sin_port));
        }
        kcps->dst_update_time=curr;
    }
    if (get_cmd(buff)==81 && get_sn(buff)==0 && get_una(buff) == 0 && kcps->kcp->snd_una != 0)
    {
        logging("notice", "client restart? snd_una: %d", kcps->kcp->snd_una);
        pthread_mutex_lock(&kcps->ikcp_mutex);
        _init_kcp(kcps);
        pthread_mutex_unlock(&kcps->ikcp_mutex);
    }
}
int new_socket_port()
{
    return rand()%(50000-45000+1)+45000;
}

int binding(char *bind_addr, int port)
{
    struct sockaddr_in server;
    bzero(&server, sizeof(server));
    int server_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_fd < 0)
    {
        logging("listening", "create socket fail!");
        exit(EXIT_FAILURE);
    }
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(bind_addr);
    server.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr *)&server, sizeof(server)))
    {
        logging("error", "udp bind() failed %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    else
    {
        logging("listening", "udp bind to :%d", port);
    }
    return server_fd;
}

void _renew_socket(kcpsess_t *kcps, int idx)
{
    int orig_sock_fd = kcps->binds[idx].sock_fd;
    close(orig_sock_fd);
    int port = new_socket_port();
    kcps->binds[idx].port = port;
    int sock_fd = binding(kcps->binds[idx].bind_addr, port);
    kcps->binds[idx].sock_fd = sock_fd;
    logging("notice", "renew socket, sock_fd: %d", sock_fd);
}

int _is_m_frame(char *buff)
{
    struct ether_header *eth_header;
    eth_header = (struct ether_header *)buff;
    uint16_t ether_type = ntohs(eth_header->ether_type);
    //printf("ehter_type: %#x\n", ether_type);
    if (ether_type == ETHERTYPE_IP)
    {
        struct ip *ip_packet = (struct ip *)(buff + 14);
        //printf("len %d proto: %#x\n", ntohs(ip_packet->ip_len), ip_packet->ip_p);
        if (ip_packet->ip_p == 0x1)
        {
            //printf("icmp\n");
            return 1;
        }
    }
    else if (ether_type == ETHERTYPE_ARP)
    {
        //printf("arp\n");
        return 1;
    }
    return 0;
}

int _choose_sock_fd(kcpsess_t *kcps)
{
    if (kcps->binds_cnt<=0)
    {
        return -1;
    }
    if (kcps->binds_cnt==1) {
        return kcps->binds[0].sock_fd;
    }
    int idx = rand() % kcps->binds_cnt;
    return kcps->binds[idx].sock_fd;
}

void _direct_write_udp(kcpsess_t *kcps, char *frame_buff, int len)
{
    char buff[RCV_BUFF_LEN];
    int cnt = 0;
    set_conv(buff, kcps->conv);
    // memcpy(buff, &kcps->conv, 4); 
    memcpy(buff + 4, frame_buff, len);
    cnt = len + 4;
    int sock_fd = _choose_sock_fd(kcps);
    if (sock_fd==-1)
    {
        logging("warning", "sock_fd failed.");
        return;
    }
    cnt = sendto(sock_fd, buff, cnt, 0, (struct sockaddr *)&kcps->dst, kcps->dst_len);
    if (cnt < 0)
    {
        logging("warning", "send udp packet failed, addr: %s port: %d.", inet_ntoa(kcps->dst.sin_addr), ntohs(kcps->dst.sin_port));
    }
    logging("_direct_write_udp", "udp sendto: %d", cnt);
}

void _direct_write_dev(kcpsess_t *kcps, char *buff, int len)
{
    int w = write(kcps->dev_fd, buff, len);
    logging("direct_write_dev", "wrote dev: %d", w);
}

void *readudp_client(void *data)
{
    client_kcps_t *c_kcps = (client_kcps_t *)data;
    kcpsess_t *kcps = c_kcps->kcps;
    char buff[RCV_BUFF_LEN];
    while (kcps->dead == 0)
    {
        int cnt = recvfrom(kcps->binds[c_kcps->idx].sock_fd, buff, RCV_BUFF_LEN, 0, (struct sockaddr *)&kcps->dst, &(kcps->dst_len));
        if (cnt < 0)
        {
            continue;
        }
        if (_is_m_frame(buff + 4) == 1)
        {
            _direct_write_dev(kcps, buff + 4, cnt-4);
            continue;
        }
        if (cnt < 24) //24(KCP)
        {
            if (cnt==-1 && errno == EAGAIN) {
                _renew_socket(kcps, c_kcps->idx);
            }
            continue;
        }
        logging("readudp_client", "state: %d, cmd %c, conv: %d, sn: %d, una: %d, snd_una %d, rcv_nxt %d, snd_nxt %d, length: %d", kcps->kcp->state, get_cmd(buff), get_conv(buff), get_sn(buff), get_una(buff), kcps->kcp->snd_una, kcps->kcp->rcv_nxt, kcps->kcp->snd_nxt, cnt);
        logging("readudp_client", "recv udp packet: %d addr: %s port: %d", cnt, inet_ntoa(kcps->dst.sin_addr), ntohs(kcps->dst.sin_port));
        _check_kcp_client(kcps, buff);
        pthread_mutex_lock(&kcps->ikcp_mutex);
        int ret = ikcp_input(kcps->kcp, buff, cnt);
        pthread_mutex_unlock(&kcps->ikcp_mutex);
    }
    return NULL;
}

void *readudp_server(void *data)
{
    server_listen_t *server_listen = (server_listen_t *)data;
    struct sockaddr_in client;
    socklen_t client_len = sizeof(struct sockaddr_in);
    char buff[RCV_BUFF_LEN];
    while (1)
    {
        int cnt = recvfrom(server_listen->sock_fd, buff, RCV_BUFF_LEN, 0, (struct sockaddr *)&client, &client_len);
        if (cnt < 0)
        {
            continue;
        }
        if (_is_m_frame(buff + 4) == 1) {
            char conv_str[8];
            bzero(conv_str, 8);
            sprintf(conv_str, "%d", get_conv(buff));
            kcpsess_t *kcps = ok_map_get(server_listen->conn_map, conv_str);
            if (kcps) {
                _check_kcp_server(kcps, server_listen->sock_fd, &client, client_len, buff);
                _direct_write_dev(kcps, buff + 4, cnt-4);
                continue;
            } else {
                logging("notice", "CONV NOT EXISTS or NOT INIT COMPLETED %s", conv_str);
                continue;
            }
        }
        
        if (cnt < 24) { //24(KCP)
            continue;
        }
        char conv_str[8];
        bzero(conv_str, 8);
        sprintf(conv_str, "%d", get_conv(buff));
        kcpsess_t *kcps = ok_map_get(server_listen->conn_map, conv_str);
        if (kcps) {
            logging("readudp_server", "state: %d, cmd %c, conv: %d, sn: %d, una: %d, snd_una %d, rcv_nxt %d, snd_nxt %d, length: %d", kcps->kcp->state, get_cmd(buff), get_conv(buff), get_sn(buff), get_una(buff), kcps->kcp->snd_una, kcps->kcp->rcv_nxt, kcps->kcp->snd_nxt, cnt);
            logging("readudp_server", "recv udp packet: %d addr: %s port: %d", cnt, inet_ntoa(kcps->dst.sin_addr), ntohs(kcps->dst.sin_port));
            _check_kcp_server(kcps, server_listen->sock_fd, &client, client_len, buff);
            pthread_mutex_lock(&kcps->ikcp_mutex);
            int ret = ikcp_input(kcps->kcp, buff, cnt);
            pthread_mutex_unlock(&kcps->ikcp_mutex);
        }else{
            logging("notice", "CONV NOT EXISTS or NOT INIT COMPLETED %s", conv_str);
            continue;
        }
    }
}

void _de_write_dev(kcpsess_t *kcps, char *buff, int len)
{
    logging("de_write_dev", "ikcp_recv: %d", len);
    if (global_main->recombine == true)
    {
        IUINT32 total_frms = ikcp_get32u(buff);
        // memcpy(&total_frms, buff, 4);
        
        if (total_frms <= 0 || total_frms > 7)
        {
            logging("warning", "alive frame or illegal data, total_frms: %d, r_addr: %s port: %d", total_frms, inet_ntoa(kcps->dst.sin_addr), ntohs(kcps->dst.sin_port)); //alive OR illegal
            return;
        }
        int position = 32;
        uint16_t i = 0;
        for (i = 0; i < total_frms; i++)
        {
            IUINT32 frm_size = ikcp_get32u(buff + (i + 1) * 4);
            // memcpy(&frm_size, buff + (i + 1) * 4, 4);

            int y = write(kcps->dev_fd, buff + position, frm_size);
            logging("de_write_dev", "write to dev: idx: %d, position: %d, size: %d, wrote: %d", i, position, frm_size, y);
            position += frm_size;
        }
    }
    else
    {
        int w = write(kcps->dev_fd, buff, len);
        logging("de_write_dev", "wrote dev: %d", w);
    }
}

int _is_m_packet(char *buff, int len)
{
    uint32_t label = M_LABEL;
    return memcmp(&label, buff + len - 4, 4);
}

void *kcp2dev(void *data)
{
    kcpsess_t *kcps = (kcpsess_t *)data;
    mcrypt_t de_mcrypt;
    _init_mcrypt(&de_mcrypt, kcps->key);
    int sleep_times;
    char buff[RCV_BUFF_LEN];
    while (kcps->dead == 0)
    {
        pthread_mutex_lock(&kcps->ikcp_mutex);
        int cnt = ikcp_recv(kcps->kcp, buff, RCV_BUFF_LEN);
        pthread_mutex_unlock(&kcps->ikcp_mutex);
        if (cnt <= 0)
        {
            isleep(1);
            sleep_times++;
            if (sleep_times >= 4000)
            {
                logging("kcp2dev", "didn't recv data from kcp by %d times.", sleep_times);
                sleep_times = 0;
            }
            continue;
        }
        else
        {
            sleep_times = 0;
            cnt = _decrypt_decompress(kcps, &de_mcrypt, buff, cnt);
            if (cnt == -1)
            {
                logging("warning", "faile to decrypt and decompress, r_addr: %s port: %d, len: %d", inet_ntoa(kcps->dst.sin_addr), ntohs(kcps->dst.sin_port), cnt);
            }
            _de_write_dev(kcps, buff, cnt);
        }
    }
    logging("notice", "writedev thread go to dead, conv: %d", kcps->conv);
    return NULL;
}

/*
0,1 int16 总帧数
2,3 int16 帧1的长度
4,5 int16 帧2的长度
6,7 int16 帧3的长度
8,9 int16 帧4的长度
10,11 int16 帧5的长度
12,13 int16 帧6的长度
14,15 int16 帧7的长度
*/
void *dev2kcp(void *data)
{
    kcpsess_t *kcps = (kcpsess_t *)data;
    char buff[RCV_BUFF_LEN];
    int buff_cnt = 32;
    IUINT32 total_frms = 0;

    mcrypt_t en_mcrypt;
    _init_mcrypt(&en_mcrypt, kcps->key);
    char frame_buff[RCV_BUFF_LEN];
    int sleep_times;
    while (kcps->dead == 0)
    {
        int cnt = read(kcps->dev_fd, frame_buff, RCV_BUFF_LEN);
        if (cnt <= 0)
        {
            isleep(1);
            sleep_times++;
        }else{
            IUINT32 len = cnt;
            logging("dev2kcp", "read tap: %u", len);
            if (_is_m_frame(frame_buff) == 1)
            {
                _direct_write_udp(kcps, frame_buff, len);
                continue;
            } else {
                total_frms++;
                if (global_main->recombine)
                {
                    ikcp_set32u(buff + total_frms * 4, len);
                    // memcpy(buff + total_frms * 4, &len, 4);    // frame length.
                    memcpy(buff + buff_cnt, frame_buff, len); // frame content.
                    ikcp_set32u(buff, total_frms);
                    // memcpy(buff, &total_frms, 4);
                    buff_cnt += len;
                }else{
                    memcpy(buff, frame_buff, len);
                    buff_cnt = len;
                }
                logging("dev2kcp", "total_frms: %d, buff_cnt: %d, len: %d", total_frms, buff_cnt, len);
            }
        }
        if ((global_main->recombine && total_frms>=5)
         || (global_main->recombine && total_frms>=1 && sleep_times>=10)
         || (!global_main->recombine && total_frms>=1)) {
            buff_cnt = _encrypt_encompress(kcps, &en_mcrypt, buff, buff_cnt);
            if (buff_cnt == -1)
            {
                logging("warning", "faile to decrypt and decompress, r_addr: %s len: %d", inet_ntoa(kcps->dst.sin_addr), buff_cnt);
                continue;
            }
            pthread_mutex_lock(&kcps->ikcp_mutex);
            int y = ikcp_send(kcps->kcp, buff, buff_cnt);
            ikcp_flush(kcps->kcp);
            pthread_mutex_unlock(&kcps->ikcp_mutex);
            logging("dev2kcp", "ikcp_send: %d", buff_cnt);
            total_frms = 0;
            buff_cnt = 32;
            sleep_times=0;
        }
    }
    return NULL;
}

int udp_output(const char *buf, int len, ikcpcb *kcp, void *user)
{
    kcpsess_t *kcps = (kcpsess_t *)user;
    logging("udp_output", "sent udp packet: %d, addr: %s port: %d", len, inet_ntoa(kcps->dst.sin_addr), ntohs(kcps->dst.sin_port));
    int sock_fd = _choose_sock_fd(kcps);
    if (sock_fd==-1)
    {
        return 0;
    }
    int cnt = sendto(sock_fd, buf, len, 0, (struct sockaddr *)&kcps->dst, kcps->dst_len);
    if (cnt < 0)
    {
        logging("warning", "send udp packet failed, addr: %s port: %d.", inet_ntoa(kcps->dst.sin_addr), ntohs(kcps->dst.sin_port));
    }
    logging("udp_output", "kcp.state: %d ", kcp->state);
    return 0;
}

void _kcpupdate(kcpsess_t *kcps)
{
    if (kcps->kcp)
    {
        // logging("kcpupdate", "ikcp_update.");
        pthread_mutex_lock(&kcps->ikcp_mutex);
        ikcp_update(kcps->kcp, iclock());
        pthread_mutex_unlock(&kcps->ikcp_mutex);
    }
}

void kcpupdate_client(kcpsess_t *kcps)
{
    while (1)
    {
        _kcpupdate(kcps);
        isleep(5);
    }
}

void *kcpupdate_server(void *data)
{
    kcps_map_t *conn_m = (kcps_map_t *)data;
    while (1)
    {
        int check_item_count = 0;
        ok_map_foreach(conn_m, const char *key, kcpsess_t *value) {
            _kcpupdate(value);
        }
        isleep(5);
    }
}
