#include <mpi.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>

#define PROCESSES 12
#define PLAYERS 10

#define LENGTH 128
#define WIDTH 64
#define LENGTH_HALF 64
#define SHOOT_THRES 10

#define FP0 0
#define FP1 1

#define X 0
#define Y 1

#define SCORE 0
#define PASS 1

#define PLYR_MSG_SIZE 7
#define RANK 2
#define CHAL_SCR 3
#define SHOT_TYPE 4
#define SHOOT_SKILL 5
#define RND_NO 6

#define PLYR_INFO_SIZE 9
#define INIT_X 0
#define INIT_Y 1
#define END_X 2
#define END_Y 3
#define REACH_RND 4
#define WIN_RND 5
#define CHALLENGE 6
#define SHOOT_X 7
#define SHOOT_Y 8

void initInfo(int *);
int nearBall(int *, int *, int);
void runStrategy(int, int, int*, int*, int*, int);
int runTowardsBall(int, int, int, int, int*, int*, int);
void run(int, int, int*, int*, int);
void runOffenseDirection(int, int, int, int, int *, int *, int);
void runDefenseDirection(int, int, int, int, int *, int *, int);
void determineShot(int, int *, int *, int *);
float getShotProbability(int, int);
int inMyField(int *);
int fieldProcess(int *);
int isOffenseSide(int, int*, int);

long long wall_clock_time()
{
#ifdef __linux__
	struct timespec tp;
	clock_gettime(CLOCK_REALTIME, &tp);
	return (long long)(tp.tv_nsec + (long long)tp.tv_sec * 1000000000ll);
#else
#warning "Your timer resolution might be too low. Compile on Linux and link with librt"
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (long long)(tv.tv_usec * 1000 + (long long)tv.tv_sec * 1000000000ll);
#endif
}

int main(int argc, char *argv[]){
	int rank, numtasks;
	MPI_Init(&argc,&argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
	if(numtasks != PROCESSES){
		printf("warning: 12 processes are required. exiting..\n");
		MPI_Finalize();
		exit(0);
	}
	srand(time(NULL)+rank);
	
	MPI_Request reqs[PROCESSES];
	MPI_Status stat[PROCESSES];
	int target;
	target = rank < 7 ? 128 : 0;
	
	
	//field processes
	int ballCoords[2];
	ballCoords[0] = LENGTH_HALF;
	ballCoords[1] = 32;		
	int * playerMessage;
	if(rank == FP0 || rank == FP1)
		playerMessage = malloc(PROCESSES*PLYR_MSG_SIZE*sizeof(int));
	else
		playerMessage = malloc(PLYR_MSG_SIZE*sizeof(int));
	int allPlayerInfo[PROCESSES*PLYR_INFO_SIZE];
	int score[2] = {0};
	int ballChallengeInfo[7];
	MPI_Status recvMsgStatus[PLAYERS];
	MPI_Request sendBallCoordsReqs[PROCESSES];
	MPI_Request recvMsg[PLAYERS];
	
	//player processes
	MPI_Request messages[2];
	MPI_Request ballRequest[2];
	MPI_Status ballRequestStatus[2];
	int playerInfo [PLYR_INFO_SIZE];
	int speed = 5;
	int dribbling = 5;
	int shooting = 5;
	switch(rank){
		case 2:
			playerInfo[INIT_X] = playerInfo[END_X] = 21; 
			playerInfo[INIT_Y] = playerInfo[END_Y] = 48; 
			speed = 3; dribbling = 10; shooting = 2;
			break;
		case 3:
			playerInfo[INIT_X] = playerInfo[END_X] = 21; 
			playerInfo[INIT_Y] = playerInfo[END_Y] = 32; 
			speed = 3; dribbling = 8; shooting = 4;
			break;
		case 4:
			playerInfo[INIT_X] = playerInfo[END_X] = 21; 
			playerInfo[INIT_Y] = playerInfo[END_Y] = 16; 
			speed = 5; dribbling = 7; shooting = 3;
			break;
		case 5:
			playerInfo[INIT_X] = playerInfo[END_X] = 41; 
			playerInfo[INIT_Y] = playerInfo[END_Y] = 48; 
			speed = 10; dribbling = 3; shooting = 2;
			break;
		case 6:
			playerInfo[INIT_X] = playerInfo[END_X] = 41; 
			playerInfo[INIT_Y] = playerInfo[END_Y] = 16; 
			speed = 8; dribbling = 1; shooting = 6;
			break;
		case 7:
			playerInfo[INIT_X] = playerInfo[END_X] = 107; 
			playerInfo[INIT_Y] = playerInfo[END_Y] = 48; 
			speed = 7; dribbling = 2; shooting = 6;
			break;
		case 8:
			playerInfo[INIT_X] = playerInfo[END_X] = 107; 
			playerInfo[INIT_Y] = playerInfo[END_Y] = 32; 
			speed = 10; dribbling = 4; shooting = 1;
			break;
		case 9:
			playerInfo[INIT_X] = playerInfo[END_X] = 107; 
			playerInfo[INIT_Y] = playerInfo[END_Y] = 16; 
			speed = 3; dribbling = 10; shooting = 2;
			break;
		case 10:
			playerInfo[INIT_X] = playerInfo[END_X] = 87; 
			playerInfo[INIT_Y] = playerInfo[END_Y] = 48; 
			speed = 8; dribbling = 2; shooting = 5;
			break;
		case 11:
			playerInfo[INIT_X] = playerInfo[END_X] = 87; 
			playerInfo[INIT_Y] = playerInfo[END_Y] = 16; 
			speed = 5; dribbling = 2; shooting = 8;
			break;
	}
	playerInfo[REACH_RND] = playerInfo[WIN_RND] = 0;
	playerInfo[CHALLENGE] = playerInfo[SHOOT_X] = playerInfo[SHOOT_Y] = -1;
	
	//tags
	int ballTag = 1;
	int ballChallengeTag = 2;
	int infoTag = 4;
	int messageTag = 8;

	//set playerMessage for fp0 and fp1
	if(rank == FP0 || rank == FP1){
		int i;
		for(i = 2; i < PROCESSES; i++){
			*(playerMessage+((i*PLYR_MSG_SIZE)+RND_NO)) = 1;
		}
	}
	
	//ROUND
	int round;
	long before, after;
	if(rank == FP0)
		before = wall_clock_time();
	for(round = 0; round < 5400; round++){
		/****************************************************
		*****PLAYERS
		***************************************************/
		if(rank != FP0 && rank != FP1){
			initInfo(playerInfo);			//init all variables
			if(round >= 2700){
				target = rank < 7 ? 0 : 128;
			}
			
			//Get ball coordinates
			int ballSender;
			if(round != 0){
				MPI_Irecv(ballCoords, 2, MPI_INT, FP0, ballTag, MPI_COMM_WORLD, &ballRequest[0]);
				MPI_Irecv(ballCoords, 2, MPI_INT, FP1, MPI_ANY_TAG, MPI_COMM_WORLD, &ballRequest[1]);
				MPI_Waitany(2, ballRequest, &ballSender, ballRequestStatus);
			}
			
			//Apply run strategy
			runStrategy(rank, round, ballCoords, playerInfo, playerInfo+2, speed);
			
			//if reach ball send message with all fields set
			if(playerInfo[END_X] == ballCoords[X] && playerInfo[END_Y] == ballCoords[Y]){
				playerMessage[X] = playerInfo[END_X];
				playerMessage[Y] = playerInfo[END_Y];
				playerMessage[RANK] = rank;
				playerMessage[CHAL_SCR] = (rand() % 10 + 1) * dribbling;
				
				int dist = abs(playerInfo[END_X] - target) + abs(playerInfo[END_Y] - 32);
				//we determine what the shot type is (either we try to score, or we pass)
				if(getShotProbability(dist, shooting) > 0.6)
					playerMessage[SHOT_TYPE] = SCORE;
				else
					playerMessage[SHOT_TYPE] = PASS;
					
				playerMessage[SHOOT_SKILL] = shooting;
				playerMessage[RND_NO] = round;
				
				//send our message: 1 to our corresopnding field process, and 1 to the other field process as dummy
				int fp = fieldProcess(ballCoords);
				MPI_Isend(playerMessage, PLYR_MSG_SIZE, MPI_INT, fp, messageTag, MPI_COMM_WORLD, &messages[0]);
				playerMessage[X] = -1;	//set message as dummy
				if(fp == FP0)
					MPI_Isend(playerMessage, PLYR_MSG_SIZE, MPI_INT, FP1, messageTag, MPI_COMM_WORLD, &messages[1]);
				else
					MPI_Isend(playerMessage, PLYR_MSG_SIZE, MPI_INT, FP0, messageTag, MPI_COMM_WORLD, &messages[1]);
					
				//set my own player information
				playerInfo[REACH_RND] = 1;
				playerInfo[CHALLENGE] = playerMessage[CHAL_SCR];
			}
			else{		//i did not reach the ball this round
				//set information for sending
				playerMessage[X] = playerInfo[END_X];
				playerMessage[Y] = playerInfo[END_Y];
				playerMessage[RANK] = rank;
				playerMessage[CHAL_SCR] = -1;
				playerMessage[SHOT_TYPE] = -1;
				playerMessage[SHOOT_SKILL] = shooting;
				playerMessage[RND_NO] = round;

				//send our message: 1 to our corresopnding field process, and 1 to the other field process as dummy
				int fp = fieldProcess(ballCoords);
				MPI_Isend(playerMessage, PLYR_MSG_SIZE, MPI_INT, fp, messageTag, MPI_COMM_WORLD, &messages[0]);
				if(fp == FP0)
					MPI_Isend(playerMessage, PLYR_MSG_SIZE, MPI_INT, FP1, messageTag, MPI_COMM_WORLD, &messages[1]);
				else
					MPI_Isend(playerMessage, PLYR_MSG_SIZE, MPI_INT, FP0, messageTag, MPI_COMM_WORLD, &messages[1]);
			}
			//round finish, send all info for printing
			MPI_Isend(playerInfo, PLYR_INFO_SIZE, MPI_INT, FP0, infoTag, MPI_COMM_WORLD, &reqs[rank]);
		}/************************************END OF PLAYER PROCESS***************************************/
		else{	
			/****************************************************************************
			****FIELD PROCESSES
			****************************************************************************/
			int i;
			if(rank == FP0){
				printf("%d\n%d %d\n%d %d\n", round, score[0], score[1], ballCoords[X], ballCoords[Y]);
			}

			//send ball coordinates
			if(round != 0){
				if(rank == FP0){
					for(i = 2; i < PROCESSES; i++)
							MPI_Isend(ballCoords, 2, MPI_INT, i, ballTag, MPI_COMM_WORLD, &sendBallCoordsReqs[rank]);
				}
			}
			
			//recv all player messages
			for(i = 2; i < PROCESSES; i++)
				MPI_Irecv(playerMessage+(i*PLYR_MSG_SIZE), PLYR_MSG_SIZE, MPI_INT, i, messageTag, MPI_COMM_WORLD, &recvMsg[i-2]);
				
			//if i'm the field process with the ball, go on to handle ball challenge
			if(rank == fieldProcess(ballCoords)){	
				int drawBuf[PROCESSES];
				int drawCount = 0;
				int winBallRank = -1;
				int maxBallChallenge = -1;
			
				MPI_Waitall(10, recvMsg, recvMsgStatus);	//wait for all messages to be received
				
				//get max ball challenges, or find out if any draw in ball challenges
				for(i = 2; i < PROCESSES; i++){
					int * currPlayer = playerMessage+(i*PLYR_MSG_SIZE);
					if(*currPlayer == -1)
						continue;
					if(*(currPlayer+CHAL_SCR) == -1){
					}
					else{
						if(*(currPlayer+CHAL_SCR) > maxBallChallenge){
							drawCount = 0;
							maxBallChallenge = *(currPlayer+CHAL_SCR);
							winBallRank = i;
						}
						else if(*(currPlayer+CHAL_SCR) == maxBallChallenge)
							drawBuf[drawCount++] = i;
					}
				}
				
				if(drawCount > 1)		//if there's a draw, we pick 1 winner randomly
						winBallRank = drawBuf[rand() % drawCount];
			
				if(winBallRank > 0){		//the winner of the ball challenge (-1 if no challenges)
					int * winBallPlayer = playerMessage+(winBallRank*PLYR_MSG_SIZE);
					int shotLocation[2] = {128, 32};
					int points = 0;
					//set player's scoring grid
					if(round < 2700){
						if(winBallRank >= 7)
							shotLocation[X] = 0;
					}
					else{
						if(winBallRank < 7)
							shotLocation[X] = 0;
					}
					int distToGoal = abs(*(winBallPlayer+X) - shotLocation[X]) + abs(*(winBallPlayer+Y) - shotLocation[Y]);
					
					//if the ball is already at the scoring grid, we simply add 2 points
					if(*(winBallPlayer+X) == shotLocation[X] && *(winBallPlayer+Y) == shotLocation[Y])
						points = 2;
					else{
						if(*(winBallPlayer+SHOT_TYPE) == SCORE){		//if the player wishes to score
							determineShot(*(winBallPlayer+SHOOT_SKILL), winBallPlayer, shotLocation, ballCoords);						
						}
						else{		//player wishes to pass
							//we find teammate closest to score grid
							int * targetTeammate = winBallPlayer;
							int * currPlayer;
							
							if(winBallRank < 7){
								for(i = 2; i < 7; i++){
									currPlayer = playerMessage+(i*PLYR_MSG_SIZE);
									if(*(currPlayer+RND_NO) == round && abs(*(currPlayer+X)-shotLocation[X])+abs(*(currPlayer+Y)-shotLocation[Y]) < distToGoal){
										distToGoal = abs(*(currPlayer+X)-shotLocation[X])+abs(*(currPlayer+Y)-shotLocation[Y]);
										targetTeammate = currPlayer;
									}
								}
								if(targetTeammate != winBallPlayer){
									shotLocation[X] = *(targetTeammate+X);
									shotLocation[Y] = *(targetTeammate+Y);
								}
							}
							else{
								for(i = 7; i < 12; i++){
									currPlayer = playerMessage+(i*PLYR_MSG_SIZE);
									if(*(currPlayer+RND_NO) == round && abs(*(currPlayer+X)-shotLocation[X])+abs(*(currPlayer+Y)-shotLocation[Y]) < distToGoal){
										distToGoal = abs(*(currPlayer+X)-shotLocation[X])+abs(*(currPlayer+Y)-shotLocation[Y]);
										targetTeammate = currPlayer;
									}
								}
								if(targetTeammate != winBallPlayer){
									shotLocation[X] = *(targetTeammate+X);
									shotLocation[Y] = *(targetTeammate+Y);
								}
							}
							
							if(targetTeammate == winBallPlayer)	//already the player with ball is already nearest to score grid, he tries his luck
								determineShot(*(winBallPlayer+SHOOT_SKILL), winBallPlayer, shotLocation, ballCoords);			
							else{
								determineShot(*(currPlayer+SHOOT_SKILL), currPlayer, shotLocation, ballCoords);			
							}
						}
			
						//determine score
						if(ballCoords[Y] == 32 && (ballCoords[X] == 128 || ballCoords[X] == 0)){
							points = distToGoal < 24 ? 2 : 3;
							ballCoords[X] = 64;	//start from center after scoring
							ballCoords[Y] = 32;
						}
					}
					
					//message to sync information between field processes
					ballChallengeInfo[0] = winBallRank;
					ballChallengeInfo[1] = shotLocation[0];
					ballChallengeInfo[2] = shotLocation[1];
					ballChallengeInfo[3] = ballCoords[0];
					ballChallengeInfo[4] = ballCoords[1];
					ballChallengeInfo[5] = points;
					ballChallengeInfo[6] = winBallRank < 7 ? 0 : 1;
				}
				else{	//no ball challenges
					ballChallengeInfo[0] = winBallRank;
					ballChallengeInfo[1] = -1;
					ballChallengeInfo[2] = -1;
					ballChallengeInfo[3] = ballCoords[0];
					ballChallengeInfo[4] = ballCoords[1];
					ballChallengeInfo[5] = 0;
					ballChallengeInfo[6] = -1;
				}			
				
				//field process sends information about ball challenge 
				if(rank == FP1)
					MPI_Isend(ballChallengeInfo, 7, MPI_INT, 0, ballChallengeTag, MPI_COMM_WORLD, &reqs[rank]);
				else
					MPI_Isend(ballChallengeInfo, 7, MPI_INT, 1, ballChallengeTag, MPI_COMM_WORLD, &reqs[rank]);
			}
			else{	//i'm the field process without the ball, i simply wait
				if(rank == FP0){
					MPI_Irecv(ballChallengeInfo, 7, MPI_INT, 1, ballChallengeTag, MPI_COMM_WORLD, &reqs[rank]);
					MPI_Wait(&reqs[rank], &stat[rank]);	//wait for ball challenge info from fp1
					ballCoords[X] = ballChallengeInfo[3];
					ballCoords[Y] = ballChallengeInfo[4];
				}
				else{
					MPI_Irecv(ballChallengeInfo, 7, MPI_INT, 0, ballChallengeTag, MPI_COMM_WORLD, &reqs[rank]);
					MPI_Wait(&reqs[rank], &stat[rank]);	//wait for ball challenge info from fp1
					ballCoords[X] = ballChallengeInfo[3];
					ballCoords[Y] = ballChallengeInfo[4];
				}
			}
			
			//for round printing
			if(rank == FP0){
				//get all player info for printing
				int i;
				for(i = 2; i < PROCESSES; i++)
					MPI_Irecv(allPlayerInfo+i*9, 9, MPI_INT, i, infoTag, MPI_COMM_WORLD, &reqs[i]);
				MPI_Waitall(PLAYERS, &reqs[2], &stat[2]);			
				
				//set the newly sync information
				score[ballChallengeInfo[6]] += ballChallengeInfo[5];
				*(allPlayerInfo+(ballChallengeInfo[0]*9 + SHOOT_X)) = ballChallengeInfo[1];
				*(allPlayerInfo+(ballChallengeInfo[0]*9 + SHOOT_Y)) = ballChallengeInfo[2];
				*(allPlayerInfo+(ballChallengeInfo[0]*9 + WIN_RND)) = 1;
				ballCoords[X] = ballChallengeInfo[3];
				ballCoords[Y] = ballChallengeInfo[4];
				
				int * currPlayer = allPlayerInfo + 18;

				for(i = 2; i < 7; i++){
					printf("%d %d %d %d %d %d %d %d %d %d\n", i-2, *(currPlayer+INIT_X), *(currPlayer+INIT_Y), *(currPlayer+END_X),
						*(currPlayer+END_Y), *(currPlayer+REACH_RND), *(currPlayer+WIN_RND), *(currPlayer+CHALLENGE), *(currPlayer+SHOOT_X), *(currPlayer+SHOOT_Y));
					currPlayer += 9;
				}
				for(; i < PROCESSES; i++){
					printf("%d %d %d %d %d %d %d %d %d %d\n", i-7, *(currPlayer+INIT_X), *(currPlayer+INIT_Y), *(currPlayer+END_X), *
						(currPlayer+END_Y), *(currPlayer+REACH_RND), *(currPlayer+WIN_RND), *(currPlayer+CHALLENGE), *(currPlayer+SHOOT_X), *(currPlayer+SHOOT_Y));
					currPlayer += 9;
				}
			}
		}
	}
	if(rank == FP0){
		after = wall_clock_time();
		//printf("%1.5f sec\n", (float)(after-before)/1000000000);
	}
	MPI_Finalize();
	
	free(playerMessage);
	return 0;
}

//function to reset round variables of a player
void initInfo(int * info){
	*(info+INIT_X) = *(info+END_X);
	*(info+INIT_Y) = *(info+END_Y);
	*(info+REACH_RND) = *(info+WIN_RND) = 0;
	*(info+CHALLENGE) = *(info+SHOOT_X) = *(info+SHOOT_Y) = -1;
}

void runStrategy(int rank, int round, int * ballCoords, int * startPos, int * endPos, int speed){
	if(rank == 5 || rank == 6 || rank == 10 || rank == 11){		//designated ball chasers
		runTowardsBall(*ballCoords, *(ballCoords+1), *startPos, *(startPos+1), endPos, endPos+1, speed);
	}
	else{
		if(nearBall(startPos, ballCoords, speed)){
			runTowardsBall(*ballCoords, *(ballCoords+1), *startPos, *(startPos+1), endPos, endPos+1, speed);
		}
		else{
			if(isOffenseSide(rank, ballCoords, round)){	//if ball is in my attacking half, run towards my attacking half
				runOffenseDirection(rank, round, *(startPos), *(startPos+1), endPos, endPos+1, speed);
			}
			else{ //else run towards my defensive half
				runDefenseDirection(rank, round, *(startPos), *(startPos+1), endPos, endPos+1, speed);
			}
		}
	}
}

//function checks if player is near to ball
int nearBall(int * location, int * ballLocation, int dist){
	int totalDist = abs(*location - *ballLocation) + abs(*(location+1) - *(ballLocation+1));
	if(totalDist < 5 * dist)
		return 1;
	else
		return 0;
}

//functions return true if ball is in player's offensive side, and false otherwise
int isOffenseSide(int rank, int * ball, int round){
	if(round < 2700){
		if(rank < 7)	//attack right side
			return *ball <= 64 ? 0 : 1;
		else		//attack left side
			return *ball > 64 ? 0 : 1;
	}
	else{
		if(rank < 7)
			return *ball <= 64 ? 1 : 0;
		else
			return *ball > 64 ? 1 : 0;
	}
}

void runOffenseDirection(int rank, int round, int currX, int currY, int *resultX, int * resultY, int distance){
	//if already in offense zone, stay
	if(round < 2700){
		if(rank < 7){	//attack right side
			if(currX < 108 || abs(currY - 32) > 16)
				run(128,32,resultX,resultY,distance);
		}
		else{	//attack left side
			if(currX > 20 || abs(currY - 32) > 16)
				run(0,32,resultX,resultY,distance);
		}
	}
	else{
		if(rank < 7){	//attack left side
			if(currX > 20 || abs(currY - 32) > 16)
				run(0,32,resultX,resultY,distance);
		}
		else{
			if(currX < 108 || abs(currY -32) > 16)
				run(128,32,resultX,resultY,distance);
		}
	}
}

void runDefenseDirection(int rank, int round, int currX, int currY, int *resultX, int * resultY, int distance){
	//if already in defense zone, stay
	if(round < 2700){
		if(rank < 7){	//attack right side
			if(currX > 20 || abs(currY - 32) > 16){
				run(0,32,resultX,resultY,distance);
			}
		}
		else{	//attack left side
			if(currX < 108 || abs(currY - 32) > 16)
				run(128,32,resultX,resultY,distance);
		}
	}
	else{
		if(rank < 7){	//attack left side
			if(currX < 108 || abs(currY -32) > 16)
				run(128,32,resultX,resultY,distance);
		}
		else{
			if(currX > 20 || abs(currY - 32) > 16)
				run(0,32,resultX,resultY,distance);		
		}
	}
}

int inMyField(int * location){
	return *location <= 64 ? FP0 : FP1;
}

void determineShot(int shootSkill, int * ballCoords, int * shotCoords, int * output){
	//get shot probability
	int distance = abs(*ballCoords-*shotCoords) + abs((*ballCoords+1)-(*shotCoords+1));
	float probability = getShotProbability(distance, shootSkill);
	//get rand() if good shot, set output and return
	float shotProb = (float)rand()/(float)RAND_MAX;
	if(shotProb <= probability){
		*output = *shotCoords;
		*(output+1) = *(shotCoords+1);
	}
	else{
		int ranLocation = (rand() % 8) + 1;
		int minus = rand() % 2;
		*output = *shotCoords + (ranLocation/2 * (minus==0?-1:1));
		minus = rand() % 2;
		*(output+1) = *(shotCoords+1) + (ranLocation/2 * (minus==0?-1:1));
		
		//check for bounds
		if(*output > 128){
			*output = 108;
		} else if(*output < 0){
			*output = 20;
		}
		
		if(*(output+1) > 64){
			*(output+1) = 64;
		} else if(*(output+1) < 0){
			*(output+1) = 0;
		}
	}
}

float getShotProbability(int d, int s){
	float ratio = (10.0+90.0*s)/(0.5*d*pow(d,0.5)-0.5);
	return ratio < 100 ? ratio/100.0 : 1;
}

int runTowardsBall(int ballX, int ballY, int currX, int currY, int *resultX, int *resultY, int distance){
	int offsetX = ballX - currX;
	int offsetY = ballY - currY;
	int distFromBall = abs(offsetX) + abs(offsetY);
	
	if(distance >= distFromBall){
		*resultX = ballX;
		*resultY = ballY;
		return distFromBall;
	}
	else{
		run(ballX, ballY, resultX, resultY, distance);
		return distance;
	}	
}

//Precondition: distance will not reach destX and destY
void run(int destX, int destY, int *currX, int *currY, int distance){
	if(distance == 0)
		return;	
	else if(*currX == destX){
		*currY = (*currY > destY) ? *currY - 1 : *currY + 1;
		run(destX, destY, currX, currY, distance-1);
	}
	else if(*currY == destY){
		*currX = (*currX > destX) ? *currX - 1 : *currX + 1;
		run(destX, destY, currX, currY, distance-1);
	}
	else{
		if(distance == 1){
			*currX = (*currX > destX) ? *currX - 1 : *currX + 1;
			return;
		}
		*currX = (*currX > destX) ? *currX - 1 : *currX + 1;
		*currY = (*currY > destY) ? *currY - 1 : *currY + 1;
		run(destX, destY, currX, currY, distance-2);
	}
}

int fieldProcess(int * coords){
	return (*coords <= 64) ? FP0 : FP1;
}

