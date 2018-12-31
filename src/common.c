#include "common.h"

#ifndef _COMMON_
#define _COMMON_ 1
#define TABLE_SIZE 64
#define KV_LEN 64
#endif

static hashtable_t *thread_table;
static rbt_t *log_table;
static int fifo_fd;
static int debug = false;

struct main_st
{
    int pid;
    char pid_path[128];
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

void _init_logging()
{
    char arr[TABLE_SIZE][KV_LEN] = ENABLED_LOG;
    int i = 0;
    log_table = rbt_create(libhl_cmp_keys_int16, free);
    for (i = 0; i < TABLE_SIZE; i++)
    {
        if (length(arr[i]) > 0)
        {
            rbt_add(log_table, arr[i], length(arr[i]), NULL);
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
    if (rbt_find(log_table, log_name, length(log_name), &data) == 0)
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
    ht_set_copy(thread_table, key, length(key), name, length(name), &data, &data_len);
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
    ht_delete(thread_table, key, length(key), &prev, &prev_len);
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
    if (ht_exists(thread_table, key, length(key)) == 1)
    {
        size_t data_len;
        void *data = ht_get(thread_table, key, length(key), &data_len);
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

void init_global_config(int role, int mode, int lz4, int debug_param, int crypt, char *crypt_algo, char *crypt_mode)
{
    debug = debug_param;

    global_main = malloc(sizeof(main_t));
    global_main->lz4 = lz4;
    global_main->role = role;
    global_main->mode = mode;

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
    // fcntl(dev, F_SETFL, flags | O_NONBLOCK);
    logging("init_tap", "init tap dev success. fd: %d", dev);
    return dev;
}

uint32_t _get_conv(void *buf)
{
    uint32_t conv_id;
    memcpy(&conv_id, buf, 4);
    return conv_id;
}

void _init_kcp(kcpsess_t *ps)
{
    ikcpcb *kcp_ = ikcp_create(ps->conv, ps);
    logging("init_kcp", "ikcp_create, kcps: %p, kcp: %p, buffer: %p", ps, kcp_, kcp_->buffer);
    int mode = global_main->mode;
    // 启动快速模式
    // 第二个参数 nodelay-启用以后若干常规加速将启动
    // 第三个参数 interval为内部处理时钟，默认设置为 10ms
    // 第四个参数 resend为快速重传指标，设置为2
    // 第五个参数 为是否禁用常规流控，这里禁止
    switch (mode)
    {
    case 0:
        ikcp_nodelay(kcp_, M0_MODE);
        break;
    case 1:
        ikcp_nodelay(kcp_, M1_MODE);
        break;
    case 2:
        ikcp_nodelay(kcp_, M2_MODE);
        break;
    case 3:
        ikcp_nodelay(kcp_, M3_MODE);
        break;
    case 4:
        ikcp_nodelay(kcp_, M4_MODE);
        break;
    case 5:
        ikcp_nodelay(kcp_, M5_MODE);
        break;
    case 6:
        ikcp_nodelay(kcp_, M6_MODE);
        break;
    case 7:
        ikcp_nodelay(kcp_, M7_MODE);
        break;
    case 8:
        ikcp_nodelay(kcp_, M8_MODE);
        break;
    default:
        ikcp_nodelay(kcp_, M3_MODE);
        break;
    }
    ikcp_wndsize(kcp_, SND_WINDOW, RSV_WINDOW);
    ikcp_setmtu(kcp_, MTU);

    kcp_->rx_minrto = RX_MINRTO;
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

kcpsess_t *init_kcpsess(int conv, int dev_fd, char *key, int sock_fd)
{
    kcpsess_t *ps = (kcpsess_t *)malloc(sizeof(kcpsess_t));
    bzero(ps, sizeof(kcpsess_t));
    ps->sock_fd = sock_fd;
    ps->dev_fd = dev_fd;
    ps->conv = conv;
    ps->dead = 0;
    strcpy(ps->key, key);
    pthread_mutex_t ikcp_mutex = PTHREAD_MUTEX_INITIALIZER;
    ps->ikcp_mutex = ikcp_mutex;

    _init_kcp(ps);
    _init_mcrypt(&ps->de_mcrypt_udp2dev, ps->key);
    _init_mcrypt(&ps->en_mcrypt_dev2udp, ps->key);
    ps->dev2kcp_queue = rqueue_create(16384, RQUEUE_MODE_BLOCKING);
    logging("init_kcpsess", "kcps: %p", ps);
    return ps;
}

int _check_kcp(kcpsess_t *kcps)
{
    if (!kcps->kcp || kcps->kcp->state == -1)
    {
        pthread_mutex_lock(&kcps->ikcp_mutex);
        _init_kcp(kcps);
        pthread_mutex_unlock(&kcps->ikcp_mutex);
    }
}

void _check_kcp_state(kcpsess_t *kcps, int sock_fd, struct sockaddr_in *client, socklen_t client_len)
{
    _check_kcp(kcps);
    if (kcps->sock_fd==-1) {
        kcps->sock_fd = sock_fd;
    }
    if (memcmp(&kcps->dst, client, client_len)!=0) {
        memcpy(&kcps->dst, client, client_len);
        kcps->dst_len = client_len;
    }
}

void *alive(void *data)
{
}

void *readudp_client(void *data)
{
    kcpsess_t *kcps = (kcpsess_t *)data;
    char buff[RCV_BUFF_LEN];
    while (1)
    {
        int cnt = recvfrom(kcps->sock_fd, buff, RCV_BUFF_LEN, 0, (struct sockaddr *)&kcps->dst, &(kcps->dst_len));
        if (cnt < 0)
        {
            continue;
        }
        _check_kcp(kcps);
        logging("readudp_client", "recvfrom udp packet: %d addr: %s", cnt, inet_ntoa(kcps->dst.sin_addr));

        pthread_mutex_lock(&kcps->ikcp_mutex);
        int ret = ikcp_input(kcps->kcp, buff, cnt);
        pthread_mutex_unlock(&kcps->ikcp_mutex);

        pthread_kill(kcps->writedevt, SIGRTMIN);
        logging("signal", "send a SIGRTMIN to %s, result: %d , kcp->state: %d", _thread_name(kcps->writedevt), ret, kcps->kcp->state);
    }
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
        logging("readudp_server", "recvfrom udp packet: %d addr: %s port: %d", cnt, inet_ntoa(client.sin_addr), ntohs(client.sin_port));
        if (cnt < 24) //24(KCP)
        {
            continue;
        }
        char conv_str[8];
        bzero(conv_str, 8);
        sprintf(conv_str, "%d", _get_conv(buff));
        if (ht_exists(server_listen->conn_map, conv_str, length(conv_str)) == 0)
        {
            logging("readudp_server", "CONV NOT EXISTS or NOT INIT COMPLETED %s", conv_str);
            continue;
        }
        else
        {
            size_t data_len;
            void *data = ht_get(server_listen->conn_map, conv_str, length(conv_str), &data_len);
            kcpsess_t *kcps = (kcpsess_t *)data;
            _check_kcp_state(kcps, server_listen->sock_fd, &client, client_len);
            logging("readudp_server", "recvfrom udp packet: %d addr: %s, port: %d kcps: %p, kcp: %p", cnt, inet_ntoa(kcps->dst.sin_addr), ntohs(client.sin_port), kcps, kcps->kcp);

            pthread_mutex_lock(&kcps->ikcp_mutex);
            int ret = ikcp_input(kcps->kcp, buff, cnt);
            pthread_mutex_unlock(&kcps->ikcp_mutex);

            pthread_kill(kcps->writedevt, SIGRTMIN);
            logging("signal", "send a SIGRTMIN to %s, result: %d , kcp->state: %d", _thread_name(kcps->writedevt), ret, kcps->kcp->state);
        }
    }
}

int _encrypt_encompress(kcpsess_t *kcps, char **buff, int *buff_len)
{
    char *ptr = kcps->write_udp_buff;
    int len = kcps->write_udp_buff_len;
    if (global_main->lz4)
    {
        kcps->write_udp_buff_lz4_len = LZ4_compress_default(kcps->write_udp_buff, kcps->write_udp_buff_lz4, kcps->write_udp_buff_len, RCV_BUFF_LEN);
        if (kcps->write_udp_buff_lz4_len < 0)
        {
            logging("notice", "encompress failed");
            return -1;
        }
        ptr = kcps->write_udp_buff_lz4;
        len = kcps->write_udp_buff_lz4_len;
        logging("_encrypt_encompress", "encompress data: %d", len);
    }
    if (global_crypt->crypt)
    {
        mcrypt_generic(kcps->en_mcrypt_dev2udp.td, ptr, len);
        mcrypt_enc_set_state(kcps->en_mcrypt_dev2udp.td, kcps->en_mcrypt_dev2udp.enc_state, kcps->en_mcrypt_dev2udp.enc_state_size);
        if (kcps->en_mcrypt_dev2udp.td == MCRYPT_FAILED)
        {
            logging("notice", "encrypt failed");
            return -1;
        }
        logging("_encrypt_encompress", "encrypt data: %d", len);
    }
    memcpy(buff, &ptr, sizeof(char *));
    memcpy(buff_len, &len, sizeof(int));
    return 0;
}

int _decrypt_decompress(kcpsess_t *kcps, char **buff, int *buff_len)
{
    char *ptr = kcps->write_dev_buff;
    int len = kcps->write_dev_buff_len;
    if (global_crypt->crypt)
    {
        mdecrypt_generic(kcps->de_mcrypt_udp2dev.td, kcps->write_dev_buff, kcps->write_dev_buff_len);
        mcrypt_enc_set_state(kcps->de_mcrypt_udp2dev.td, kcps->de_mcrypt_udp2dev.enc_state, kcps->de_mcrypt_udp2dev.enc_state_size);
        if (kcps->de_mcrypt_udp2dev.td == MCRYPT_FAILED)
        {
            logging("notice", "decrypt failed");
            return -1;
        }
    }
    if (global_main->lz4)
    {
        kcps->write_dev_buff_lz4_len = LZ4_decompress_safe(kcps->write_dev_buff, kcps->write_dev_buff_lz4, kcps->write_dev_buff_len, RCV_BUFF_LEN);
        if (kcps->write_dev_buff_lz4_len < 0)
        {
            logging("notice", "decompress failed");
            return -1;
        }
        else
        {
            ptr = kcps->write_dev_buff_lz4;
            len = kcps->write_dev_buff_lz4_len;
        }
    }
    memcpy(buff, &ptr, sizeof(char *));
    memcpy(buff_len, &len, sizeof(int));
    return 0;
}

void *writedev(void *data)
{
    kcpsess_t *kcps = (kcpsess_t *)data;
    if (pthread_sigmask(SIG_BLOCK, &kcps->writedev_sigset, NULL) != 0)
    {
        logging("warning", "error to pthread_sigmask writedev_sigset.");
    }
    while (kcps->dead == 0)
    {
        int sig;
        if (sigwait(&kcps->writedev_sigset, &sig) == -1)
        {
            logging("warning", "error sigwait.");
        }
        logging("signal", "receive a writedev_sigset.");
        _check_kcp(kcps);
        pthread_mutex_lock(&kcps->ikcp_mutex);
        kcps->write_dev_buff_len = ikcp_recv(kcps->kcp, kcps->write_dev_buff, RCV_BUFF_LEN);
        pthread_mutex_unlock(&kcps->ikcp_mutex);
        if (kcps->write_dev_buff_len<=0){
            logging("warning", "recv data from kcp: %d", kcps->write_dev_buff_len);
            continue;
        }
        char *buff;
        int cnt;
        if (_decrypt_decompress(kcps, &buff, &cnt) == -1)
        {
            logging("warning", "faile to decrypt and decompress, r_addr: %s len: %d content: %s", inet_ntoa(kcps->dst.sin_addr), cnt, buff + 2);
            continue;
        }
        int total_frms = 0;
        memcpy(&total_frms, buff, 2);
        if (total_frms <= 0 || total_frms > 7)
        {
            logging("warning", "alive frame or illegal data, r_addr: %s len: %d content: %s", inet_ntoa(kcps->dst.sin_addr), cnt, buff + 2); //alive OR illegal
            continue;
        }
        int position = 16;
        int i = 0;
        for (i = 0; i < total_frms; i++)
        {
            int frm_size;
            memcpy(&frm_size, buff + (i + 1) * 2, 2);
            int y = write(kcps->dev_fd, buff + position, frm_size);
            logging("writedev", "write to dev: idx: %d, position: %d, size: %d, wrote: %d", i, position, frm_size, y);
            position += frm_size;
        }
    }
    logging("notice", "writedev thread go to dead, conv: %d", kcps->conv);
}

void *readdev(void *data)
{
    kcpsess_t *kcps = (kcpsess_t *)data;
    while (kcps->dead == 0)
    {
        frame_t *frame = malloc(sizeof(frame_t));
        frame->len = read(kcps->dev_fd, frame->buff, RCV_BUFF_LEN);
        if (frame->len > 0)
        {
            logging("readdev", "read data from tap, len: %d", frame->len);
            rqueue_write(kcps->dev2kcp_queue, frame);
            pthread_kill(kcps->writeudpt, SIGRTMIN + 1);
        }
        else
        {
            logging("readdev", "read from tap dev faile: %d", frame->len);
            free(frame);
        }
    }
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
void *writeudp(void *data)
{
    kcpsess_t *kcps = (kcpsess_t *)data;
    if (pthread_sigmask(SIG_BLOCK, &kcps->writeudp_sigset, NULL) != 0)
    {
        logging("warning", "error to pthread_sigmask writeudp_sigset.");
    }
    while (kcps->dead == 0)
    {
        int sig;
        if (sigwait(&kcps->writeudp_sigset, &sig) == -1)
        {
            logging("warning", "error sigwait.");
        }
        logging("signal", "receive a writeudp_sigset.");
        _check_kcp(kcps);
        while (rqueue_isempty(kcps->dev2kcp_queue) == 0)
        {
            char *buff = kcps->write_udp_buff;
            uint16_t total_frms = 0;
            uint16_t position = 16;
            while (rqueue_isempty(kcps->dev2kcp_queue) == 0 && total_frms <= 5) // not empty or >= 5 frames.
            {
                frame_t *frame = (frame_t *)rqueue_read(kcps->dev2kcp_queue);
                total_frms++;
                position += frame->len;
                memcpy(buff + total_frms * 2, &frame->len, 2);
                memcpy(buff + position, frame->buff, frame->len);
                free(frame);
            }
            memcpy(buff, &total_frms, 2);
            int len;
            if (_encrypt_encompress(kcps, &buff, &len) == -1)
            {
                logging("warning", "faile to decrypt and decompress, r_addr: %s len: %d", inet_ntoa(kcps->dst.sin_addr), len);
                continue;
            }
            pthread_mutex_lock(&kcps->ikcp_mutex);
            ikcp_send(kcps->kcp, buff, len);
            ikcp_update(kcps->kcp, iclock());
            pthread_mutex_unlock(&kcps->ikcp_mutex);
        }
    }
}

int udp_output(const char *buf, int len, ikcpcb *kcp, void *user)
{
    logging("udp_output", "udp_output-1 %ld, length %d", timstamp(), len);
    kcpsess_t *kcps = (kcpsess_t *)user;
    if (kcps->sock_fd == -1)
    {
        logging("warning", "socket not opened");
        return 0;
    }
    int cnt = sendto(kcps->sock_fd, buf, len, 0, (struct sockaddr *)&kcps->dst, kcps->dst_len);
    if (cnt < 0)
    {
        logging("udp_output", "udp send failed, addr: %s port: %d.", inet_ntoa(kcps->dst.sin_addr), ntohs(kcps->dst.sin_port));
    }
    logging("udp_output", "kcp.state: %d ", kcp->state);
    return 0;
}
