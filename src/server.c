#include "common.h"

#define MAX_CONVS 256

struct action_st
{
    char act[4];
    char conv[8];
    char key[36];
};
typedef struct action_st action_t;

hashtable_t *conv_session_map;

void print_help()
{
    printf("\
//start a server process.\n\
server --bind=192.168.1.2 [--port=8888] [--mode=3] [--minrto=20] [--with-lz4] [--no-recombine] [--no-crypt]  [--crypt-algo=twofish] [--crypt-mode=cbc] [--debug]\n\
//add a conv to a server, identify the server by ipaddr and port. \n\
server --bind=192.168.1.2 [--port=8888] --add-conv=38837 --crypt-key=0123456789012345678901234567890\n\
//del a conv from a server, identify the server by ipaddr and port. \n\
server --bind=192.168.1.2 [--port=8888] --del-conv=38837\n");
}

int listening(char *bind_addr, int port)
{
    struct sockaddr_in server;
    bzero(&server, sizeof(server));
    int server_fd = socket(AF_INET, SOCK_DGRAM, 0);
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

int open_fifo(char *ip_addr, int port, char rw)
{
    int fifo_fd;
    char fifo_file[50];
    bzero(&fifo_file, 50);
    sprintf(fifo_file, FIFO_PATH, ip_addr, port);
    /*
    0 exists
    2 write
    4 read
    */
    if (access(fifo_file, 0) == -1)
    {
        if (mkfifo(fifo_file, 666))
        {
            perror("Mkfifo error");
        }
        chmod(fifo_file, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    }
    if (rw == 'R')
    {
        fifo_fd = open(fifo_file, O_RDONLY);
    }
    else if (rw == 'W')
    {
        fifo_fd = open(fifo_file, O_WRONLY);
    }
    logging("notice", "open fifo: %s, fd: %d", fifo_file, fifo_fd);
    return fifo_fd;
}

void send_fifo(int fifo_fd, char *cmd, char *conv, char *key)
{
    char buf[128];
    bzero(&buf, 128);
    strcat(buf, cmd);
    strcat(buf, "&");
    strcat(buf, conv);
    if (key && length(key) >= 16 && length(key) <= 32)
    {
        strcat(buf, "&");
        strcat(buf, key);
    }
    else
    {
        if (strcmp("ADD", cmd)==0) {
            logging("notice", "no key input or key too long, the length must be between 16 and 32");
            print_help();
            exit(1);
        }
    }
    strcat(buf, "\n");
    int cnt = write(fifo_fd, buf, length(buf));
    logging("notice", "sent %d bytes: %s", cnt, buf);
}

int read_fifo(int fifo_fd, action_t *action)
{
    char buf[128];
    bzero(&buf, 128);
    int count = read(fifo_fd, buf, 127); //主线程将会BLOCK在这儿
    logging("notice", "read fifo %d bytes, content: %s", count, buf);
    if (count >= 7)
    {
        char *conv;
        char *key;
        int i;
        int cnt=0;
        for (i = 0; i < count; i++)
        {
            if (buf[i] == '&')
            {
                cnt++;
                buf[i] = '\0';
                if (cnt==1) {
                    conv = (void *)&buf + i + 1;
                }else if(cnt==2) {
                    key = (void *)&buf + i + 1;
                }
            }
            if (buf[i] == '\n')
            {
                buf[i] = '\0';
            }
        }
        if (buf) strcpy(action->act, buf);
        if (conv) strcpy(action->conv, conv);
        if (key) strcpy(action->key, key);
        logging("read_fifo", "action: %s, conv:%s, key:%s", action->act, action->conv, action->key);
        return true;
    }
    else
    {
        return false;
    }
}

void start_conv(action_t *action)
{
    if (ht_exists(conv_session_map, action->conv, length(action->conv)) == 0)
    {
        int dev_fd = init_tap(atoi(action->conv));
        kcpsess_t *kcps = init_kcpsess(atoi(action->conv), dev_fd, action->key, -1);    //SERVER DONT NEED sock_fd
        sigaddset(&kcps->dev2kcpm_sigset, SIGRTMIN + 1);
        // sigaddset(&kcps->kcp2dev_sigset, SIGRTMIN);
        start_thread(&kcps->readdevt, "readdev", readdev, (void *)kcps);
        start_thread(&kcps->kcp2devt, "kcp2dev", kcp2dev, (void *)kcps);
        start_thread(&kcps->dev2kcpmt, "dev2kcpm", dev2kcpm, (void *)kcps);
        start_thread(&kcps->dev2kcpt, "dev2kcp", dev2kcp, (void *)kcps);
        ht_set(conv_session_map, action->conv, length(action->conv), kcps, sizeof(kcpsess_t));
        logging("notice", "server init_kcpsess conv: %s key: %s kcps: %p", action->conv, action->key, kcps);
    }
    else
    {
        logging("notice", "conv %s exists.", action->conv);
    }
}

void stop_conv(action_t *action)
{
    if (ht_exists(conv_session_map, action->conv, length(action->conv)) == 1)
    {
        logging("notice", "stop conv %s", action->conv);
        size_t data_len;
        kcpsess_t *kcps = (kcpsess_t *)ht_get(conv_session_map, action->conv, length(action->conv), &data_len);
        kcps->dead = 1;
        stop_thread(kcps->readdevt);
        stop_thread(kcps->kcp2devt);
        stop_thread(kcps->dev2kcpt);
        stop_thread(kcps->dev2kcpmt);
        if (kcps->dev_fd > 0)
            close(kcps->dev_fd);
        if (kcps->kcp)
            ikcp_release(kcps->kcp);
        void *prev;
        size_t prev_len;
        ht_delete(conv_session_map, action->conv, length(action->conv), &prev, &prev_len);
        free(kcps);
    }
}

void wait_conv(char *server_addr, int server_port)
{
    while (1)
    {
        int fifo_fd = open_fifo(server_addr, server_port, 'R');
        set_fifo_fd(fifo_fd);   //当收到程序中止的信号时，尝试关闭fifo_fd.
        action_t action;
        if (read_fifo(fifo_fd, &action)==true)
        {
            if (strcmp("ADD", action.act) == 0)
            {
                start_conv(&action);
            }
            else if (strcmp("DEL", action.act) == 0)
            {
                stop_conv(&action);
            }
        }
        close(fifo_fd);
    }
}

struct option long_option[] = {
    {"bind", required_argument, NULL, 'b'},
    {"port", required_argument, NULL, 'p'},
    {"with-lz4", no_argument, NULL, 'Z'},
    {"no-crypt", no_argument, NULL, 'C'},
    {"no-recombine", no_argument, NULL, 'R'},
    {"crypt-key", required_argument, NULL, 'k'},
    {"crypt-algo", required_argument, NULL, 'A'},
    {"crypt-mode", required_argument, NULL, 'M'},
    {"mode", required_argument, NULL, 'm'},
    {"minrto", required_argument, NULL, 'r'},
    {"del", required_argument, NULL, 'X'},
    {"add", required_argument, NULL, 'Y'},
    {"del-conv", required_argument, NULL, 'X'},
    {"add-conv", required_argument, NULL, 'Y'},
    {"debug", no_argument, NULL, 'd'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};

int main(int argc, char *argv[])
{
    init_ulimit();
    logging("notice", "Server Starting.");
    if (signal(SIGUSR1, usr_signal) == SIG_ERR || signal(SIGUSR2, usr_signal) == SIG_ERR)
    {
        logging("warning", "Failed to register USR signal");
    }
    if (signal(SIGINT, exit_signal) == SIG_ERR || signal(SIGQUIT, exit_signal) == SIG_ERR || signal(SIGTERM, exit_signal) == SIG_ERR)
    {
        logging("warning", "Failed to register exit signal");
    }
    char *server_addr=NULL;
    int server_port = DEFAULT_SERVER_PORT;
    int role=SERVER; 
    int mode=3;
    int minrto=RX_MINRTO;
    int lz4=false; 
    int recombine=true; //frame re recombine
    int debug=false; 
    int crypt=true; 
    char *crypt_algo=MCRYPT_TWOFISH; 
    char *crypt_mode=MCRYPT_CBC;
    
    char *cmd = NULL;
    char *conv = NULL;
    char *key = NULL;

    int opt = 0;
    while ((opt = getopt_long(argc, argv, "p:c:h", long_option, NULL)) != -1)
    {
        switch (opt)
        {
        case 0:
            break;
        case 'b':
            server_addr = optarg; break;
        case 'p':
            server_port = atoi(optarg); break;
        case 'Z':
            lz4=true; break;
        case 'R':
            recombine=false; break;
        case 'C':
            crypt=false; break;
        case 'k':
            key = optarg; break;
        case 'A':
            crypt_algo = optarg; break;
        case 'M':
            crypt_mode = optarg; break;
        case 'm':
            mode = atoi(optarg); break;
        case 'r':
            minrto = atoi(optarg); break;
        case 'X':
            cmd = "DEL"; conv = optarg; break;
        case 'Y':
            cmd = "ADD"; conv = optarg; break;
        case 'd':
            debug=true; break;
        case 'h':
            print_help(); exit(0);
        }
    }
    if (!server_addr)
    {
        print_help();
        exit(1);
    }
    init_global_config(role, mode, minrto, lz4, recombine, debug, crypt, crypt_algo, crypt_mode);
    if (cmd && conv)
    {
        int fifo_fd = open_fifo(server_addr, server_port, 'W');
        send_fifo(fifo_fd, cmd, conv, key);
        close(fifo_fd);
        exit(0);
    }
    init_server_config(server_addr, server_port);
    print_params();

    conv_session_map = ht_create(MAX_CONVS, 0, NULL);
    char *mPtr = NULL;
    mPtr = strtok(server_addr, ",");
    int cnt = 0;
    while (mPtr != NULL) {
        cnt++;
        int sock_fd = listening(mPtr, server_port);
        server_listen_t *server = malloc(sizeof(server_listen_t));
        bzero(server, sizeof(server_listen_t));
        server->sock_fd = sock_fd;
        server->conn_map = conv_session_map;
        char temp[64];
        sprintf(temp, "readudp%d", cnt);
        start_thread(&server->readudpt, temp, readudp_server, (void *)server);
        mPtr = strtok(NULL, ",");
    }
    pthread_t kcpupdatet;
    start_thread(&kcpupdatet, "writeudp", kcpupdate_server, (void *)conv_session_map);
    wait_conv(server_addr, server_port);
}
