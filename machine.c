/*
	@author:		Ryan Ku
	@date:			09 March 2018
	@description:	This program serves as a machine on a network that is able to calculate a least cost path
					to all computers on its network using Dijkstra's link-state algorithm. When executed, this
					program takes in its machine ID, the number of machines on the same network, an initial
					cost table, and a host table for information on the IPs and port #s of the other machines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <pthread.h>
#include <ctype.h>

// GLOBAL VARIABLES
int i, out_socket_fd, in_socket_fd, **cost, *leastCostArray, myID, nodes; // socket file descriptor, neighbor cost table, least cost array, # of machines
int costChange[3];
int buff[3];
time_t t;
socklen_t serv_stor_size;
FILE *costfile, *hostfile;
struct sockaddr_in out_serv_addr, in_serv_addr;
struct sockaddr_storage serv_stor;
struct MACHINE *my_machines;
pthread_t thread1, thread3; // thread2 = main
pthread_mutex_t lock;

void readCostTable();
void editCostTable();
void printCostTable();
void printLeastCost();
void readHostTable();
void printHostTable();
void receiveCostChange();
void broadcastCostChange();
void link_state();

struct MACHINE
{
	char name[50];
	char ip[50];
	int port;
};

void readCostTable()
{
	int x, y;
	for(x = 0; x < nodes; x++) for(y = 0; y < nodes; y++) fscanf(costfile, "%d", &cost[x][y]);
}

void editCostTable(int machine1, int machine2, int newCost)
{
	cost[machine1][machine2] = newCost;
	cost[machine2][machine1] = newCost;
	printCostTable();
}

void printCostTable()
{
	printf("COST TABLE\n");
	int x, y;
	for(x = 0; x < nodes; x++)
	{
		for(y = 0; y < nodes; y++)
		{
			printf("%d\t", cost[x][y]);
		}
		printf("\n");
	}
	printf("\n");
}

void printLeastCost()
{
	printf("\rLeast Cost Array:       ");
	for(i = 0; i < nodes; i++)	printf("%d\t", leastCostArray[i]);
	printf("\n");
}

void readHostTable()
{
	for(i = 0; i < nodes; i++)
	{
		fscanf(hostfile, "%s%s%d",
				my_machines[i].name,
				my_machines[i].ip,
				&my_machines[i].port);
	}
}

void printHostTable()
{
	printf("HOST TABLE\n");
	for(i = 0; i < nodes; i++)
	{
		printf("%s\t%s\t%d\n",
				my_machines[i].name,
				my_machines[i].ip,
				my_machines[i].port);
	}
	printf("\n");
}

/***********************************************************
			   RECEIVE COST CHANGES: THREAD 1
 ***********************************************************/
void receiveCostChange()
{
	while(true)
	{
		if(recvfrom(in_socket_fd, &buff, sizeof(buff), 0, (struct sockaddr *)&serv_stor, &serv_stor_size) < 0)
		{
			perror("recvfrom() error");
			exit(EXIT_FAILURE);
		}
		printf("\rCost update received: Machine1: %d - Machine2: %d - New cost: %d\n", buff[0], buff[1], buff[2]);
		pthread_mutex_lock(&lock);
		editCostTable(buff[0], buff[1], buff[2]);
		pthread_mutex_unlock(&lock);
	}
}

void broadcastCostChange(int neighbor, int newCost)
{
	int bytes_sent;
	// out_serv_addr.sin_port = htons(port_number)
	for(i = 0; i < nodes; i++)
	{
		out_serv_addr.sin_port = htons(my_machines[i].port);
		if(inet_pton(AF_INET, my_machines[i].ip, &out_serv_addr.sin_addr.s_addr) == -1)
		{	// check inet_pton()
			perror("inet_ptons() error");
			exit(EXIT_FAILURE);
		}

		costChange[0] = myID;
		costChange[1] = neighbor;
		costChange[2] = newCost;

		bytes_sent = sendto(out_socket_fd, costChange, sizeof(costChange), 0, (struct sockaddr *)&out_serv_addr, sizeof(out_serv_addr));
		if(bytes_sent == -1)
		{
			perror("sendto() error");
			exit(EXIT_FAILURE);
		}
	}	
}

/***********************************************************
			   LINK STATE ALGORITHM: THREAD 1
 ***********************************************************/
void link_state()
{
	while(true)
	{
		// initialization
		int node, nodes_left = nodes, minNode, diffpath, remaining[nodes]; // remaining = remaining subset of Nodes to be tested in Dijkstra's algorithm
		bool exists = false;
		memset(leastCostArray, 10000, sizeof(leastCostArray));
		pthread_mutex_lock(&lock);
		for(i = 0; i < nodes; i++)	remaining[i] = i; // subset initially holds all machines in network
		for(node = 0; node < nodes; node++)
		{
			leastCostArray[node] = cost[myID][node];
		}

		// loop
		while(nodes_left > 0)
		{
			for(i = 0; i < nodes; i++)
			{	// find least cost neighbor in subset
				if(remaining[i] < 10000)
				{	// if node exists in the subset
					if(remaining[minNode] >= 10000)	minNode = i; // erases trace of old minNode
					if(leastCostArray[i] < leastCostArray[minNode])	minNode = i;
				}
			}
			remaining[minNode] = 10000; // remove least cost neighbor from subset
			nodes_left--;

			for(node = 0; node < nodes; node++)
			{	// test for neighbors of minNode - node = potential neighbor
				if(cost[minNode][node] >= 10000)	continue; // 10000 --> not neighbor --> do not test
				diffpath = leastCostArray[minNode] + cost[minNode][node];
				if(diffpath < leastCostArray[node])	leastCostArray[node] = diffpath; // if new path is shorter, set new path
			}
		}

		printLeastCost();
		pthread_mutex_unlock(&lock);
		sleep(10 + (rand() % 10)); // sleeps between 10 and 20 seconds
	}
}

// accepts router ID, # of nodes, cost table file, name + IP + port # file
int main(int argc, char* argv[])
{
	int neighbor, newCost, loopCount = 0, t2loopsleep = 10, t2postsleep = 30;
	srand((unsigned) time(&t));
	serv_stor_size = sizeof(serv_stor);
	pthread_mutex_init(&lock, NULL);


	/***********************************************************
					CHECK COMMAND LINE INPUTS
	 ***********************************************************/
	if(argc != 5)
	{	// check number of inputs
		printf("Error: Could not input files.\n");
		exit(EXIT_FAILURE);
	}
	if((argv[2] == NULL))
	{	// check # of nodes input
		printf("Error: number of nodes not present.\n");
		exit(EXIT_FAILURE);
	}	else if((nodes = atoi(argv[2])) < 1)
	{
		printf("Error: incorrect number of nodes.\n");
		exit(EXIT_FAILURE);
	}
	if((argv[1] == NULL))
	{	// check router ID input
		printf("Error: machine ID not present.\n");
		exit(EXIT_FAILURE);
	}	else if(((myID = atoi(argv[1])) < 0) || (myID >= nodes))
	{
		printf("Error: incorrect machine ID.\n");
		exit(EXIT_FAILURE);
	}
	if(argv[3] == NULL)
	{	// check cost table file name
		printf("Error: no cost table file.\n");
		exit(EXIT_FAILURE);
	}
	if(argv[4] == NULL)
	{	// check topology table file name
		printf("Error: no topology table.\n");
		exit(EXIT_FAILURE);
	}
	cost = malloc(sizeof(int) * nodes);
	if(cost == 0)
	{	// check cost malloc
		perror("malloc() cost error");
		exit(EXIT_FAILURE);
	}
	for(i = 0; i < nodes; i++)
	{	// malloc cost[]
		cost[i] = malloc(sizeof(int) * nodes);
		if(cost[i] == 0)
		{	// check cost[] malloc
			printf("malloc() cost[] error");
			exit(EXIT_FAILURE);
		}
	}
	leastCostArray = malloc(sizeof(int) * nodes);
	if(leastCostArray == 0)
	{	// check leastCostArray malloc
		perror("malloc() leastCostArray error");
		exit(EXIT_FAILURE);
	}
	my_machines = malloc(sizeof(struct MACHINE) * nodes);
	if(my_machines == 0)
	{	// check my_machines malloc
		perror("malloc() my_machines error");
		exit(EXIT_FAILURE);
	}


	/***********************************************************
							FILE PARSING
	 ***********************************************************/
	costfile = fopen(argv[3], "r");
	if(costfile == NULL)
	{
		perror("fopen() costfile error");
		exit(EXIT_FAILURE);
	}
	hostfile = fopen(argv[4], "r");
	if(hostfile == NULL)
	{
		perror("fopen() hostfile error");
		exit(EXIT_FAILURE);
	}
	// mutex lock not necessary because other threads have not been created yet
	readCostTable();
	readHostTable();
	fclose(costfile);
	fclose(hostfile);
	printf("NAME: %s - IP: %s - PORT: %d\n\n",
			my_machines[myID].name,
			my_machines[myID].ip,
			my_machines[myID].port);
	printCostTable();
	printHostTable();
	memset(costChange, '0', sizeof(costChange));


	/***********************************************************
				  OUT SERVER ADDRESS STRUCT SET UP
	 ***********************************************************/
	memset(&out_serv_addr, '0', sizeof(out_serv_addr));
	out_serv_addr.sin_family = AF_INET;
	out_serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	memset((char *) out_serv_addr.sin_zero, 0, sizeof(out_serv_addr.sin_zero));

	// create socket
	out_socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(out_socket_fd < 0)
	{	//	check out socket() error
		perror("socket() error");
		exit(EXIT_FAILURE);
	}


	/***********************************************************
				  IN SERVER ADDRESS STRUCT SET UP
	 ***********************************************************/
	memset(&in_serv_addr, '0', sizeof(in_serv_addr));
	in_serv_addr.sin_family = AF_INET;
	in_serv_addr.sin_port = htons(my_machines[myID].port);
	in_serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	memset((char *)in_serv_addr.sin_zero, 0, sizeof(in_serv_addr.sin_zero));

	// create socket
	in_socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(in_socket_fd < 0)
	{	// check in socket() error
		printf("Error: Could not create socket.");
		exit(EXIT_FAILURE);
	}
	if(bind(in_socket_fd, (struct sockaddr *)&in_serv_addr, sizeof(in_serv_addr)) != 0)
	{	// check bind() error
		printf("Error: Could not bind.\n");
		exit(EXIT_FAILURE);
	}

	// create other threads
	pthread_create(&thread1, NULL, (void *) receiveCostChange, NULL);
	pthread_create(&thread3, NULL, (void *) link_state, NULL);


	/***********************************************************
					READ COST CHANGES: THREAD 2
	 ***********************************************************/
	sleep(1);
	while(loopCount < 2) // only accept 2 changes
	{
		printf("Neighbor cost update: ");
		scanf("%d%d", &neighbor, &newCost);
		if((neighbor < 0) || (neighbor >= nodes))
		{	// check if neighbor exists in network
			printf("Neighbor does not exist. Please re-enter cost update.\n");
			continue;
		} else if(neighbor == myID)
		{	// check if neighbor isn't self
			printf("Neighbor entered is this machine. Please re-enter cost update.\n");
			continue;
		}
		printf("Cost table updated: Neighbor %d - New cost: %d\n", neighbor, newCost);
		pthread_mutex_lock(&lock);
		editCostTable(myID, neighbor, newCost);
		broadcastCostChange(neighbor, newCost);
		pthread_mutex_unlock(&lock);
		loopCount++;
		sleep(t2loopsleep);
	}
	
	printf("\rSystem shutting down in %d seconds.\n", t2postsleep);
	sleep(t2postsleep); // CHANGE TO 30 FOR DEMO
	// printf("Sleeping\n\n");
	// printf("\n");
	// for(i = 1; i < 3; i++)
	// {
	// 	sleep(1);
	// 	printf("\rSleeping: %d", i);
	// }

	pthread_mutex_destroy(&lock);
	printf("\rSystem shut down.\n");
	exit(EXIT_SUCCESS);
}