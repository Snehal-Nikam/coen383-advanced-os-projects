#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<time.h>
#include<string.h>
#include<malloc/malloc.h>
#include<pthread.h>
#include"utility.h"
#define hp_seller_count 1
#define mp_seller_count 3
#define lp_seller_count 6
#define total_seller_count (hp_seller_count + mp_seller_count + lp_seller_count)
#define concert_row 10
#define concert_col 10
#define simulation_duration 60

typedef struct sell_args {
	char seller_no;
	char seller_type;
	queue *seller_queue;
} sell_arg;

typedef struct customer_struct {
	char cust_no;
	int arrival_t;
} customer;

//Global Variable
int sim_time;
int N = 5;
int at1[15] = {0}, st1[15] = {0}, tat1[15] = {0}, bt1[15]={0}, rt1[15]={0};
float throughput[3] = {0};

float avg_rt=0, avg_tat=0, num_cust_served = 0;
char seat_matrix[concert_row][concert_col][5];	//4 to hold L002\0

//Thread Variable
pthread_t seller_t[total_seller_count];
pthread_mutex_t thread_count_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t thread_waiting_for_clock_tick_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t reservation_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t thread_completion_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t condition_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  condition_cond  = PTHREAD_COND_INITIALIZER;

//Function Defination
void display_queue(queue *q);
void create_seller_threads(pthread_t *thread, char seller_type, int no_of_sellers);
void wait_for_thread_to_serve_current_time_slice();
void wakeup_all_seller_threads();
void *sell(void *);
queue * gen_customer_q(int);
int compare_by_arrival_time(void * data1, void * data2);
int findAvailableSeat(char seller_type);

int thread_count = 0;
int threads_waiting_for_clock_tick = 0;
int active_thread = 0;
int verbose = 0;
int main(int argc, char** argv) {
    srand(4388);

	if(argc == 2) {
		N = atoi(argv[1]);
	}

	//Initialize Global Variables
	for(int r=0; r<concert_row; r++) {
		for(int c=0; c<concert_col; c++) {
			strncpy(seat_matrix[r][c],"-",1);
		}
	}

	//Create all threads
	create_seller_threads(seller_t, 'H', hp_seller_count);
	create_seller_threads(seller_t + hp_seller_count, 'M', mp_seller_count);
	create_seller_threads(seller_t + hp_seller_count + mp_seller_count, 'L', lp_seller_count);

	//Wait for threads to finish initialization and wait for synchronized clock tick
	while(1) {
		pthread_mutex_lock(&thread_count_mutex);
		if(thread_count == 0) {
			pthread_mutex_unlock(&thread_count_mutex);
			break;
		}
		pthread_mutex_unlock(&thread_count_mutex);
	}

	printf("Starting Simulation\n");
	threads_waiting_for_clock_tick = 0;
	wakeup_all_seller_threads(); //For first tick
	
	do {

		wait_for_thread_to_serve_current_time_slice();
		sim_time = sim_time + 1;
		wakeup_all_seller_threads();
	} while(sim_time < simulation_duration);

	wakeup_all_seller_threads();

	while(active_thread);

	//Display concert chart
	printf("\n\n");
	printf("Final Concert Seat Chart\n");
	printf("========================\n");

	int h_customers = 0,m_customers = 0,l_customers = 0;
	for(int r=0;r<concert_row;r++) {
		for(int c=0;c<concert_col;c++) {
			if(c!=0)
				printf("\t");
			printf("%5s",seat_matrix[r][c]);
			if(seat_matrix[r][c][0]=='H') h_customers++;
			if(seat_matrix[r][c][0]=='M') m_customers++;
			if(seat_matrix[r][c][0]=='L') l_customers++;
		}
		printf("\n");
	}

	printf("\n\nStat for N = %02d\n",N);
	printf("===============\n");
	printf(" ============================================\n");
	printf("|%3c | No of Customers | Got Seat | Returned |\n",' ');
	printf(" ============================================\n");
	printf("|%3c | %15d | %8d | %8d |\n",'H',hp_seller_count*N,h_customers,(hp_seller_count*N)-h_customers);
	printf("|%3c | %15d | %8d | %8d |\n",'M',mp_seller_count*N,m_customers,(mp_seller_count*N)-m_customers);
	printf("|%3c | %15d | %8d | %8d |\n",'L',lp_seller_count*N,l_customers,(lp_seller_count*N)-l_customers);
	printf(" ============================================\n");

    for(int z1=0; z1<N; z1++){
        int ct = 0;
        ct = st1[z1] + bt1[z1];
        rt1[z1] = abs(st1[z1]-at1[z1]);
        tat1[z1] = abs(ct - at1[z1]);
    }

    for(int j1=0; j1<N; j1++){
        avg_tat += tat1[j1];
        avg_rt += rt1[j1];
    }

    printf("\nAverage TAT is %.2f\n", avg_tat/N);
    printf("Average RT is %.2f\n", avg_rt/N);
    printf("Throughput of seller H is %.2f\n", throughput[0]/60.0);
    printf("Throughput of seller M is %.2f\n", throughput[1]/60.0);
    printf("Throughput of seller L is %.2f\n", throughput[2]/60.0);

	return 0;
}

void create_seller_threads(pthread_t *thread, char seller_type, int no_of_sellers){
	//Create all threads
	for(int t_no = 0; t_no < no_of_sellers; t_no++) {
		sell_arg *seller_arg = (sell_arg *) malloc(sizeof(sell_arg));
		seller_arg->seller_no = t_no;
		seller_arg->seller_type = seller_type;
		seller_arg->seller_queue = gen_customer_q(N);

		pthread_mutex_lock(&thread_count_mutex);
		thread_count++;
		pthread_mutex_unlock(&thread_count_mutex);
		if(verbose)
			printf("Creating thread %c%02d\n",seller_type,t_no);
		pthread_create(thread+t_no, NULL, &sell, seller_arg);
	}
}

void display_queue(queue *q) {
	for(node *ptr = q->head;ptr!=NULL;ptr=ptr->next) {
		customer *cust = (customer * )ptr->data;
		printf("[%d,%d]",cust->cust_no,cust->arrival_t);
	}
}

void wait_for_thread_to_serve_current_time_slice(){
	//Check if all threads has finished their jobs for this time slice
	while(1){
		pthread_mutex_lock(&thread_waiting_for_clock_tick_mutex);
		if(threads_waiting_for_clock_tick == active_thread) {
			threads_waiting_for_clock_tick = 0;	
			pthread_mutex_unlock(&thread_waiting_for_clock_tick_mutex);
			break;
		}
		pthread_mutex_unlock(&thread_waiting_for_clock_tick_mutex);
	}
}
void wakeup_all_seller_threads() {

	pthread_mutex_lock( &condition_mutex );
	if(verbose)
		printf("00:%02d Main Thread Broadcasting Clock Tick\n",sim_time);
	pthread_cond_broadcast( &condition_cond);
	pthread_mutex_unlock( &condition_mutex );
}

void *sell(void *t_args) {
	//Initializing thread
	sell_arg *args = (sell_arg *) t_args;
	queue * cust_q = args->seller_queue;
	queue * seller_queue = create_queue();
	char seller_type = args->seller_type;
	int seller_no = args->seller_no + 1;
	
	pthread_mutex_lock(&thread_count_mutex);
	thread_count--;
	active_thread++;
	pthread_mutex_unlock(&thread_count_mutex);

	customer *cust = NULL;
	int random_wait_time = 0;
    int temp1 = 0;
	

	while(sim_time < simulation_duration) {
		//Waiting for clock tick
		pthread_mutex_lock(&condition_mutex);
		if(verbose)
			printf("00:%02d %c%02d Waiting for next clock tick\n",sim_time,seller_type,seller_no);
		
		pthread_mutex_lock(&thread_waiting_for_clock_tick_mutex);
		threads_waiting_for_clock_tick++;
		pthread_mutex_unlock(&thread_waiting_for_clock_tick_mutex);
		
		pthread_cond_wait( &condition_cond, &condition_mutex);
		if(verbose)
			printf("00:%02d %c%02d Received Clock Tick\n",sim_time,seller_type,seller_no);
		pthread_mutex_unlock( &condition_mutex );

		// Sell
		if(sim_time == simulation_duration) break;
		//All New Customer Came
		while(cust_q->size > 0 && ((customer *)cust_q->head->data)->arrival_t <= sim_time) {
			customer *temp = (customer *) dequeue (cust_q);
			enqueue(seller_queue,temp);
			printf("00:%02d %c%d Customer No %c%d%02d arrived\n",sim_time,seller_type,seller_no,seller_type,seller_no,temp->cust_no);
		}
		//Serve next customer
		if(cust == NULL && seller_queue->size>0) {
			cust = (customer *) dequeue(seller_queue);
			printf("00:%02d %c%d Serving Customer No %c%d%02d\n",sim_time,seller_type,seller_no,seller_type,seller_no,cust->cust_no);
			switch(seller_type) {
				case 'H':
				random_wait_time = (rand()%2) + 1;
                bt1[temp1] = random_wait_time;
                temp1++;
				break;
				case 'M':
				random_wait_time = (rand()%3) + 2;
                bt1[temp1] = random_wait_time;
                temp1++;
				break;
				case 'L':
				random_wait_time = (rand()%4) + 4;
                bt1[temp1] = random_wait_time;
                temp1++;
			}
		}
		if(cust != NULL) {
			//printf("Wait time %d\n",random_wait_time);
			if(random_wait_time == 0) {
				//Selling Seat
				pthread_mutex_lock(&reservation_mutex);

				// Find seat
				int seatIndex = findAvailableSeat(seller_type);
				if(seatIndex == -1) {
					printf("00:%02d %c%d Customer No %c%d%02d has been told Concert Sold Out.\n",sim_time,seller_type,seller_no,seller_type,seller_no,cust->cust_no);
				} else {
					int row_no = seatIndex/concert_col;
					int col_no = seatIndex%concert_col;
					sprintf(seat_matrix[row_no][col_no],"%c%d%02d",seller_type,seller_no,cust->cust_no);
					printf("00:%02d %c%d Customer No %c%d%02d assigned seat %d,%d \n",sim_time,seller_type,seller_no,seller_type,seller_no,cust->cust_no,row_no,col_no);
                    num_cust_served++;
                    if (seller_type == 'L')
                        throughput[0]++;
                    else if (seller_type=='M')
                        throughput[1]++;
                    else if (seller_type == 'H')
                        throughput[2]++;
				}
				pthread_mutex_unlock(&reservation_mutex);
				cust = NULL;
			} else {
				random_wait_time--;
			}
		} else {
			//printf("00:%02d %c%02d Waiting for customer\n",sim_time,seller_type,seller_no);
		}
	}

	while(cust!=NULL || seller_queue->size > 0) {
		if(cust==NULL)
			cust = (customer *) dequeue(seller_queue);
		printf("00:%02d %c%d Ticket Sale Closed. Customer No %c%d%02d Leaves\n",sim_time,seller_type,seller_no,seller_type,seller_no,cust->cust_no);
		cust = NULL;
	}

	pthread_mutex_lock(&thread_count_mutex);
	active_thread--;
	pthread_mutex_unlock(&thread_count_mutex);
}

int findAvailableSeat(char seller_type){
	int seatIndex = -1;

	if(seller_type == 'H') {
		for(int row_no = 0;row_no < concert_row; row_no ++ ){
			for(int col_no = 0;col_no < concert_col; col_no ++) {
				if(strcmp(seat_matrix[row_no][col_no],"-") == 0) {
					seatIndex = row_no * concert_col + col_no;
					return seatIndex;
				}
			}
		}
	} else if(seller_type == 'M') {
		int mid = concert_row / 2;
		int row_jump = 0;
		int next_row_no = mid;
		for(row_jump = 0;;row_jump++) {
			int row_no = mid+row_jump;
			if(mid + row_jump < concert_row) {
				for(int col_no = 0;col_no < concert_col; col_no ++) {
					if(strcmp(seat_matrix[row_no][col_no],"-") == 0) {
						seatIndex = row_no * concert_col + col_no;
						return seatIndex;
					}
				}
			}
			row_no = mid - row_jump;
			if(mid - row_jump >= 0) {
				for(int col_no = 0;col_no < concert_col; col_no ++) {
					if(strcmp(seat_matrix[row_no][col_no],"-") == 0) {
						seatIndex = row_no * concert_col + col_no;
						return seatIndex;
					}
				}
			}
			if(mid + row_jump >= concert_row && mid - row_jump < 0) {
				break;
			}
		}
	} else if(seller_type == 'L') {
		for(int row_no = concert_row - 1;row_no >= 0; row_no -- ){
			for(int col_no = concert_col - 1;col_no >= 0; col_no --) {
				if(strcmp(seat_matrix[row_no][col_no],"-") == 0) {
					seatIndex = row_no * concert_col + col_no;
					return seatIndex;
				}
			}
		}
	}

	return -1;
}

queue * gen_customer_q(int N){
	queue * cust_q = create_queue();
	char cust_no = 0;
	while(N--) {
		customer *cust = (customer *) malloc(sizeof(customer));
		cust->cust_no = cust_no;
		cust->arrival_t = rand() % simulation_duration;
        at1[cust_no] = cust->arrival_t;
		enqueue(cust_q,cust);
		cust_no++;
	}
	sort(cust_q, compare_by_arrival_time);
	node * ptr = cust_q->head;
	cust_no = 0;
	while(ptr!=NULL) {
		cust_no ++;
		customer *cust = (customer *) ptr->data;
		cust->cust_no = cust_no;
		ptr = ptr->next;
	}
	return cust_q;
}

int compare_by_arrival_time(void * data1, void * data2) {
	customer *c1 = (customer *)data1;
	customer *c2 = (customer *)data2;
	if(c1->arrival_t < c2->arrival_t) {
		return -1;
	} else if(c1->arrival_t == c2->arrival_t){
		return 0;
	} else {
		return 1;
	}
}
