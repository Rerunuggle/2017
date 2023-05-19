/**
 * comp2017 - assignment 3
 * Ron Tang
 * ztan3471
 */

#include "pe_exchange.h"
#include <signal.h>
// product name is up to 16 characters, plus one null byte
#define MAX_PRODUCT_LENGTH 17
// length of PIPE_EXCHANGE plus the maximum length of an integer
#define MAX_PIPE_NAME_LENGTH 28
// output starting msg
#define MSG "[PEX]"
// maximum length of message from child (in pipes) 
// strlen("SELL ") + order id + " " + product name + " " + number of products + " " + price + ";" + null byte
// 5 + 12 + 1 + 16 + 1 + 12 + 1 + 12 + 1 + 1
#define MAX_MSG_FROM_C_LEN 62
// the maximum length of the string needed to print in the order book is
// SELL 2147483647 @ 2147483647 (2147483647 order)
// length is 5 + 10 + 3 + 10 + 2 + 10 + 7 + 1 = 48 bytes
#define MAX_ORDER_LENGTH 48
// maximum length of message to be written to pipe
// "MARKET SELL " + product name + quantity + price + 2 spaces + ";" + nul byte
// 12 + 16 + 1 + 10 + 1 + 10 + 1
#define MAX_TO_C_LEN 50

// fees earned
int fees = 0;

// signal flag
volatile int flag = 0;

// two sets of pipes
char** pipes_p_to_c;
char** pipes_c_to_p;

// product names
char** products;
int numofProducts = 0;

// pids of child processes
pid_t* trader_pids;

// number of traders
int trader_num;

// number of disconnected traders
int trader_disc = 0;

// order information
typedef struct{
	int trader; // 4
	int order_id; // 4
	int price; // 4
	int quantity;
	// type is "SELL" or "BUY", plus one nul byte
	char order_type[5];
}order_info;
// each product have several amount of order_info
typedef struct{
	char product_name[17];
	int nOfOrder;
	order_info* o_info;
}product_orders;
product_orders* products_orders;

// position information
typedef struct{
	char product_name[17];
	int num;
	int money;
}product_num;
typedef struct{
	int trader;
	product_num* product_nums;
}trader_possession;
trader_possession* traders_possession;

// trader and its order_id
int* ts_os;

void message_children_except(char*, pid_t);
void message_child(char*, pid_t);
void message_fill(int, pid_t*, int*, int*);
void free_all();
void print_order_book();
int compare_ints();
int count_buy_levels(product_orders);
int count_sell_levels(product_orders);
void print_orders(order_info*, int);
void add_order_book(char*, int, int, int, int, char*);
void modify_order_book(int, int, int, int, char*);
void print_position();
int can_match();
void match_orders();
void rearrange_orderbook();
int* get_prices_h_to_l(order_info*, int, int*);
int check_next_order(int);
void add_trader_order(int);

void sigusr1_handler(int signum){
	flag = 1;
	// block signals
	// sigset_t block_mask;
	// sigemptyset(&block_mask);
	// sigaddset(&block_mask, SIGUSR1);
	// // block the signals until setup is all done.
	// if (sigprocmask(SIG_BLOCK, &block_mask, NULL) == -1) {
	// 	printf("sigprocmask error\n");
	// 	free_all();
	// 	exit(1);
	// }
	// printf("signal blocked\n");
}

void sigchld_handler(int signum){
	int status, pid;
	pid = waitpid(-1, &status, WNOHANG);
	for(int i = 0; i < trader_num; ++i){
		if(trader_pids[i] == pid){
			unlink(pipes_c_to_p[i]);
			unlink(pipes_p_to_c[i]);
			printf("%s Trader %d disconnected\n", MSG, i);
		}
	}
	trader_disc += 1;
	if(trader_disc == trader_num){
		printf("%s Trading completed\n%s Exchange fees collected: $%d\n", MSG, MSG, fees);
		free_all();
		exit(0);
	}
}

void free_all(){
	// free
	free(trader_pids);
	for (int j = 0; j < numofProducts; ++j) {
		if(products_orders[j].o_info != NULL)
			free(products_orders[j].o_info);
		free(products[j]);
	}
	free(products_orders);
	free(products);
	for(int i = 0; i < trader_num; ++i){
		if(traders_possession[i].product_nums != NULL){
			free(traders_possession[i].product_nums);
		}
		free(pipes_p_to_c[i]);
		free(pipes_c_to_p[i]);
	}
	free(traders_possession);
	free(pipes_p_to_c);
	free(pipes_c_to_p);
	free(ts_os);
}

int compare_int(const void* a, const void* b) {
    return (*(int*)b - *(int*)a);
}

void print_order_book() {
    printf("%s\t--ORDERBOOK--\n", MSG);
    for (int i = 0; i < numofProducts; i++) {
        printf("%s\tProduct: %s; Buy levels: %d; Sell levels: %d\n", MSG, products_orders[i].product_name, count_buy_levels(products_orders[i]), count_sell_levels(products_orders[i]));
        print_orders(products_orders[i].o_info, products_orders[i].nOfOrder);
    }
}

int count_buy_levels(product_orders po) {
	// printf("counting buy levels, %s\n", po.product_name);
	// printf("first augument is \ntrader: %d order: %d price %d quantity %d type %s\n", 
	// 	po.o_info[0].order_id, po.o_info[0].order_id, po.o_info[0].price, po.o_info[0].quantity, po.o_info[0].order_type);
    int buy_levels = 0;
    int* unique_prices = (int*)malloc(po.nOfOrder*sizeof(int));
    int num_unique_prices = 0;

    for (int i = 0; i < po.nOfOrder; i++) {
        if (strcmp(po.o_info[i].order_type, "BUY") == 0) {
            int price = po.o_info[i].price;
            // Check if this price has already been encountered
            int is_new_price = 1;
            for (int j = 0; j < num_unique_prices; j++) {
                if (unique_prices[j] == price) {
                    is_new_price = 0;
                    break;
                }
            }
            // If this is a new price, increment the buy levels counter
            if (is_new_price) {
                buy_levels++;
                unique_prices[num_unique_prices] = price;
                num_unique_prices++;
            }
        }
    }
	free(unique_prices);
    return buy_levels;
}

int count_sell_levels(product_orders po) {
    int sell_levels = 0;
    int* unique_prices = (int*)malloc(po.nOfOrder*sizeof(int));
    int num_unique_prices = 0;

    for (int i = 0; i < po.nOfOrder; i++) {
        if (strcmp(po.o_info[i].order_type, "SELL") == 0) {
            int price = po.o_info[i].price;
            // Check if this price has already been encountered
            int is_new_price = 1;
            for (int j = 0; j < num_unique_prices; j++) {
                if (unique_prices[j] == price) {
                    is_new_price = 0;
                    break;
                }
            }
            // If this is a new price, increment the buy levels counter
            if (is_new_price) {
                sell_levels++;
                unique_prices[num_unique_prices] = price;
                num_unique_prices++;
            }
        }
    }
	free(unique_prices);
    return sell_levels;
}

void print_orders(order_info* orders, int num_orders) {
	// length of the prices
	int k = 0;
	int* prices = get_prices_h_to_l(orders, num_orders, &k);

	for(int i = 0; i < k; i ++){
		int s_orders = 0;
		int s_amt = 0;
		int b_orders = 0;
		int b_amt = 0;
		for(int j = 0; j < num_orders; j ++){
			if(prices[i] == orders[j].price){
				if(strcmp(orders[j].order_type, "BUY") == 0){
					b_orders += 1;
					b_amt += orders[j].quantity;
				}else if(strcmp(orders[j].order_type, "SELL") == 0){
					s_orders += 1;
					s_amt += orders[j].quantity;
				}
			}
		}
		if(b_orders > 0){
			if(b_orders == 1)
				printf("%s\t\t%s %d @ $%d (%d order)\n", MSG, "BUY", b_amt, prices[i], b_orders);
			else
				printf("%s\t\t%s %d @ $%d (%d orders)\n", MSG, "BUY", b_amt, prices[i], b_orders);
		}
		if(s_orders > 0){
			if(s_orders == 1)
				printf("%s\t\t%s %d @ $%d (%d order)\n", MSG, "SELL", s_amt, prices[i], s_orders);
			else
				printf("%s\t\t%s %d @ $%d (%d orders)\n", MSG, "SELL", s_amt, prices[i], s_orders);
		}
	}
	free(prices);
}

int* get_prices_h_to_l(order_info* orders, int num_orders, int* k){
	int* prices = (int*)malloc(num_orders*(sizeof(int)));
	int a = 0;
    for (int i = 0; i < num_orders; i++) {
        int price = orders[i].price;
        int found = 0;
        for (int j = 0; j < a; j++) {
            if (prices[j] == price) {
                found = 1;
                break;
            }
        }
        if (!found) {
            prices[a++] = price;
        }
    }
    // sort the prices array in descending order
    qsort(prices, a, sizeof(int), compare_int);

	*k = a;
	return prices;
}

void print_position(){
	printf("%s\t--POSITIONS--\n", MSG);
	for(int i = 0; i < trader_num; i ++){
		printf("%s\tTrader %d: ", MSG, i);
		for (int j = 0; j < numofProducts; j ++){
			if(j == numofProducts - 1){
				printf("%s %d ($%d)\n", traders_possession[i].product_nums[j].product_name, 
					traders_possession[i].product_nums[j].num, traders_possession[i].product_nums[j].money);
			}else{
				printf("%s %d ($%d), ", traders_possession[i].product_nums[j].product_name, 
					traders_possession[i].product_nums[j].num, traders_possession[i].product_nums[j].money);
			}
		}
	}
}

void add_order_book(char* pname, int trd, int o_id, int prc, int amt, char* type){
	// modify products_orders
	for(int j = 0; j < numofProducts; ++ j){
		if(strcmp(products_orders[j].product_name, pname) == 0){
			// printf("reallocating for %s\n", products_orders[j].product_name);
			products_orders[j].nOfOrder += 1;
			order_info* new = (order_info*)realloc(products_orders[j].o_info, (products_orders[j].nOfOrder) * sizeof(order_info));
			if(new == NULL){
				perror("products_orders reallocation fail\n");
				exit(1);
			}
			products_orders[j].o_info = new;
			products_orders[j].o_info[products_orders[j].nOfOrder - 1].trader = trd;
			products_orders[j].o_info[products_orders[j].nOfOrder - 1].order_id = o_id;
			products_orders[j].o_info[products_orders[j].nOfOrder - 1].price = prc;
			products_orders[j].o_info[products_orders[j].nOfOrder - 1].quantity = amt;
			strcpy(products_orders[j].o_info[products_orders[j].nOfOrder - 1].order_type, type);
		}
	}
}

void modify_order_book(int trader, int order_id, int price, int quantity, char* out_str) {
	// product name + space + SELL + nul
    char temp_str[16 + 1 + 4 + 1];
    for(int i = 0; i < numofProducts; i ++) {
        for(int j = 0; j < products_orders[i].nOfOrder; j ++) {
            if((products_orders[i].o_info[j].trader == trader) && (products_orders[i].o_info[j].order_id == order_id)) {
                products_orders[i].o_info[j].price = price;
                products_orders[i].o_info[j].quantity = quantity;
                snprintf(temp_str, 22, "%s %s", products_orders[i].product_name, products_orders[i].o_info[j].order_type);
                strncpy(out_str, temp_str, 22);
                return;
            }
        }
    }
    // if no matching order is found, return an empty string
    out_str[0] = '\0';
}

void message_child(char* message, pid_t pid){
	int writefd;
	// write to pipe and send signal
	for(int i = 0; i < trader_num; ++i){
		if(pid != trader_pids[i]){
			continue;
		}
		writefd = open(pipes_p_to_c[i], O_WRONLY);
		if(writefd == -1){
			break;
		}
        ssize_t wr = write(writefd, message, strlen(message));
        if (wr < 0){
            perror("failed to write to pipe\n");
			exit(1);
        }
		// send signal to the child process
        if(kill(pid, SIGUSR1) == -1){
            perror("Send signal to child failed\n");
			exit(1);
        }
		// for(int k = 0; k < trader_num; k ++){
		// 	printf("trader %d pid: %d\n", k, trader_pids[k]);
		// }
		// printf("message: %s, signal sent to T[%d], trader number is: %d\n", message, i, trader_pids[i]);
		return;
	}
}

void message_children_except(char* message, pid_t pid){
	int writefd;
	// write to pipe and send signal
	for(int i = 0; i < trader_num; ++i){
		if(pid == trader_pids[i]){
			continue;
		}
		writefd = open(pipes_p_to_c[i], O_WRONLY);
		if(writefd == -1){
			break;
		}
        ssize_t wr = write(writefd, message, strlen(message));
        if (wr < 0){
            perror("failed to write to pipe\n");
			exit(1);
        }
		// send signal to the child process
        if(kill(pid, SIGUSR1) == -1){
            perror("Send signal to child failed\n");
			exit(1);
        }
	}
}

void message_fill(int len, pid_t* traders, int* orders, int* quantities){
	for(int i = 0; i < len; i ++){
		char m[MAX_TO_C_LEN];
		sprintf(m, "FILL %d %d;", orders[i], quantities[i]);
		message_child(m, traders[i]);
	}
}

int can_match(){
	for(int i = 0; i < numofProducts; i ++){
		// have at least 2 orders 
		if(products_orders[i].nOfOrder <= 1){
			continue;
		}
		// needs to be different types
		int can = 0;
		for(int j = 0; j < products_orders[i].nOfOrder; j ++){
			// loop all and compare their types with the first one, if there are sell and buy there must be one different from the first order_type.
			if(strcmp(products_orders[i].o_info[0].order_type, products_orders[i].o_info[j].order_type)!= 0){
				can = 1;
				break;
			}
		}
		if(!can){
			continue;
		}

		// have both sell and buy, then for each product
		// prices array from high to low
		int len = 0;
		int* prices = get_prices_h_to_l(products_orders[i].o_info, products_orders[i].nOfOrder, &len);

		// loop through the prices
		for(int j = 0; j < len; j ++){
			int price = prices[j];
			for(int k = 0; k < products_orders[i].nOfOrder; k++){
				// find the corresponding price, SELL
				if((products_orders[i].o_info[k].price == price) && 
					(strcmp(products_orders[i].o_info[k].order_type, "SELL") == 0)){
					// compare this one's price with the later ones
					// older sell order is products_orders[i].o_info[k]
					for(int l = k; l < products_orders[i].nOfOrder; l ++){
						// match if the order is BUY and the price, and that
						// the BUY order is larger than or equal to the price of the SELL order
						// new buy order is products_orders[i].o_info[l]
						if((strcmp(products_orders[i].o_info[l].order_type, "BUY") == 0) 
							&& (products_orders[i].o_info[l].price >= products_orders[i].o_info[k].price)){
							// order matched
							free(prices);
							return 1;
						}
					}
				}
				else if((products_orders[i].o_info[k].price == price) && 
					(strcmp(products_orders[i].o_info[k].order_type, "BUY") == 0)){
					// compare this one's price with the later ones
					// older buy order is products_orders[i].o_info[k]
					for(int l = k; l < products_orders[i].nOfOrder; l ++){
						// match if the order is BUY and the price, and that
						// the BUY order is larger than or equal to the price of the SELL order
						// new sell order is products_orders[i].o_info[l]
						if((strcmp(products_orders[i].o_info[l].order_type, "SELL") == 0) 
							&& (products_orders[i].o_info[k].price >= products_orders[i].o_info[l].price)){
							// order matched
							free(prices);
							return 1;
						}
					}
				}
			}
		}
		free(prices);
	}
	return 0;
}

void match_orders(){
	for(int i = 0; i < numofProducts; i ++){
		// have at least 2 orders 
		if(products_orders[i].nOfOrder <= 1){
			continue;
		}
		// needs to be different types
		int can = 0;
		for(int j = 0; j < products_orders[i].nOfOrder; j ++){
			// loop all and compare their types with the first one, if there are sell and buy there must be one different from the first order_type.
			if(strcmp(products_orders[i].o_info[0].order_type, products_orders[i].o_info[j].order_type)!= 0){
				can = 1;
				break;
			}
		}
		if(!can){
			continue;
		}

		// have both sell and buy, then for each product
		// prices array from high to low
		int len = 0;
		int* prices = get_prices_h_to_l(products_orders[i].o_info, products_orders[i].nOfOrder, &len);
		
		// initialize the traders, orders and quantities to fill
		int fill_len = 0;
		pid_t* traders_fill = (pid_t*)malloc(1*sizeof(int));
		int* orders_fill = (int*)malloc(1*sizeof(int));
		int* quantities_fill = (int*)malloc(1*sizeof(int));
		// loop through the prices
		for(int j = 0; j < len; j ++){
			int price = prices[j];
			for(int k = 0; k < products_orders[i].nOfOrder; k++){
				// find the corresponding price, SELL
				if((products_orders[i].o_info[k].price == price) && 
					(strcmp(products_orders[i].o_info[k].order_type, "SELL") == 0)){
					// compare this one's price with the later ones
					// older sell order is products_orders[i].o_info[k]
					for(int l = k; l < products_orders[i].nOfOrder; l ++){
						// match if the order is BUY and the price, and that
						// the BUY order is larger than or equal to the price of the SELL order
						// new buy order is products_orders[i].o_info[l]
						if((strcmp(products_orders[i].o_info[l].order_type, "BUY") == 0) 
							&& (products_orders[i].o_info[l].price >= products_orders[i].o_info[k].price)){
							// order matched
							// calculate the amount and fees
							// The matching price is the price of the older order
							// sell - buy
							int diff = products_orders[i].o_info[k].quantity - products_orders[i].o_info[l].quantity;
							int amount = 0;

							if(diff <= 0){
								// buy remains, sell gone or both gone
								amount = products_orders[i].o_info[k].price * products_orders[i].o_info[k].quantity;
								if(diff == 0){
									// adding this order information to the fill arrays for sell trader
									traders_fill[fill_len] = trader_pids[products_orders[i].o_info[k].trader];
									orders_fill[fill_len] = products_orders[i].o_info[k].order_id;
									quantities_fill[fill_len] = products_orders[i].o_info[k].quantity;
									fill_len ++;
									pid_t* newt = realloc(traders_fill, (fill_len+1)*sizeof(int));
									if(newt == NULL){
										perror("reallocating in match orders fail\n");
										exit(1);
									}
									traders_fill = newt;
									int* newo = realloc(orders_fill, (fill_len+1)*sizeof(int));
									if(newo == NULL){
										perror("reallocating in match orders fail\n");
										exit(1);
									}
									orders_fill = newo;
									newo = realloc(quantities_fill, (fill_len+1)*sizeof(int));
									if(newo == NULL){
										perror("reallocating in match orders fail\n");
										exit(1);
									}
									quantities_fill = newo;

									// for buy trader
									traders_fill[fill_len] = trader_pids[products_orders[i].o_info[l].trader];
									orders_fill[fill_len] = products_orders[i].o_info[l].order_id;
									quantities_fill[fill_len] = products_orders[i].o_info[l].quantity;
									fill_len ++;
									newt = realloc(traders_fill, (fill_len+1)*sizeof(int));
									if(newt == NULL){
										perror("reallocating in match orders fail\n");
										exit(1);
									}
									traders_fill = newt;
									newo = realloc(orders_fill, (fill_len+1)*sizeof(int));
									if(newo == NULL){
										perror("reallocating in match orders fail\n");
										exit(1);
									}
									orders_fill = newo;
									newo = realloc(quantities_fill, (fill_len+1)*sizeof(int));
									if(newo == NULL){
										perror("reallocating in match orders fail\n");
										exit(1);
									}
									quantities_fill = newo;
								}else if(diff < 0){
									// adding this order information to the fill arrays for sell trader
                                    traders_fill[fill_len] = trader_pids[products_orders[i].o_info[k].trader];
                                    orders_fill[fill_len] = products_orders[i].o_info[k].order_id;
                                    quantities_fill[fill_len] = products_orders[i].o_info[k].quantity;
                                    fill_len ++;
                                    pid_t* newt = realloc(traders_fill, (fill_len+1)*sizeof(int));
                                    if(newt == NULL){
                                        perror("reallocating in match orders fail\n");
                                        exit(1);
                                    }
                                    traders_fill = newt;
                                    int* newo = realloc(orders_fill, (fill_len+1)*sizeof(int));
                                    if(newo == NULL){
                                        perror("reallocating in match orders fail\n");
                                        exit(1);
                                    }
                                    orders_fill = newo;
                                    newo = realloc(quantities_fill, (fill_len+1)*sizeof(int));
                                    if(newo == NULL){
                                        perror("reallocating in match orders fail\n");
                                        exit(1);
                                    }
                                    quantities_fill = newo;

                                    // for buy trader
                                    traders_fill[fill_len] = trader_pids[products_orders[i].o_info[l].trader];
                                    orders_fill[fill_len] = products_orders[i].o_info[l].order_id;
                                    quantities_fill[fill_len] = products_orders[i].o_info[k].quantity;
                                    fill_len ++;
                                    newt = realloc(traders_fill, (fill_len+1)*sizeof(int));
                                    if(newt == NULL){
                                        perror("reallocating in match orders fail\n");
                                        exit(1);
                                    }
                                    traders_fill = newt;
                                    newo = realloc(orders_fill, (fill_len+1)*sizeof(int));
                                    if(newo == NULL){
                                        perror("reallocating in match orders fail\n");
                                        exit(1);
                                    }
                                    orders_fill = newo;
                                    newo = realloc(quantities_fill, (fill_len+1)*sizeof(int));
                                    if(newo == NULL){
                                        perror("reallocating in match orders fail\n");
                                        exit(1);
                                    }
                                    quantities_fill = newo;
								}
							}else{
								// sell remains, buy gone
								amount = products_orders[i].o_info[k].price * products_orders[i].o_info[l].quantity;
								
								// adding this order information to the fill arrays for sell trader
                                traders_fill[fill_len] = trader_pids[products_orders[i].o_info[k].trader];
                                orders_fill[fill_len] = products_orders[i].o_info[k].order_id;
                                quantities_fill[fill_len] = products_orders[i].o_info[l].quantity;
                                fill_len ++;
                                pid_t* newt = realloc(traders_fill, (fill_len+1)*sizeof(int));
                                if(newt == NULL){
                                    perror("reallocating in match orders fail\n");
                                    exit(1);
                                }
                                traders_fill = newt;
                                int* newo = realloc(orders_fill, (fill_len+1)*sizeof(int));
                                if(newo == NULL){
                                perror("reallocating in match orders fail\n");
                                    exit(1);
                                   }
                                orders_fill = newo;
                                newo = realloc(quantities_fill, (fill_len+1)*sizeof(int));
                                if(newo == NULL){
                                    perror("reallocating in match orders fail\n");
                                    exit(1);
                                }
                                quantities_fill = newo;
							
                                // for buy trader
                                traders_fill[fill_len] = trader_pids[products_orders[i].o_info[l].trader];
                                orders_fill[fill_len] = products_orders[i].o_info[l].order_id;
                                quantities_fill[fill_len] = products_orders[i].o_info[l].quantity;
                                fill_len ++;
                                newt = realloc(traders_fill, (fill_len+1)*sizeof(int));
                                if(newt == NULL){
                                    perror("reallocating in match orders fail\n");
                                    exit(1);
                                }
                                traders_fill = newt;
                                newo = realloc(orders_fill, (fill_len+1)*sizeof(int));
                                if(newo == NULL){
                                    perror("reallocating in match orders fail\n");
                                    exit(1);
                                }
                                orders_fill = newo;
                                newo = realloc(quantities_fill, (fill_len+1)*sizeof(int));
                                if(newo == NULL){
                                    perror("reallocating in match orders fail\n");
                                    exit(1);
                                }
                                quantities_fill = newo;
							}
							double d = amount * (FEE_PERCENTAGE / 100.0);
							int fee = (int) (d + 0.5);
							fees += fee;

							// print match message new order is the later one.
							printf("%s Match: Order %d [T%d], New Order %d [T%d], value: $%d, fee: $%d.\n", MSG, 
								products_orders[i].o_info[k].order_id, products_orders[i].o_info[k].trader, 
								products_orders[i].o_info[l].order_id, products_orders[i].o_info[l].trader, amount, fee);
							

							// modify order book, changing the values
							int quantity = 0;
							if(diff == 0){
								printf("diff == 0\n");
								quantity = products_orders[i].o_info[l].quantity;
								products_orders[i].o_info[k].quantity = 0;
								products_orders[i].o_info[l].quantity = 0;
							}else if(diff > 0){
								quantity = products_orders[i].o_info[l].quantity;
								products_orders[i].o_info[k].quantity -= products_orders[i].o_info[l].quantity;
								products_orders[i].o_info[l].quantity = 0;
							}else{
								quantity = products_orders[i].o_info[k].quantity;
								products_orders[i].o_info[l].quantity -= products_orders[i].o_info[k].quantity;
								products_orders[i].o_info[k].quantity = 0;
							}


							// modify position
							int sell_trader = products_orders[i].o_info[k].trader;
							int buy_trader = products_orders[i].o_info[l].trader;
							for(int p = 0; p < trader_num; p ++){
								// older sell trader
								if(traders_possession[p].trader == sell_trader){
									for(int q = 0; q < numofProducts; q ++){
										if(strcmp(traders_possession[p].product_nums[q].product_name, products[i]) == 0){
											if(diff <= 0){
												// order all sold
												traders_possession[p].product_nums[q].num -= quantity;
											}else{
												// order partial sold
												traders_possession[p].product_nums[q].num -= quantity;
											}
											traders_possession[p].product_nums[q].money += amount;
										}
									}	
								}
								// newer buy trader, charge
								if(traders_possession[p].trader == buy_trader){
									for(int q = 0; q < numofProducts; q ++){
										if(strcmp(traders_possession[p].product_nums[q].product_name, products[i]) == 0){
											if(diff <= 0){
												// order all sold
												traders_possession[p].product_nums[q].num += quantity;
											}else{
												// order partial sold
												traders_possession[p].product_nums[q].num += quantity;
											}
											traders_possession[p].product_nums[q].money -= amount;
											traders_possession[p].product_nums[q].money -= fee;
										}
									}
								}
							}

							// call rearrange_orderbook(); which will delete the invalid orders and realloc memories
							rearrange_orderbook();
							// resacn
							k = 0;
						}
					}
				}
				// find the corresponding price, BUY
				else if((products_orders[i].o_info[k].price == price) && 
					(strcmp(products_orders[i].o_info[k].order_type, "BUY") == 0)){
					// compare this one's price with the later ones
					// older buy order is products_orders[i].o_info[k]
					for(int l = k; l < products_orders[i].nOfOrder; l ++){
						// match if the order is BUY and the price, and that
						// the BUY order is larger than or equal to the price of the SELL order
						// new sell order is products_orders[i].o_info[l]
						
						if((strcmp(products_orders[i].o_info[l].order_type, "SELL") == 0) 
							&& (products_orders[i].o_info[k].price >= products_orders[i].o_info[l].price)){
							// printf("buy price: %d, sell price: %d\n", products_orders[i].o_info[k].price, products_orders[i].o_info[l].price);
							// order matched
							// calculate the amount and fees
							// The matching price is the price of the older order
							// sell - buy
							int diff = products_orders[i].o_info[l].quantity - products_orders[i].o_info[k].quantity;
							int amount = 0;

							if(diff <= 0){
								// buy remains, sell gone or both gone
								amount = products_orders[i].o_info[k].price * products_orders[i].o_info[l].quantity;
								if(diff == 0){
									// adding this order information to the fill arrays for sell trader
                                    traders_fill[fill_len] = trader_pids[products_orders[i].o_info[l].trader];
                                    orders_fill[fill_len] = products_orders[i].o_info[l].order_id;
                                    quantities_fill[fill_len] = products_orders[i].o_info[l].quantity;
                                    fill_len ++;
                                    pid_t* newt = realloc(traders_fill, (fill_len+1)*sizeof(int));
                                    if(newt == NULL){
                                        perror("reallocating in match orders fail\n");
                                        exit(1);
                                    }
                                    traders_fill = newt;
                                    int* newo = realloc(orders_fill, (fill_len+1)*sizeof(int));
                                    if(newo == NULL){
                                        perror("reallocating in match orders fail\n");
                                        exit(1);
                                    }
                                    orders_fill = newo;
                                    newo = realloc(quantities_fill, (fill_len+1)*sizeof(int));
                                    if(newo == NULL){
                                        perror("reallocating in match orders fail\n");
                                        exit(1);
                                    }
                                    quantities_fill = newo;

                                    // for buy trader
                                    traders_fill[fill_len] = trader_pids[products_orders[i].o_info[k].trader];
                                    orders_fill[fill_len] = products_orders[i].o_info[k].order_id;
                                    quantities_fill[fill_len] = products_orders[i].o_info[k].quantity;
                                    fill_len ++;
                                    newt = realloc(traders_fill, (fill_len+1)*sizeof(int));
                                    if(newt == NULL){
                                        perror("reallocating in match orders fail\n");
                                        exit(1);
                                    }
                                    traders_fill = newt;
                                    newo = realloc(orders_fill, (fill_len+1)*sizeof(int));
                                    if(newo == NULL){
                                        perror("reallocating in match orders fail\n");
                                        exit(1);
                                    }
                                    orders_fill = newo;
                                    newo = realloc(quantities_fill, (fill_len+1)*sizeof(int));
                                    if(newo == NULL){
                                        perror("reallocating in match orders fail\n");
                                        exit(1);
                                    }
                                    quantities_fill = newo;
								}else if(diff < 0){
									// adding this order information to the fill arrays for sell trader
                                    traders_fill[fill_len] = trader_pids[products_orders[i].o_info[l].trader];
                                    orders_fill[fill_len] = products_orders[i].o_info[l].order_id;
                                    quantities_fill[fill_len] = products_orders[i].o_info[l].quantity;
                                    fill_len ++;
                                    pid_t* newt = realloc(traders_fill, (fill_len+1)*sizeof(int));
                                    if(newt == NULL){
                                        perror("reallocating in match orders fail\n");
                                        exit(1);
                                    }
                                    traders_fill = newt;
                                    int* newo = realloc(orders_fill, (fill_len+1)*sizeof(int));
                                    if(newo == NULL){
                                        perror("reallocating in match orders fail\n");
                                        exit(1);
                                    }
                                    orders_fill = newo;
                                    newo = realloc(quantities_fill, (fill_len+1)*sizeof(int));
                                    if(newo == NULL){
                                        perror("reallocating in match orders fail\n");
                                        exit(1);
                                    }
                                    quantities_fill = newo;

                                    // for buy trader
                                    traders_fill[fill_len] = trader_pids[products_orders[i].o_info[k].trader];
                                    orders_fill[fill_len] = products_orders[i].o_info[k].order_id;
                                    quantities_fill[fill_len] = products_orders[i].o_info[l].quantity;
                                    fill_len ++;
                                    newt = realloc(traders_fill, (fill_len+1)*sizeof(int));
                                    if(newt == NULL){
                                        perror("reallocating in match orders fail\n");
                                        exit(1);
                                    }
                                    traders_fill = newt;
                                    newo = realloc(orders_fill, (fill_len+1)*sizeof(int));
                                    if(newo == NULL){
                                        perror("reallocating in match orders fail\n");
                                        exit(1);
                                    }
                                    orders_fill = newo;
                                    newo = realloc(quantities_fill, (fill_len+1)*sizeof(int));
                                    if(newo == NULL){
                                        perror("reallocating in match orders fail\n");
                                        exit(1);
                                    }
                                    quantities_fill = newo;
								}
							}else{
								// sell remains, buy gone
								amount = products_orders[i].o_info[k].price * products_orders[i].o_info[k].quantity;

								// adding this order information to the fill arrays for sell trader
                                traders_fill[fill_len] = trader_pids[products_orders[i].o_info[l].trader];
                                orders_fill[fill_len] = products_orders[i].o_info[l].order_id;
                                quantities_fill[fill_len] = products_orders[i].o_info[k].quantity;
                                fill_len ++;
                                pid_t* newt = realloc(traders_fill, (fill_len+1)*sizeof(int));
                                if(newt == NULL){
                                    perror("reallocating in match orders fail\n");
                                    exit(1);
                                }
                                traders_fill = newt;
                                int* newo = realloc(orders_fill, (fill_len+1)*sizeof(int));
                                if(newo == NULL){
                                    perror("reallocating in match orders fail\n");
                                    exit(1);
                                }
                                orders_fill = newo;
                                newo = realloc(quantities_fill, (fill_len+1)*sizeof(int));
                                if(newo == NULL){
                                	perror("reallocating in match orders fail\n");
                                    exit(1);
                                }
                                quantities_fill = newo;

                                // for buy trader
                                traders_fill[fill_len] = trader_pids[products_orders[i].o_info[k].trader];
                                orders_fill[fill_len] = products_orders[i].o_info[k].order_id;
                                quantities_fill[fill_len] = products_orders[i].o_info[k].quantity;
                                fill_len ++;
                                newt = realloc(traders_fill, (fill_len+1)*sizeof(int));
                                if(newt == NULL){
                                    perror("reallocating in match orders fail\n");
                                    exit(1);
                                }
                                traders_fill = newt;
                                newo = realloc(orders_fill, (fill_len+1)*sizeof(int));
                                if(newo == NULL){
                                    perror("reallocating in match orders fail\n");
                                    exit(1);
                                }
                                orders_fill = newo;
                                newo = realloc(quantities_fill, (fill_len+1)*sizeof(int));
                                if(newo == NULL){
                                    perror("reallocating in match orders fail\n");
                                    exit(1);
                                }
                                quantities_fill = newo;
							}
							double d = amount * (FEE_PERCENTAGE / 100.0);
							int fee = (int) (d + 0.5);
							fees += fee;

							// print match message new order is the later one.
							printf("%s Match: Order %d [T%d], New Order %d [T%d], value: $%d, fee: $%d.\n", MSG, 
								products_orders[i].o_info[k].order_id, products_orders[i].o_info[k].trader, 
								products_orders[i].o_info[l].order_id, products_orders[i].o_info[l].trader, amount, fee);
							
							// modify order book, changing the values
							int quantity = 0;
							if(diff == 0){
								quantity = products_orders[i].o_info[l].quantity;
								products_orders[i].o_info[l].quantity = 0;
								products_orders[i].o_info[k].quantity = 0;
							}else if(diff > 0){
								quantity = products_orders[i].o_info[k].quantity;
								products_orders[i].o_info[l].quantity -= products_orders[i].o_info[k].quantity;
								products_orders[i].o_info[k].quantity = 0;
							}else{
								quantity = products_orders[i].o_info[l].quantity;
								products_orders[i].o_info[k].quantity -= products_orders[i].o_info[l].quantity;
								products_orders[i].o_info[l].quantity = 0;
							}
							
							// modify position
							int sell_trader = products_orders[i].o_info[l].trader;
							int buy_trader = products_orders[i].o_info[k].trader;
							for(int p = 0; p < trader_num; p ++){
								// newer sell trader, charge
								if(traders_possession[p].trader == sell_trader){
									for(int q = 0; q < numofProducts; q ++){
										if(strcmp(traders_possession[p].product_nums[q].product_name, products[i]) == 0){
											if(diff <= 0){
												// order all sold, 
												traders_possession[p].product_nums[q].num -= quantity;
											}else{
												// order partial sold
												traders_possession[p].product_nums[q].num -= quantity;
											}
											traders_possession[p].product_nums[q].money += amount;
											traders_possession[p].product_nums[q].money -= fee;
										}
									}	
								}
								// older buy trader
								if(traders_possession[p].trader == buy_trader){
									for(int q = 0; q < numofProducts; q ++){
										if(strcmp(traders_possession[p].product_nums[q].product_name, products[i]) == 0){
											if(diff <= 0){
												// order all bought
												traders_possession[p].product_nums[q].num += quantity;
											}else{
												// order partially bought
												traders_possession[p].product_nums[q].num += quantity;
											}
											traders_possession[p].product_nums[q].money -= amount;
										}
									}
								}
							}

							// call rearrange_orderbook(); which will delete the invalid orders and realloc memories
							rearrange_orderbook();
							// rescan
							k = 0;
						}
					}
				}
			}
		}
		// free
		free(prices);
		message_fill(fill_len, traders_fill, orders_fill, quantities_fill);
		free(traders_fill);
		free(orders_fill);
		free(quantities_fill);
	}
	print_order_book();
	print_position();
}

void rearrange_orderbook(){
	// find orders with quantity 0, delete that order and realloc memory
	for(int i = 0; i < numofProducts; i ++){
		for(int j = 0; j < products_orders[i].nOfOrder; j ++){
			if(products_orders[i].o_info[j].quantity == 0){
				for(int k = j; k < products_orders[i].nOfOrder - 1; k ++){
					products_orders[i].o_info[k] = products_orders[i].o_info[k+1];
				}
				products_orders[i].nOfOrder -= 1;
				order_info* o_infos = (order_info*)realloc(products_orders[i].o_info, products_orders[i].nOfOrder*sizeof(order_info));
				products_orders[i].o_info = o_infos;
			}
		}
	}
	// 
}

void add_trader_order(int trader){
	for(int i = 0; i < trader_num; i ++){
		if(i == trader){
			ts_os[i] += 1;
		}
	}
}

int check_next_order(int trader){
	for(int i = 0; i < trader_num; i ++){
		if(i == trader){
			return ts_os[i] + 1;
		}
	}
	return -1;
}
	
int main(int argc, char **argv) {
	// at least 2 traders, at most 9999 traders
	if (argc < 3 || argc > 10001) {
        printf("Incorrect number of arguments\n");
        return 1;
    }

	// register handler for sigusr1
	struct sigaction sa;
    sa.sa_handler = sigusr1_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if(sigaction(SIGUSR1, &sa, NULL) == -1){
        perror("Signal action error\n");
        exit(1);
    }

	// signal handler for child terminations
	struct sigaction sa_chld;
	sa_chld.sa_handler = sigchld_handler;
	sigemptyset(&sa_chld.sa_mask);
	sa_chld.sa_flags = SA_RESTART;
	if(sigaction(SIGCHLD, &sa_chld, NULL) == -1){
		perror("Signal action error\n");
		exit(1);
	}
	// sigset_t block_mask;
	// sigemptyset(&block_mask);
	// sigaddset(&block_mask, SIGUSR1);
	// // block the signals until setup is all done.
	// if (sigprocmask(SIG_BLOCK, &block_mask, NULL) == -1) {
	// 	printf("sigprocmask error");
	// 	exit(1);
	// }

	// first message
	printf("%s Starting\n", MSG);

	// read product files
	FILE* file = fopen("products.txt", "r");
	if (file == NULL){
		perror("Error opening file");
		return 1;
	}

	fscanf(file, "%d", &numofProducts);
	// dynamically allocate the products
	products = (char**)malloc(numofProducts * sizeof(char*));
	if (products == NULL){
		perror("Error allocating products\n");
		fclose(file);
		return 1;
	}
	fgetc(file);

	// reading file
	for(int i = 0; i < numofProducts; ++i){
		// read the line into a buffer
		char buffer[MAX_PRODUCT_LENGTH] = {0};
		if(fgets(buffer, sizeof(buffer), file) == NULL){
			perror("Error reading files\n");
			fclose(file);
			free(products);
			return 1;
		}
		buffer[strcspn(buffer, "\n")] = '\0';

		// dynamically allocate each product name, 17 characters maximum.
		products[i] = (char*)malloc(MAX_PRODUCT_LENGTH * sizeof(char)); // allocate memory for each element
		if (products[i] == NULL) {
			perror("Error allocating memory for product\n");
			fclose(file);
			// free
			for (int j = 0; j < i; ++j) {
				free(products[j]); // free previously allocated memory
			}
			free(products);
			return 1;
		}
		strcpy(products[i], buffer);
	}
	fclose(file);

	// printing message
	if (numofProducts == 1) {
		printf("%s Trading 1 products: %s\n", MSG, products[0]);
	} else {
		printf("%s Trading %d products: ", MSG, numofProducts);
		// free
		for (int i = 0; i < numofProducts; i++) {
			printf("%s", products[i]);
			if (i < numofProducts - 1) {
				printf(" ");
			}
		}
		printf("\n");
	}

	// generate all pipes and start all child processes
	// ./pe_exchange file_name ./trader_a ./trader_b .....
	// the number of traders is argc-2
	trader_num = argc - 2;
	
	// setting up orderbook infomation
	products_orders = (product_orders*)malloc(numofProducts*sizeof(product_orders));
	if(products_orders == NULL){
		perror("Allocating products_orders error\n");
		exit(1);
	}
	// initialize each product_info
	for(int i = 0; i < numofProducts; ++i){
		strcpy(products_orders[i].product_name, products[i]);
		products_orders[i].nOfOrder = 0;
		products_orders[i].o_info = NULL;
		//printf("%d is %s\n", i, products_orders[i].product_name);
	}

	// setting up position information
	traders_possession = (trader_possession*)malloc(trader_num*sizeof(trader_possession));
	if(traders_possession == NULL){
		perror("Allocating traders_possession error\n");
		exit(1);
	}
	for(int i = 0; i < trader_num; i ++){
		traders_possession[i].trader = i;
		traders_possession[i].product_nums = (product_num*)malloc(numofProducts*sizeof(product_num));
		if(traders_possession[i].product_nums == NULL){
			perror("Allocating product_nums error\n");
			exit(1);
		}
		for(int j = 0; j < numofProducts; j ++){
			traders_possession[i].product_nums[j].num = 0;
			traders_possession[i].product_nums[j].money = 0;
			strcpy(traders_possession[i].product_nums[j].product_name, products[j]);
		}
	}

	// setting up traders and its order id
	ts_os = (int*)malloc(trader_num*sizeof(int));
	for(int i = 0; i < trader_num; i ++){
		ts_os[i] = -1;
	}

	// the longest pipe file name is "/tmp/pe_exchange_9999", which is 22 bytes long.
	char pipename[MAX_PIPE_NAME_LENGTH];
	memset(pipename, 0, sizeof(pipename));
	// it is fine to use only one variable for all pipes, as long as it is reset to 0 every time after it is used.
	// allocate 2 variables to store the pipe names, one contains all pipes from parent to child, another from child to parent
	pipes_p_to_c = (char**)malloc(trader_num*sizeof(char*));
	if (pipes_p_to_c == NULL) {
        perror("malloc pipes p to c\n");
		// free
		// for (int j = 0; j < numofProducts; ++j) {
		// 	free(products[j]);
		// }
		// free(products);
		// free(trader_pids);
        return 1;
    }

	pipes_c_to_p = (char**)malloc(trader_num*sizeof(char*));
	if (pipes_c_to_p == NULL) {
        perror("error malloc pipes c to p\n");
		// free
		// for (int j = 0; j < numofProducts; ++j) {
		// 	free(products[j]);
		// }
		// free(products);
		// free(pipes_p_to_c);
        return 1;
    }

	// dynamically allocate the pids
	trader_pids = (pid_t*) malloc(sizeof(pid_t) * trader_num);
	if (trader_pids == NULL) {
		perror("malloc pids\n");
		return 1;
	}

	// make pipe files and start traders
	for(int i = 0; i < trader_num; ++i){
		// create the corresponding pipes for the child process first
		// pipe from parent to children
		sprintf(pipename, FIFO_EXCHANGE, i);
		mkfifo(pipename, 0666);
		// allocate the space for each pipe name
		pipes_p_to_c[i] = (char*) malloc(MAX_PIPE_NAME_LENGTH * sizeof(char));
		if(pipes_p_to_c[i] == NULL){
			printf("error malloc %d pipe p to c\n", i);
			exit(1);
		}
		// copy pipe name into the allocated space
		strcpy(pipes_p_to_c[i], pipename);
		// print message
		printf("%s Created FIFO %s\n", MSG, pipename);
		// reset pipename
		memset(pipename, 0, sizeof(pipename));

		// pipe from children to parent
		sprintf(pipename, FIFO_TRADER, i);
		mkfifo(pipename, 0666);
		// copy pipe name into the allocated space
		pipes_c_to_p[i] = (char*) malloc(MAX_PIPE_NAME_LENGTH * sizeof(char));
		if(pipes_c_to_p[i] == NULL){
			printf("error malloc %d pipe c to p\n", i);
			exit(1);
		}
		strcpy(pipes_c_to_p[i], pipename);
		// print message
		printf("%s Created FIFO %s\n", MSG, pipename);
		// reset pipename
		memset(pipename, 0, sizeof(pipename));

		//then fork and execute the traders
		trader_pids[i] = fork();
		if(trader_pids[i] == 0){
			// execute with the correct trader number, 12 is the maximum length of integer
			char trader_num_str[12] = {0};
            sprintf(trader_num_str, "%d", i);
			printf("%s Starting trader %d (%s)\n", MSG, i, argv[i+2]);
            execl(argv[i+2], argv[i+2], trader_num_str, NULL);
            perror("Error executing trader binary\n");
			exit(1);
		}else if (trader_pids[i] < 0){
			perror("error when forking\n");
			exit(1);
		}

		// continue parent process, connect to the two pipes
		// pipe from parent to child is used to write
		int writefd = open(pipes_p_to_c[i], O_WRONLY);
		if (writefd < 0){
			perror("open pipe p to c failed\n");
			exit(1);
		}
		printf("%s Connected to %s\n", MSG, pipes_p_to_c[i]);

		// pipe from child to parent is used to read
		int readfd = open(pipes_c_to_p[i], O_RDONLY);
		if(readfd < 0){
			perror("open pipe c to p failed\n");
			exit(1);
		}
		printf("%s Connected to %s\n", MSG, pipes_c_to_p[i]);


        ssize_t wr = write(writefd, "MARKET OPEN;", strlen("MARKET OPEN;"));
        if (wr < 0){
            perror("failed to write to pipe\n");
			exit(1);
        }
		//printf("message: %s is written to pipe: %s\n", message, pipes_p_to_c[i]);
		// send signal to the child process
        if(kill(trader_pids[i], SIGUSR1) == -1){
            perror("Send signal to child failed\n");
			exit(1);
        }
	}
	// unblock signals
	// if (sigprocmask(SIG_UNBLOCK, &block_mask, NULL) == -1) {
	// 	printf("sigprocmask error\n");
	// 	free_all();
	// 	exit(1);
	// }

	while(1){
		// if no signal
		while(flag == 0){
			pause();
		}

		// if signal sent from child
		// reset flag
		flag = 0;

		// a loop used to read all pipes and find the message
		// number of traders disconnected
		for(int i = 0; i < trader_num; ++i){
			// pipe from child to parent is used to read
			int readfd = open(pipes_c_to_p[i], O_RDONLY);
			if(readfd < 0){
				// if open the pipe fails, then it means the pipe has been closed, 
				// not because there is an error, because the opening of pipes was already checked 
				continue;
			}

			char msg_from_c[MAX_MSG_FROM_C_LEN] = {0};
			read(readfd, msg_from_c, sizeof(msg_from_c));

			// Find the ; character and replace it with \0
    		char *semicolon_pos = strchr(msg_from_c, ';');
    		if (semicolon_pos != NULL) {
    		    *semicolon_pos = '\0';
   			}

			if (strncmp(msg_from_c, "BUY", strlen("BUY")) == 0){
				// print handling message
				printf("%s [T%d] Parsing command: <%s>\n", MSG, i, msg_from_c);
				// getting information
				int o_id = -1;
				int amt = -1;
				int prc = -1;
				char pname[17] = {0};
				sscanf(msg_from_c, "BUY %d %s %d %d;", &o_id, pname, &amt, &prc);
				
				// check if the product name is valid
				int in = 0;
				for(int j = 0; j < numofProducts; ++j){
					if(strcmp(pname, products[j]) == 0){
						in = 1;
						break;
					}
				}
				if ((!in) || (pname[0] == '\0')|| (amt <= 0) || (amt > 999999) || (prc <=0) || (prc > 999999) || (o_id != check_next_order(i))){
					// struct timespec ts = {.tv_sec = 0, .tv_nsec = 500000000};
					// nanosleep(&ts, NULL);
					message_child("INVALID;", trader_pids[i]);
					break;
				}
				// modify products_orders
				add_order_book(pname, i, o_id, prc, amt, "BUY");
				add_trader_order(i);
				
				char m_to_c[MAX_TO_C_LEN] = {0};
				// send message to all child
				char* message_format = "MARKET BUY %s %d %d;";
				sprintf(m_to_c, message_format, pname, amt, prc);
				message_children_except(m_to_c, trader_pids[i]);
				
				if(can_match()){
					// send acceptance to this child
					sprintf(m_to_c, "ACCEPTED %d;", o_id);
					message_child(m_to_c, trader_pids[i]);
					match_orders();
				}else{
					print_order_book();
					print_position();
					sprintf(m_to_c, "ACCEPTED %d;", o_id);
					message_child(m_to_c, trader_pids[i]);
				}
			}else if (strncmp(msg_from_c, "SELL", strlen("SELL")) == 0){
				printf("%s [T%d] Parsing command: <%s>\n", MSG, i, msg_from_c);
				// getting information
				int o_id = -1;
				int amt = -1;
				int prc = -1;
				char pname[17] = {0};
				sscanf(msg_from_c, "SELL %d %s %d %d;", &o_id, pname, &amt, &prc);
				
				// check if the product name is valid
				int in = 0;
				for(int j = 0; j < numofProducts; ++j){
					if(strcmp(pname, products[j]) == 0){
						in = 1;
						break;
					}
				}

				if ((!in) || (pname[0] == '\0') ||(amt <= 0) || (amt > 999999) ||(prc <=0) || (prc > 999999) || (o_id != check_next_order(i))){
					message_child("INVALID;", trader_pids[i]);
					break;
				}

				// modify products_orders
				add_order_book(pname, i, o_id, prc, amt, "SELL");
				add_trader_order(i);

				
				char m_to_c[MAX_TO_C_LEN] = {0};
				// send message to all child
				sprintf(m_to_c, "MARKET SELL %s %d %d;", pname, amt, prc);
				message_children_except(m_to_c, trader_pids[i]);

				if(can_match()){
					// send acceptance to this child
					sprintf(m_to_c, "ACCEPTED %d;", o_id);
					message_child(m_to_c, trader_pids[i]);
					match_orders();
				}else{
					print_order_book();
					print_position();
					sprintf(m_to_c, "ACCEPTED %d;", o_id);
					message_child(m_to_c, trader_pids[i]);
				}
			}else if (strncmp(msg_from_c, "AMEND", strlen("AMEND")) == 0){
				printf("%s [T%d] Parsing command: <%s>\n", MSG, i, msg_from_c);
				// getting information
				int o_id = -1;
				int amt = -1;
				int prc = -1;
				sscanf(msg_from_c, "AMEND %d %d %d;", &o_id, &amt, &prc);
				
				// modify products_orders
				char str[16 + 1 + 4 + 1] = {0};
				modify_order_book(i, o_id, prc, amt, str);
				char strc[16+1+4+1] = {0};
				strcpy(strc, str);
				char* pname = strtok(strc, " ");

				char *space_pos = strchr(str, ' ');
				char* type = space_pos + 1;

				// check validity
				if ((str[0] == '\0')|| (o_id < 0) || (amt <= 0) || (amt > 999999) ||(prc <=0) || (prc > 999999)){
					message_child("INVALID;", trader_pids[i]);
					break;
				}
				// send acceptance to this child
				char m_to_c[MAX_TO_C_LEN] = {0};
				// send message to all child
				sprintf(m_to_c, "MARKET %s %s %d %d;", type, pname, amt, prc);
				message_children_except(m_to_c, trader_pids[i]);

				if(can_match()){
					// send acceptance to this child
					sprintf(m_to_c, "AMENDED %d;", o_id);
					message_child(m_to_c, trader_pids[i]);
					rearrange_orderbook();
					match_orders();
				}else{
					print_order_book();
					print_position();
					sprintf(m_to_c, "AMENDED %d;", o_id);
					message_child(m_to_c, trader_pids[i]);
				}
			}else if (strncmp(msg_from_c, "CANCEL", strlen("CANCEL")) == 0){
				printf("%s [T%d] Parsing command: <%s>\n", MSG, i, msg_from_c);
				// getting information
				int o_id = -1;
				sscanf(msg_from_c, "CANCEL %d;", &o_id);
				
				// modify products_orders
				// product name + space + SELL + nul
				char str[16 + 1 + 4 + 1] = {0};
				modify_order_book(i, o_id, 0, 0, str);
				char strc[16+1+4+1] = {0};
				strcpy(strc, str);
				char* pname = strtok(strc, " ");

				char *space_pos = strchr(str, ' ');
				char* type = space_pos + 1;
				// check validity
				if ((str[0] == '\0')|| (o_id < 0)){
					message_child("INVALID;", trader_pids[i]);
					break;
				}

				rearrange_orderbook();
				char m_to_c[MAX_TO_C_LEN] = {0};
				// send message to all child
				sprintf(m_to_c, "MARKET %s %s %d %d;", type, pname, 0, 0);
				message_children_except(m_to_c, trader_pids[i]);
				
				print_order_book();
				print_position();
				// send acceptance to this child
				sprintf(m_to_c, "CANCELLED %d;", o_id);
				message_child(m_to_c, trader_pids[i]);
			}
			continue;
		}
	}

	// will never reach here
	free_all();
	return 0;
}