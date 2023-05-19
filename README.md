1. Describe how your exchange works.
At the beginning, the exchange checks the command line arguements numbers and sets up the required variables.

The setting up includes (the printed stdout message is not mentioned here):

a. Register SIGUSR1 signal handler to handle the signals sent from child processes.

b. Register SIGCHLD signal handler to handle the termination of the child processes. If all child processes have terminated then the exchange prints the required message and terminates normally with code 0.

c. Reading the products.txt file and store the information to a string array using malloc.
d. The number of traders is the number of command line arguments - 2.

e. Initialization of the products_orders variable using malloc. The products_orders variable is an array of product_orders structure, which contains a string, an integer and an array of order_info structure, where the string is one of the products, integer indicates the number of orders of this product and the order_info array stores all orders of this product.

f. Similar initialization for traders_possession structure. The traders_possession is an array of trader_possession, which contains an integer and an array of product_num structure. The integer indicates the trader number of that trader (0, 1, 2...) and the products_num array has the same size of the products initialized in step c. Together traders_possession stores the traders and the information related to each product (product name, owned number of products, and its value in total).

g. ts_os indicates traders and orders, this ingeter array records the current latest order id each trader has, the index corresponds with the order of traders (the 0th element indicates order id of trader 0). Starting with -1, which means no orders. Initialized with malloc

h. Initializing the two sets of of pipe file names using malloc. One for pipes from children to exchange and another the opposite way. The index corresponds with the order of traders.

i. Initializing the array of pids using malloc. This array will be required to send signals to the child/children.

j. Use a for loop to start every trader, and store the corresponded pipe file names, generate the temporary pipe files. Each string in the two sets were also allocated dynamically. Also execute the child process, then try opening the corresponding pipes to make sure correctness, finally write the MARKET OPEN; message to the pipes and send signal to all child processes.

k. An infinity loop that pauses when no signal sent, and reads the pipes when signal received. Depending on the four types of message, BUY SELL AMEND CANCEL, do operations correspondingly. Error handling applied.

If exited anywhere apart from in the SIGCHLD handler, it is suggesting a mistake happened, so these exits are all with code 1.

Before exit normally, all allocated memories are freed, and because the corresponded pipes are unlinked every time the child process terminates, the files will be all unlinked before exiting normally.



2. Describe your design decisions for the trader and how it's fault-tolerant.
Every time a signal is sent, the trader sleeps for a few seconds. Because the traders are basically all "auto-trader"s, even if several traders communicate with each other, the time needed for sleep is known be the coder.

3. Describe your tests and how to run them.
Test case Errors 2 on ED fails for my exchange for unknown reason, so I am trying to write 2 traders that behave the same as the Errors 2 test case. 

The test can be run with bash script, first run the process and store the stdout to /tmp/test_output file

./pe_exchange products.txt ./trader_a ./trader_b > /tmp/test_output
Then loop through the test files (if I had many, probably won't have one)
for test_file in test_*.out; do
  expected_output=$(cat $test_file)
  actual_output=$(cat /tmp/test_output)
  if [ "$expected_output" != "$actual_output" ]; then
    echo "Test $test_file failed"
    echo "Expected:"
    echo "$expected_output"
    echo "Actual:"
    echo "$actual_output"
  fi
done

rm /tmp/test_output

The test should be run But it is now very near the deadline, If I did not change these sentences then I failed to complete that.