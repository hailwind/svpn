#include "common.h"

void print_help() {
    printf("client --server=192.168.1.1 [--port=8888] --conv=28445 [--with-lz4] [--no-recombine] [--no-crypt] --crypt-key=0123456789012345678901234567890 [--crypt-algo=twofish] [--crypt-mode=cbc] [--mode=3] [--minrto=20] [--debug]\n");
    exit(0);
}

void start_conv(int dev_fd, int conv, struct sockaddr_in *dst, char *key)
{
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0)
    {
        logging("error", "create socket fail!");
        exit(EXIT_FAILURE);
    }
    kcpsess_t *kcps = init_kcpsess(conv, dev_fd, key, sock_fd);
    kcps->dst = *dst;
    kcps->dst_len = sizeof(struct sockaddr_in);
    //sigaddset(&kcps->dev2kcpm_sigset, SIGRTMIN + 1);
    //sigaddset(&kcps->kcp2devm_sigset, SIGRTMIN);
    pthread_t readudpt;
    start_thread(&readudpt, "readudp", readudp_client, (void *)kcps);
    start_thread(&kcps->readdevt, "readdev", readdev, (void *)kcps);
    start_thread(&kcps->kcp2devt, "kcp2dev", kcp2dev, (void *)kcps);
    start_thread(&kcps->kcp2devdt, "kcp2devd", kcp2devd, (void *)kcps);
    start_thread(&kcps->dev2kcpt, "dev2kcp", dev2kcp, (void *)kcps);
    kcpupdate_client(kcps);
}

static const struct option long_option[]={
    {"server",required_argument,NULL,'s'},
    {"port",required_argument,NULL,'p'},
    {"conv",required_argument,NULL,'c'},
    {"with-lz4",no_argument,NULL,'Z'},
    {"no-crypt",no_argument,NULL,'C'},
    {"no-recombine", no_argument, NULL, 'R'},
    {"crypt-key",required_argument,NULL,'k'},
    {"crypt-algo",required_argument,NULL,'A'},
    {"crypt-mode",required_argument,NULL,'M'},
    {"mode",required_argument,NULL,'m'}, 
    {"minrto", required_argument, NULL, 'r'},
    {"debug",no_argument,NULL,'d'},
    {"help",no_argument,NULL,'h'},
    {NULL,0,NULL,0}
};

int main(int argc, char *argv[]) {
    init_ulimit();
    logging("notice", "Client Starting.");
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
    int role=CLIENT; 
    int mode=3;
    int minrto=RX_MINRTO;
    int lz4=false; 
    int debug=false; 
    int recombine=true; //frame re recombine
    int crypt=true; 
    char *crypt_algo=MCRYPT_TWOFISH; 
    char *crypt_mode=MCRYPT_CBC;

    int conv=0;
    char *key = NULL;

    int opt=0;
    while((opt=getopt_long(argc,argv,"s:p:c:h",long_option,NULL))!=-1)
    {
        switch(opt)
        {
            case 0: break;
            case 's': 
                server_addr=optarg; break;
            case 'p': 
                server_port=atoi(optarg); break;
            case 'c': 
                conv=atoi(optarg); break;
            case 'Z':
                lz4=true; break;
            case 'R':
                recombine=false; break;
            case 'C':
                crypt=false; break;
            case 'k':
                key=optarg; break;
            case 'A': 
                crypt_algo = optarg; break;
            case 'M': 
                crypt_mode = optarg; break;
            case 'm': 
                mode = atoi(optarg); break;
            case 'r':
                minrto = atoi(optarg); break;
            case 'd': 
                debug=true; break;
            case 'h': 
                print_help(); exit(0);
        }
    }
    if (!server_addr || conv==0) {
        print_help();
        exit(1);
    }
    if(!key && strlen(key)<16 && strlen(key)>32) {
        logging("notice", "no key input or key too long, the length must be between 16 and 32");
        exit(1);
    }
    init_global_config(role, mode, minrto, lz4, recombine, debug, crypt, crypt_algo, crypt_mode);
    init_server_config(server_addr, server_port);
    print_params();

    int dev_fd = init_tap(conv);

    struct sockaddr_in ser_addr;
    bzero(&ser_addr, sizeof(struct sockaddr_in));
    ser_addr.sin_family = AF_INET;
    ser_addr.sin_addr.s_addr = inet_addr(server_addr);
    ser_addr.sin_port = htons(server_port);
    logging("notice", "open server_addr: %s, server_port: %d, key: %s", server_addr, server_port, key);

    start_conv(dev_fd, conv, &ser_addr, key);
}
