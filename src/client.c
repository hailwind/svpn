#include "common.h"

void print_help() {
    printf("client --server=192.168.1.1 [--port=8888] --conv=28445 [--with-lz4] [--no-crypt] --crypt-key=0123456789012345678901234567890 [--crypt-algo=twofish] [--crypt-mode=cbc] [--mode=3] [--debug]\n");
    exit(0);
}

void handle(int dev_fd, int conv, struct sockaddr_in *dst, char *key)
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
    sigaddset(&kcps->writeudp_sigset, SIGRTMIN + 1);
    sigaddset(&kcps->writedev_sigset, SIGRTMIN);
    pthread_t readudpt;
    start_thread(&readudpt, "readudp", readudp_client, (void *)kcps);
    start_thread(&kcps->readdevt, "readdev", readdev, (void *)kcps);
    start_thread(&kcps->writedevt, "writedev", writedev, (void *)kcps);
    start_thread(&kcps->writeudpt, "writeudp", writeudp, (void *)kcps);
    while(1) {
        sleep(60);
    }
}

static const struct option long_option[]={
   {"server",required_argument,NULL,'s'},
   {"port",required_argument,NULL,'p'},
   {"conv",required_argument,NULL,'c'},
   {"with-lz4",no_argument,NULL,'Z'},
   {"no-crypt",no_argument,NULL,'C'},
   {"crypt-key",required_argument,NULL,'k'},
   {"crypt-algo",required_argument,NULL,'A'},
   {"crypt-mode",required_argument,NULL,'M'},
   {"mode",required_argument,NULL,'m'}, 
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
    int lz4=false; 
    int debug=false; 
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
    init_global_config(role, mode, lz4, debug, crypt, crypt_algo, crypt_mode);
    init_server_config(server_addr, server_port);

    int dev_fd = init_tap(conv);

    struct sockaddr_in ser_addr;
    bzero(&ser_addr, sizeof(struct sockaddr_in));
    ser_addr.sin_family = AF_INET;
    ser_addr.sin_addr.s_addr = inet_addr(server_addr);
    ser_addr.sin_port = htons(server_port);
    logging("notice", "open server_addr: %s, server_port: %d, key: %s", server_addr, server_port, key);

    handle(dev_fd, conv, &ser_addr, key);
}