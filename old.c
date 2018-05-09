#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>

#define	QLEN           5
#define	BUFSIZE     4096
#define MAXCLIENTS  1010
#define MAXNAMELEN   100
#define MAXQUESNUM   128
#define MAXQUESLEN  2048
#define MAXKEYLEN     10
#define MAXRESLEN   4096

char* testFile;

int clientsCnt=0;
int completeClientsCnt=0;
sem_t mutexClientsCnt;
pthread_mutex_t mutexCompleteClientsCnt = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t full  = PTHREAD_COND_INITIALIZER;

char lname[MAXNAMELEN];
int lnum;

char fastest[MAXNAMELEN];
sem_t mutexFast;

int answered[MAXQUESNUM];
pthread_cond_t allAnswered  = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutexAnswered = PTHREAD_MUTEX_INITIALIZER;

int scoreEntered=0;
pthread_cond_t allEntered  = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutexEntered = PTHREAD_MUTEX_INITIALIZER;

char res[BUFSIZE];
sem_t mutexRes;

_Bool quizStarted=0;
_Bool fastestFound[MAXQUESNUM];

int passivesock( char *service, char *protocol, int qlen, int *rport );

/*
    Thread
*/
void* acceptClient(void *ssockcpy){

    _Bool isAdmin=0;
    int ssock=*((int*)ssockcpy);

    sem_wait(&mutexClientsCnt);

    if (clientsCnt==0){
        isAdmin=1;
        lnum=0;
    } else if (quizStarted || clientsCnt>=lnum) {
        sem_post(&mutexClientsCnt);
        write( ssock, "QS|FULL\r\n", strlen("QS|FULL\r\n") );
        pthread_exit( NULL );
    }
    clientsCnt++;

    sem_post(&mutexClientsCnt);

    char buf[BUFSIZE];
    char cname[MAXNAMELEN];
    int cc;
    int points=0;

	fflush( stdout );

    char *token;
    char *rest = buf;
    int tokenCnt=0;
    char *tokens[3];


    if (isAdmin){

        if ( write( ssock, "QS|ADMIN\r\n", strlen("QS|ADMIN\r\n") ) < 0 ) {
        	/* This guy is dead */
        	printf("quit1\n");
        	close( ssock );
        	sem_wait(&mutexClientsCnt);
            clientsCnt--;
            sem_post(&mutexClientsCnt);
            pthread_exit( NULL );
        }

        if ( (cc = read( ssock, buf, BUFSIZE )) <= 0 ){
        	printf("quit2\n");
        	close(ssock);
        	sem_wait(&mutexClientsCnt);
            clientsCnt--;
            sem_post(&mutexClientsCnt);
            pthread_exit( NULL );
        }

        buf[cc-1]='\0';

        /* getting tokens.
           https://www.geeksforgeeks.org/strtok-strtok_r-functions-c-examples/
        */

        while (tokenCnt<3 && (token = strtok_r(rest, "|", &rest))){
            tokens[tokenCnt++]=token;
        }

        strcpy(cname, tokens[1]);
        strcpy(lname, tokens[1]);
        lnum=atoi(tokens[2]);

        if ( write( ssock, "WAIT\r\n", strlen("WAIT\r\n") ) < 0 ) {
           	/* This guy is dead */
           	printf("quit3\n");
           	close( ssock );
           	sem_wait(&mutexClientsCnt);
            clientsCnt--;
            sem_post(&mutexClientsCnt);
            pthread_exit( NULL );
        }

        /*
        initializing the variables
        */
        sem_init(&mutexFast, 0, 1);
        sem_init(&mutexRes, 0, 1);
        quizStarted=0;

        strcpy(res, "RESULT");
        int j;
        for (j=0; j<MAXQUESNUM; j++){
            fastestFound[j]=0;
            answered[j]=0;
        }

        completeClientsCnt=0;
        scoreEntered=0;

    } else {

        if ( write( ssock, "QS|JOIN\r\n", strlen("QS|JOIN\r\n")) < 0 ){
            /* This guy is dead */
            printf("quit4\n");
            close( ssock );
            sem_wait(&mutexClientsCnt);
            clientsCnt--;
            sem_post(&mutexClientsCnt);
            pthread_exit( NULL );
        }

        if ( (cc = read( ssock, buf, BUFSIZE )) <= 0 ){
            printf("quit5\n");
            close(ssock);
            sem_wait(&mutexClientsCnt);
            clientsCnt--;
            sem_post(&mutexClientsCnt);
            pthread_exit( NULL );
        }

        buf[cc-1]='\0';

        while (tokenCnt<2 && (token = strtok_r(rest, "|", &rest))){
            tokens[tokenCnt++]=token;
        }

        strcpy(cname, tokens[1]);

        if ( write( ssock, "WAIT\r\n", strlen("WAIT\r\n")) < 0 ){
            /* This guy is dead */
            printf("quit6\n");
            close( ssock );
            sem_wait(&mutexClientsCnt);
            clientsCnt--;
            sem_post(&mutexClientsCnt);
            pthread_exit( NULL );
        }
    }

    printf("%s has joined\n", cname);

    /*
    waiting for the last user to join
    */
    pthread_mutex_lock(&mutexCompleteClientsCnt);
    completeClientsCnt++;
    if (completeClientsCnt==lnum){
        quizStarted=1;
        pthread_cond_broadcast(&full);
    }
    pthread_mutex_unlock(&mutexCompleteClientsCnt);

    printf("%s is waiting\n", cname);

    pthread_mutex_lock(&mutexCompleteClientsCnt);
    while(completeClientsCnt!=lnum){
        pthread_cond_wait(&full, &mutexCompleteClientsCnt);
    }
    pthread_cond_broadcast(&full);
    pthread_mutex_unlock(&mutexCompleteClientsCnt);


    printf("%s started\n", cname);

    FILE *fp = fopen(testFile , "r");

    if(fp == NULL) {
       perror("Error opening file");
       printf("quit7\n");
       close( ssock );
       sem_wait(&mutexClientsCnt);
       clientsCnt--;
       sem_post(&mutexClientsCnt);
       pthread_exit( NULL );
    }

    char qtn[MAXQUESLEN];
    char ans[MAXNAMELEN];
    char ques[MAXQUESLEN+50];

    int quesId=0;

    while (fgets(qtn, MAXQUESLEN, fp)!=NULL) {

        while(1){
            fgets(ans, MAXQUESLEN, fp);
            if (strcmp(ans, "\n")==0){
                break;
            }
            strcat(qtn, ans);
        }

        strcpy(ques, "QUES|");

        // converting the length of the question text (int) to string
        // assume that the length > 0
        int intSize=strlen(qtn);
        int strSizeLen=0;
        while(intSize>0){
            strSizeLen++;
            intSize/=10;
        }
        char strSize[strSizeLen+1];
        strSize[strSizeLen]='\0';
        intSize=strlen(qtn);
        int charInd=strSizeLen-1;
        while(intSize>0){
            strSize[charInd]=(char)((intSize%10)+'0');
            charInd--;
            intSize/=10;
        }

        strcat(ques, strSize);
        strcat(ques, "|");
        strcat(ques, qtn);
        strcat(ques, "\r\n");

        if ( write( ssock, ques, strlen(ques)) < 0 ){
            /* This guy is dead */
            printf("quit8\n");
            close( ssock );
            sem_wait(&mutexClientsCnt);
            clientsCnt--;
            sem_post(&mutexClientsCnt);

            pthread_mutex_lock(&mutexAnswered);
            if (answered[quesId]==clientsCnt){
                pthread_cond_broadcast(&allAnswered);
            }
            pthread_mutex_unlock(&mutexAnswered);

            pthread_exit( NULL );
        }
        char ansKey[MAXKEYLEN];
        char temp[3];
        fgets(ansKey, MAXKEYLEN, fp);
        fgets(temp, 3, fp);
        ansKey[strlen(ansKey)-1]='\0';

        if ( (cc = read( ssock, buf, BUFSIZE )) <= 0 ){
            printf("quit9\n");
            close( ssock );
            sem_wait(&mutexClientsCnt);
            clientsCnt--;
            sem_post(&mutexClientsCnt);
            pthread_mutex_lock(&mutexAnswered);
            if (answered[quesId]==clientsCnt){
                pthread_cond_broadcast(&allAnswered);
            }
            pthread_mutex_unlock(&mutexAnswered);

            pthread_exit( NULL );
        }
        buf[cc-1]='\0';

        char *userAns=&buf[4];

        if (strcmp(userAns, ansKey)==0){
            sem_wait(&mutexFast);
            if (!fastestFound[quesId]){
                points+=2;
                strcpy(fastest, cname);
                fastestFound[quesId]=1;
            } else {
                points+=1;
            }
            sem_post(&mutexFast);
        } else if (strcmp(userAns, "NOANS")!=0) {
            points-=1;
        }

        // waiting the last user to answer the question
        pthread_mutex_lock(&mutexAnswered);
        answered[quesId]++;
        if (answered[quesId]==clientsCnt){
            pthread_cond_broadcast(&allAnswered);
        }
        pthread_mutex_unlock(&mutexAnswered);

        pthread_mutex_lock(&mutexAnswered);
        if(answered[quesId]!=clientsCnt){
            pthread_cond_wait(&allAnswered, &mutexAnswered);
        }
        pthread_cond_broadcast(&allAnswered);
        pthread_mutex_unlock(&mutexAnswered);


        char mes[MAXRESLEN]="WIN|";
        if (fastestFound[quesId]){
            strcat(mes, fastest);
        } else {
            strcat(mes, "_no_winner_");
        }
        strcat(mes, "\r\n");

        if ( write( ssock, mes, strlen(mes))<0 ){
            printf("quit10\n");
            close( ssock );
            sem_wait(&mutexClientsCnt);
            clientsCnt--;
            sem_post(&mutexClientsCnt);
            pthread_exit( NULL );
        }
        quesId++;
    }

    sem_wait(&mutexRes);
    strcat(res, "|");
    strcat(res, cname);
    strcat(res, "|");

    // converting the score (int) to string
    int intScore=points;
    int strScoreLen=0;
    if (points<0){
        intScore=-points;
        strcat(res, "-");
    }

    if (intScore==0){
        strScoreLen=1;
    }

    while(intScore>0){
        strScoreLen++;
        intScore/=10;
    }

    char strScore[strScoreLen+1];
    strScore[strScoreLen]='\0';
    intScore=points;
    if (points<0){
        intScore=-points;
    }
    int charScoreInd=strScoreLen-1;
    if (intScore==0){
        strScore[0]='0';
    }
    while(intScore>0){
        strScore[charScoreInd]=(char)((intScore%10)+'0');
        charScoreInd--;
        intScore/=10;
    }
    strcat(res, strScore);
    sem_post(&mutexRes);

    // waiting the last user to enter its result into the global variable
    pthread_mutex_lock(&mutexEntered);
    scoreEntered++;
    if (scoreEntered==clientsCnt){
        pthread_cond_broadcast(&allEntered);
    }
    pthread_mutex_unlock(&mutexEntered);

    pthread_mutex_lock(&mutexEntered);
    if(scoreEntered!=clientsCnt){
        pthread_cond_wait(&allEntered, &mutexEntered);
    }
    pthread_cond_broadcast(&allEntered);
    pthread_mutex_unlock(&mutexEntered);


    write( ssock, res, strlen(res));

    printf("%s ends.\n", cname);
    close( ssock );
    sem_wait(&mutexClientsCnt);
    clientsCnt--;
    sem_post(&mutexClientsCnt);
    pthread_exit( NULL );
}

//main
int main( int argc, char *argv[] ) {
	char			*service;
	struct sockaddr_in	fsin;
	int			alen;
	int			msock;
	int			ssock;
	int			rport = 0;
    sem_init( &mutexClientsCnt, 0, 1 );

	switch (argc) 
	{
		case	2:
			// No args? let the OS choose a port and tell the user
			rport = 1;
			testFile = argv[1];
			break;
		case	3:
			// User provides a port? then use it
			service = argv[1];
			testFile = argv[2];
			break;
		default:
			fprintf( stderr, "usage: server [port](optional) [test_file]\n" );
			exit(-1);
	}

	msock = passivesock( service, "tcp", QLEN, &rport );
	if (rport)
	{
		//	Tell the user the selected port
		printf( "server: port %d\n", rport );	
		fflush( stdout );
	}

	pthread_t threads[1028];
    int j=0;

	while(1)
	{
		int	ssock, status;

		alen = sizeof(fsin);
		ssock = accept( msock, (struct sockaddr *)&fsin, &alen );
		if (ssock < 0)
		{
			fprintf( stderr, "accept: %s\n", strerror(errno) );
			exit(-1);
		}

		int ssockcpy=ssock;

		status = pthread_create( &threads[j++], NULL, acceptClient, (void*) &ssockcpy );
        j%=1028;
	}

    int i;

	for ( i = 0; i < j; i++ )
    	pthread_join( threads[i], NULL );
}