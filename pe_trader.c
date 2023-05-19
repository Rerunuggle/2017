#include "pe_trader.h"

// buffer to read the pipe message from parent
char msg_from_parent[128] = {0};

// string to be written to the pipe
char msg[128];

// two variable for reading and writing 
int readfd;     // read from FIFO_EXCHANGE
int writefd;    // write to FIFO_TRADER


// initial balance
int balance = 0;

//initial order ID
int order_id = 0;

// signal flag
volatile int flag = 0;

void sigusr1_handler(int signum){
    flag = 1;
}

int main(int argc, char ** argv) {
    if (argc < 2) {
        printf("Not enough arguments\n");
        //fflush(stdout);
        return 1;
    }

    // register signal handler
    struct sigaction sa;
    sa.sa_handler = sigusr1_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if(sigaction(SIGUSR1, &sa, NULL) == -1){
        perror("Signal action error");
        exit(1);
    }
    // connect to named pipes

    // pipe names and the corresponding required action
    // pipe from parent to child should read
    char pipe_p_to_c[19] = {0};
    sprintf(pipe_p_to_c, FIFO_EXCHANGE, atoi(argv[1]));

    readfd = open(pipe_p_to_c, O_RDONLY);
    if (readfd < 0)
        perror("open read pipe failed\n");


    // pipe from child to parent should write
    char pipe_c_to_p[19] = {0};
    sprintf(pipe_c_to_p, FIFO_TRADER, atoi(argv[1]));

    writefd = open(pipe_c_to_p, O_WRONLY);
    if (writefd<0)
        perror("open write pipe failed\n");

    // event loop:

    // wait for exchange update (MARKET message)
    // send order
    // wait for exchange confirmation (ACCEPTED message)
    while(1){
        // if no signal recieved
        while(flag == 0){
            pause();
        }
        // reset flag
        flag = 0;

        // read from pipe
        read(readfd, msg_from_parent, sizeof(msg_from_parent));

        if (strncmp(msg_from_parent, "MARKET SELL ", strlen("MARKET SELL ")) == 0){
            // reformat message to pipe
            char product[10];
            int quantity;
            int price;

            sscanf(msg_from_parent, "MARKET SELL %s %d %d", product, &quantity, &price);

            if(quantity >= 1000){
                close(readfd);
                close(writefd);
                unlink(pipe_c_to_p);
                unlink(pipe_p_to_c);
                exit(0);
            }

            sprintf(msg, "BUY %d %s %d %d;", order_id, product, quantity, price);

            // write msg to pipe
            ssize_t wr = write(writefd, msg, strlen(msg));
            if (wr < 0){
                perror("child failed to write to pipe\n");
            }
            order_id += 1;
            // send signal to the parent process
            if(kill(getppid(), SIGUSR1) == -1){
                perror("Send signal to parent failed\n");
                exit(1);
            }
        }
        
    }

    return 0;
}
