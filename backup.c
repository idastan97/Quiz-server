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
#include <sys/select.h>

/*
    defines
    !!! do not forget to add 1 for null character
*/
#define QLEN           5
#define BUFSIZE     4096
#define MAXSIZELEN     6

#define MAXGROUPNAMELEN 100
#define MAXGROUPTOPICLEN 100
#define MAXQUESNUM     128

#define MAXQUESLEN   250
#define MAXANSLEN     10

#define MAXCNAMELEN 100



/*
    score record structure
*/
typedef struct {
    int score;
    char *name;
} scoreRec;

/*
    structs
*/
typedef struct _nodeChar{
    char val;
    struct _nodeChar *next;
} nodeChar;

typedef struct _question {
    char ques[MAXQUESLEN];
    char ans[MAXANSLEN];
    struct _question *next;
    sem_t mutexWinner;
    _Bool winnerFound;
    char winner[MAXCNAMELEN];
} question;

typedef struct _group {
    _Bool isCanceled;

    char name[MAXGROUPNAMELEN];
    char topic[MAXGROUPTOPICLEN];
    int size;
    question *quizText;
    int quesNum;
    _Bool isActive;
    struct _group *next;

    fd_set participants;
    int maxFd;
    int maxSock;
    sem_t mutexParticipants;

    int curSize;
    sem_t mutexCurSize;

    _Bool isStarted;
    sem_t mutexIsStarted;

    pthread_mutex_t mutexIsFull;
    pthread_cond_t full;

    int *answered;
    sem_t mutexAnswered;
    sem_t mutexSendWinner;

    pthread_mutex_t mutexNextQues;
    pthread_cond_t nextQues;

    scoreRec *scores;
    sem_t mutexScores;
    int scoresInd;

    sem_t mutexSendRes;

    pthread_mutex_t mutexGetRes;
    pthread_cond_t getRes;

} group;


typedef struct{
    group *myGroup;
    int ssock;
    pthread_t myPthreadId;
    int quesId;
} clientLeaveThreadArgument;


/*
    global variables
*/
// fd_set freeClients; // set of sockets that's client did not joine any group (free)
// int maxFreeSock=0;

group *firstGroup;
sem_t mutexGroupsList;


int passivesock( char *service, char *protocol, int qlen, int *rport );

/*
    function to convert integer to string
*/
char* intToString(int x){
    char *res = (char*)malloc(MAXSIZELEN*sizeof(char));
    if (x==0){
        res[0]='0';
        res[1]='\0';
        return res;
    }

    int xCopy, fromInd;
    if (x>0){
        xCopy = x;
        fromInd=0;
    } else {
        xCopy = -x;
        res[0]='-';
        fromInd=1;
    }

    int len = 0, xCopyCopy = xCopy;
    while (xCopyCopy>0){
        xCopyCopy/=10;
        len++;
    }

    res[fromInd+len]='\0';

    while (len>0){
        res[fromInd+len-1]=(xCopy%10)+'0';
        xCopy/=10;
        len--;
    }

    return res;
}

/*
    function to add group to linked list
*/
_Bool addGroup(group *newGroup){
    sem_wait(&mutexGroupsList);
    if (firstGroup==NULL){
        firstGroup=newGroup;
        sem_post(&mutexGroupsList);
        return 1;
    }
    group *temp=firstGroup;
    while(temp->next!=NULL){
        if (strcmp(temp->name, newGroup->name)==0){
            sem_post(&mutexGroupsList);
            return 0;
        }
        temp=temp->next;
    }
    if (strcmp(temp->name, newGroup->name)==0){
        sem_post(&mutexGroupsList);
        return 0;
    }
    temp->next=newGroup;
    sem_post(&mutexGroupsList);
    return 1;

}

/*
    function to remove group from the list and free al the memory
*/
void removeGroup(char *name){
    sem_wait(&mutexGroupsList);
    if (firstGroup==NULL){
        sem_post(&mutexGroupsList);
        return;
    }

    group *temp = firstGroup;
    if (strcmp(firstGroup->name, name)==0){
        firstGroup=firstGroup->next;
        free(temp);
        sem_post(&mutexGroupsList);
        return;
    }
    
    while(temp->next!=NULL){
        if (strcmp(temp->next->name, name)==0){
            group *t = temp->next;
            temp->next = temp->next->next;
            free(t);
            sem_post(&mutexGroupsList);
            return;
        }
    }
    sem_post(&mutexGroupsList);

    // TODO !!! free questions
}

/*
    function to find group by name. Returnes pointer to that group or NULL if not found
*/
group* findGroup(char *name){
    sem_wait(&mutexGroupsList);
    group *temp = firstGroup;

    while(temp!=NULL){
        if (strcmp(temp->name, name)==0){
            sem_post(&mutexGroupsList);
            return temp;
        }
        temp=temp->next;
    }
    sem_post(&mutexGroupsList);
    return NULL;
}


/*
    function to send groupslist
*/
void sendOpenGroups( int ssock ){

    group *temp=firstGroup;

    if ( write( ssock, "OPENGROUPS", strlen("OPENGROUPS") ) < 0 ) {
        
        close( ssock );
        pthread_exit( NULL );
    }

    while (temp!=NULL){
        if (temp->isActive){
            if ( write( ssock, "|", strlen("|") ) < 0 ) {
                
                close( ssock );
                pthread_exit( NULL );
            }
            if ( write( ssock, temp->topic, strlen(temp->topic) ) < 0 ) {
                
                close( ssock );
                pthread_exit( NULL );
            }

            if ( write( ssock, "|", strlen("|") ) < 0 ) {
                
                close( ssock );
                pthread_exit( NULL );
            }
            if ( write( ssock, temp->name, strlen(temp->name) ) < 0 ) {
                
                close( ssock );
                pthread_exit( NULL );
            }

            if ( write( ssock, "|", strlen("|") ) < 0 ) {
                
                close( ssock );
                pthread_exit( NULL );
            }
            char *stringSize = intToString(temp->size);
            if ( write( ssock, stringSize, strlen(stringSize) ) < 0 ) {
                
                close( ssock );
                pthread_exit( NULL );
            }
            free(stringSize);

            if ( write( ssock, "|", strlen("|") ) < 0 ) {
                
                close( ssock );
                pthread_exit( NULL );
            }
            stringSize = intToString(temp->curSize);
            if ( write( ssock, stringSize, strlen(stringSize) ) < 0 ) {
                
                close( ssock );
                pthread_exit( NULL );
            }
            free(stringSize);
        }
        temp=temp->next;
    }

    if ( write( ssock, "\r\n", strlen("\r\n") ) < 0 ) {
        
        close( ssock );
        pthread_exit( NULL );
    }
}

void addQuesToList(question **firstQues, question **lastQues, question *newQues){
    if (*firstQues==NULL){
        *firstQues=newQues;
        *lastQues=newQues;
        return;
    }
    (*lastQues)->next=newQues;
    (*lastQues)=newQues;
}

_Bool parseQuiz(group *gr, nodeChar *firstChar){
    
    // TODO !!! error handling

    int quesCnt = 0;

    question *firstQues = NULL;
    question *lastQues = NULL;
    
    while(firstChar!=NULL){

        question *tempQues = (question *)malloc(sizeof(question));
        tempQues->next=NULL;
        tempQues->winnerFound=0;
        sem_init(&(tempQues->mutexWinner), 0, 1);

        int quesI = 0;
        nodeChar *temp;
        char prevChar = ' ';

        while( !(firstChar->val=='\n' && prevChar=='\n') ){
            tempQues->ques[quesI] = firstChar->val;
            prevChar = firstChar->val;
            quesI++;
            temp = firstChar;
            firstChar = firstChar->next;
            free(temp);
        }
        tempQues->ques[quesI]='\0';
        //printf("%s\n", tempQues->ques);

        temp = firstChar;
        firstChar = firstChar->next;
        free(temp);
        prevChar = ' ';
        int ansI = 0;

        while( !(firstChar->val=='\n' && prevChar=='\n')  ){
            tempQues->ans[ansI] = firstChar->val;
            prevChar = firstChar->val;
            ansI++;
            temp = firstChar;
            firstChar = firstChar->next;
            free(temp);
        }
        tempQues->ans[ansI-1]='\0';
        //printf("%s\n", tempQues->ans);

        addQuesToList(&firstQues, &lastQues, tempQues);
        quesCnt++;

        temp = firstChar;
        firstChar = firstChar->next;
        free(temp);

    }

    //printf("grr\n");

    gr->quizText=firstQues;
    gr->isActive=1;
    gr->quesNum=quesCnt;
    gr->answered = (int *)malloc(quesCnt*sizeof(int));
    int i;
    for (i=0; i<quesCnt; i++){
        gr->answered[i]=0;
    }

    return 1;

}

void addCharToList(nodeChar **firstChar, nodeChar **lastChar, char newChar){
    nodeChar *newNodeChar = (nodeChar *)malloc(sizeof(nodeChar));
    newNodeChar->val = newChar;
    newNodeChar->next = NULL;
    if (*firstChar == NULL){
        *firstChar = newNodeChar;
        *lastChar = newNodeChar;
        return;
    }
    (*lastChar)->next=newNodeChar;
    (*lastChar)=newNodeChar;
}


/*
    function to sort the result
*/
void sortRes(scoreRec *arr, int size){
    // TODO
}

/*
    Group thread
    uncomment and implement
*/
void* groupThread(void *argPtr){
    group **myPtr = (group **)argPtr;
    group *newGroup = *((group **)argPtr);

    pthread_mutex_lock(&(newGroup->mutexIsFull));
    if (!(newGroup->isStarted)){
        pthread_cond_wait(&(newGroup->full), &(newGroup->mutexIsFull));
    }
    pthread_cond_broadcast(&(newGroup->full));
    pthread_mutex_unlock(&(newGroup->mutexIsFull));

    printf("cannnn\n");

    int fd;

    if (!(newGroup->isCanceled)){
        question *curQues = newGroup->quizText;
        int quesId = 0;
        char *win = (char*)malloc(sizeof(char)*(MAXCNAMELEN+5));
        
        while (curQues!=NULL){
            if ( select(newGroup->maxSock, NULL, &(newGroup->participants), NULL, NULL)<0 ){
                fprintf( stderr, " select: %s\n", strerror(errno) );
                removeGroup(newGroup->name);
                *myPtr=NULL;
                return NULL;
            }
            
            char quesToSend[MAXQUESLEN]="QUES|";
            char *quesLen = intToString(strlen(curQues->ques));
            strcat(quesToSend, quesLen);
            free(quesLen);
            strcat(quesToSend, "|");
            strcat(quesToSend, curQues->ques);
            for (fd=0; fd<(newGroup->maxSock); fd++){
                if (FD_ISSET(fd, &(newGroup->participants))){
                    if ( write(fd, quesToSend, strlen(quesToSend) )<0 ){
                        printf("ERROROROROROROR\n");
                    }
                }
            }

            printf(".%s.\n", curQues->ans);

            sem_wait(&(newGroup->mutexSendWinner));

            strcpy(win, "WIN|");
            if (curQues->winnerFound){
                strcat(win, curQues->winner);
            } else {
                strcat(win, "NO WINNER");
            }
            strcat(win, "\r\n");

            if ( select(newGroup->maxSock, NULL, &(newGroup->participants), NULL, NULL)<0 ){
                fprintf( stderr, " select: %s\n", strerror(errno) );
                removeGroup(newGroup->name);
                *myPtr=NULL;
                return NULL;
            }

            for (fd=0; fd<(newGroup->maxSock); fd++){
                if (FD_ISSET(fd, &(newGroup->participants))){
                    if ( write(fd, win, strlen(win) )<0 ){
                        printf("ERROROROROROROR\n");
                    }
                }
            }

            if (curQues->next==NULL){
                newGroup->scores = (scoreRec*)malloc((newGroup->curSize)*sizeof(scoreRec));
                newGroup->scoresInd = 0;
                printf("sscores initialized\n");
            }

            printf("\nbefore lock\n");
            pthread_mutex_lock(&(newGroup->mutexNextQues));
            printf("\nlocked\n");
            pthread_cond_broadcast(&(newGroup->nextQues));
            pthread_mutex_unlock(&(newGroup->mutexNextQues));

            curQues=curQues->next;
            quesId++;
        }

        free(win);

        sem_wait(&(newGroup->mutexSendRes));
        sortRes(newGroup->scores, newGroup->scoresInd);

        char *result = (char*)malloc(sizeof(char)*( (newGroup->curSize)*(MAXCNAMELEN+MAXSIZELEN+2) + 8 ));
        strcpy(result, "RESULT");
        int i=0;
        for (i=0; i<(newGroup->scoresInd); i++){
            strcat(result, "|"  );
            strcat(result, (newGroup->scores)[i].name );
            strcat(result, "|"  );
            char *scoreHolder = intToString((newGroup->scores)[i].score);
            strcat(result,  scoreHolder);
            free(scoreHolder);
        }

        strcat(result, "\r\n"  );

        if ( select(newGroup->maxSock, NULL, &(newGroup->participants), NULL, NULL)<0 ){
            fprintf( stderr, " select: %s\n", strerror(errno) );
            removeGroup(newGroup->name);
            *myPtr=NULL;
            return NULL;
        }

        for (fd=0; fd<(newGroup->maxSock); fd++){
            if (FD_ISSET(fd, &(newGroup->participants))){
                if ( write(fd, result, strlen(result) )<0 ){
                    printf("ERROROROROROROR\n");
                }
            }
        }

        printf("sent\n");
        
        pthread_mutex_lock(&(newGroup->mutexGetRes));
        pthread_cond_broadcast(&(newGroup->getRes));
        pthread_mutex_unlock(&(newGroup->mutexGetRes));
    }

    if ( select(newGroup->maxSock, NULL, &(newGroup->participants), NULL, NULL)<0 ){
        fprintf( stderr, " select: %s\n", strerror(errno) );
        removeGroup(newGroup->name);
        *myPtr=NULL;
        return NULL;
    }

    char endGroup[11+strlen(newGroup->name)];
    strcpy(endGroup, "ENDGROUP|");
    strcat(endGroup, newGroup->name);
    strcat(endGroup, "\r\n");

    printf("\n%s\n", endGroup);

    for (fd=0; fd<(newGroup->maxSock); fd++){
        if (FD_ISSET(fd, &(newGroup->participants))){
            if ( write(fd, endGroup, strlen(endGroup) )<0 ){
                printf("ERROROROROROROR\n");
            }
        }
    }
    
    
    removeGroup(newGroup->name);
    *myPtr=NULL;

}

void* acceptClient(void *ssockcpy);

/*
*/
void* clientLeaveThread(void *arg){
    clientLeaveThreadArgument argVal = *((clientLeaveThreadArgument*)arg);

    int ssock=argVal.ssock;
    group *myGroup = argVal.myGroup;
    pthread_t myId = argVal.myPthreadId;
    char buf[BUFSIZE];
    int cc;

    while (1){
        if ( (cc = read( ssock, buf, BUFSIZE )) <= 0 ){
            pthread_cancel(myId);
            sem_wait( &(myGroup->mutexCurSize) );
            sem_wait(&(myGroup->mutexParticipants));
            FD_CLR(ssock, &(myGroup->participants));
            sem_post(&(myGroup->mutexParticipants));
            (myGroup->curSize)--;
            sem_post( &(myGroup->mutexCurSize) );
            if (argVal.quesId>=0){
                sem_wait(&(myGroup->mutexAnswered));
                myGroup->answered[argVal.quesId]--;
                sem_post(&(myGroup->mutexAnswered));
            }
            //pthread_mutex_unlock(&(myGroup->mutexNextQues));
            close(ssock);
            return NULL;
        }
        buf[cc-2]='\0';
        if (strcmp(buf, "LEAVE")==0){
            pthread_cancel(myId);
            sem_wait( &(myGroup->mutexCurSize) );
            sem_wait(&(myGroup->mutexParticipants));
            FD_CLR(ssock, &(myGroup->participants));
            sem_post(&(myGroup->mutexParticipants));
            (myGroup->curSize)--;
            sem_post( &(myGroup->mutexCurSize) );
            if (argVal.quesId>=0){
                sem_wait(&(myGroup->mutexAnswered));
                myGroup->answered[argVal.quesId]--;
                sem_post(&(myGroup->mutexAnswered));
            }
            pthread_mutex_unlock(&(myGroup->mutexNextQues));
            pthread_t thread;
            pthread_create( &thread, NULL, acceptClient, (void*) &ssock );
            break;
        }
    }
    
}

void cleanup(pthread_mutex_t *mutexNextQues)
{
    printf("cleanup");
    pthread_mutex_unlock(mutexNextQues);
}

/*
    Client Thread
*/
void* acceptClient(void *ssockcpy){

    char buf[BUFSIZE];
    int ssock=*((int*)ssockcpy);
    char *tokens[5];
    char *token;
    char *rest;
    int tokenCnt;

    int cc;
    sendOpenGroups(ssock);
    group *groupPtr = NULL;

    while(1){
        if ( (cc = read( ssock, buf, BUFSIZE )) <= 0 ){
            
            close(ssock);
            pthread_exit( NULL );
        }

        printf("%i.%s.\n", cc, buf);
        buf[cc-2]='\0';
        printf("%i.%s.\n", cc, buf);

        rest = buf;
        tokenCnt=0;
        while (tokenCnt<4 && (token = strtok_r(rest, "|", &rest))){
            tokens[tokenCnt++]=token;
        }

        int i;
        for (i=0; i<tokenCnt; i++){
            printf("%s.\n", tokens[i]);
        }

        if (strcmp(tokens[0], "GROUP")==0 && groupPtr==NULL){ // admin mode
            group *newGroup = (group*)malloc(sizeof(group));

            newGroup->isCanceled=0;
            strcpy(newGroup->topic, tokens[1]);
            strcpy(newGroup->name, tokens[2]);
            newGroup->size=atoi(tokens[3]);
            newGroup->quesNum=0;
            newGroup->isActive=0;
            newGroup->next=NULL;

            FD_ZERO(&(newGroup->participants));
            sem_init(&(newGroup->mutexParticipants), 0, 1);
            FD_SET(ssock, &(newGroup->participants));
            newGroup->maxSock=ssock+1;

            newGroup->curSize=0;
            sem_init(&(newGroup->mutexCurSize), 0, 1);

            newGroup->isStarted=0;
            sem_init(&(newGroup->mutexIsStarted), 0, 1);

            // ref: https://stackoverflow.com/questions/23400097/c-confused-on-how-to-initialize-and-implement-a-pthread-mutex-and-condition-vari
            pthread_cond_init( &(newGroup->full), NULL);
            pthread_mutex_init( &(newGroup->mutexIsFull), NULL);

            newGroup->answered=NULL;
            sem_init( &(newGroup->mutexAnswered), 0, 1);
            sem_init(&(newGroup->mutexSendWinner), 0, 0);

            pthread_cond_init( &(newGroup->nextQues), NULL);
            pthread_mutex_init( &(newGroup->mutexNextQues), NULL);

            sem_init(&(newGroup->mutexScores), 0, 1);

            sem_init(&(newGroup->mutexSendRes), 0, 0);

            pthread_cond_init( &(newGroup->getRes), NULL);
            pthread_mutex_init( &(newGroup->mutexGetRes), NULL);

            if (addGroup(newGroup)){

                printf("quizsend\n");

                if ( write( ssock, "SENDQUIZ\r\n", strlen("SENDQUIZ\r\n") ) < 0 ) {
                    removeGroup(newGroup->name);
                    
                    close( ssock );
                    pthread_exit( NULL );
                }

                if ( (cc = read( ssock, buf, BUFSIZE )) <= 0 ){
                    removeGroup(newGroup->name);
                    
                    close(ssock);
                    pthread_exit( NULL );
                }
                buf[cc-1]='\0';

                printf("%s.........\n", buf);

                char *quizTokens[3]={NULL, NULL, NULL};
                char *quizToken;
                char *quizRest=buf;
                int quizTokenCnt=0;
                while (quizTokenCnt<3 && (quizToken = strtok_r(quizRest, "|", &quizRest))){
                    quizTokens[quizTokenCnt++]=quizToken;
                }

                if(strcmp(quizTokens[0], "QUIZ")==0){

                    printf("quiz msg received\n");

                    int len = atoi(quizTokens[1]);
                    printf("len = %i\n", len);

                    nodeChar *firstChar = NULL;
                    nodeChar *lastChar = NULL;

                    int bufI = 0;

                    if (quizTokens[2]!=NULL){
                        for (bufI=0; bufI<strlen(quizTokens[2]); bufI++){
                            printf("%c", quizTokens[2][bufI]);
                            addCharToList(&firstChar, &lastChar, quizTokens[2][bufI]);
                        }
                        len-=strlen(quizTokens[2]);
                    }

                    while (len>0){
                        if ( (cc = read( ssock, buf, BUFSIZE )) <= 0 ){
                            removeGroup(newGroup->name);
                            
                            close(ssock);
                            pthread_exit( NULL );
                        }

                        bufI = 0;
                        for (bufI=0; bufI<strlen(buf); bufI++){
                            printf("%c", buf[bufI]);
                            addCharToList(&firstChar, &lastChar, buf[bufI]);
                        }

                        len-=cc;
                    }

                    if (parseQuiz(newGroup, firstChar)>0){ // group thread mode

                        write( ssock, "OK\r\n", strlen("OK\r\n") );

                        printf("%s %s %i %i\n *** \n", newGroup->name, newGroup->topic, newGroup->size, newGroup->curSize);
                        question *tempQ = newGroup->quizText;
                        while(tempQ != NULL){
                            printf("%s\n", tempQ->ques);
                            printf("%s\n\n", tempQ->ans);
                            tempQ=tempQ->next;
                        }

                        pthread_t groupThreadId;

                        groupPtr = newGroup;

                        int status = pthread_create( &groupThreadId, NULL, groupThread, (void*) &groupPtr ); // thread for group
                        
                        

                    } else {
                        removeGroup(tokens[2]);
                        if ( write( ssock, "BAD\r\n", strlen("BAD\r\n") ) < 0 ) {
                            
                            close( ssock );
                            pthread_exit( NULL );
                        }

                    }

                } else {
                    removeGroup(tokens[2]);
                    continue;
                }


            } else {
                if ( write( ssock, "BAD\r\n", strlen("BAD\r\n") ) < 0 ) {
                    
                    close( ssock );
                    pthread_exit( NULL );
                }
            }

        } else if (strcmp(tokens[0], "GETOPENGROUPS")==0){
            printf("getgroups\n");
            sendOpenGroups(ssock);

        } else if (strcmp(tokens[0], "JOIN")==0 && groupPtr==NULL){ // quiz participant mode

            group *myGroup = findGroup(tokens[1]);

            if (myGroup == NULL){
                printf("no group %s\n", tokens[0]);
                if ( write( ssock, "BAD\r\n", strlen("BAD\r\n") ) < 0 ) {
                    
                    close( ssock );
                    pthread_exit( NULL );
                }
                continue;
            }
            
            sem_wait( &(myGroup->mutexIsStarted) );
            if (myGroup->isStarted || myGroup->isCanceled){
                if ( write( ssock, "BAD\r\n", strlen("BAD\r\n") ) < 0 ) {
                    
                    close( ssock );
                    sem_post( &(myGroup->mutexIsStarted) );
                    pthread_exit( NULL );
                }
                sem_post( &(myGroup->mutexIsStarted) );
                continue;
            }

            
            if ( write( ssock, "OK\r\n", strlen("OK\r\n") ) < 0 ) {
                sem_post( &(myGroup->mutexIsStarted) );
                close( ssock );
                pthread_exit( NULL );
            }

            char cname[MAXCNAMELEN];
            strcpy(cname, tokens[2]);

            sem_wait( &(myGroup->mutexCurSize) );
            (myGroup->curSize)++;

            sem_wait(&(myGroup->mutexParticipants));
            FD_SET( ssock, &(myGroup->participants) );
            if ((myGroup->maxSock)<ssock+1){
                (myGroup->maxSock)=ssock+1;
            }
            sem_post(&(myGroup->mutexParticipants));

            
            if (myGroup->curSize==myGroup->size){
                myGroup->isStarted=1;
            } 

            sem_post( &(myGroup->mutexCurSize) );
            sem_post( &(myGroup->mutexIsStarted) );
            
            pthread_t myId = pthread_self();
            pthread_t leaveThreadId;
            clientLeaveThreadArgument toThread = {myGroup, ssock, myId, -1};
            pthread_create( &leaveThreadId, NULL, clientLeaveThread, (void*) &toThread );


            printf("%s is waiting\n", cname);

            pthread_mutex_lock(&(myGroup->mutexIsFull));
            if (!(myGroup->isStarted)){
                pthread_cond_wait(&(myGroup->full), &(myGroup->mutexIsFull));
            }
            pthread_cond_broadcast(&(myGroup->full));
            pthread_mutex_unlock(&(myGroup->mutexIsFull));

            if (myGroup->isCanceled){
                continue;
            }

            printf("%s started\n", cname);

            question *curQues = myGroup->quizText;

            // using timer ref: https://www.youtube.com/watch?v=qyFwGyTYe-M
            fd_set mySet;
            struct timeval timeout;
            int sret;
            _Bool isLeaved=0;
            int score = 0;
            int quesId = 0;

            pthread_cancel(leaveThreadId);

            while (curQues!=NULL){

                FD_ZERO(&mySet);
                FD_SET(ssock, &mySet);
                timeout.tv_sec=60;
                timeout.tv_usec=0;

                if ( (sret=select(ssock+1, &mySet, NULL, NULL, &timeout))<0 ){
                    fprintf( stderr, " select: %s\n", strerror(errno) );
                    removeGroup(tokens[2]);
                    return NULL;
                }

                if (sret==0){
                    
                    sem_wait(&(myGroup->mutexCurSize));
                    sem_wait(&(myGroup->mutexParticipants));
                    FD_CLR(ssock, &(myGroup->participants));
                    sem_post(&(myGroup->mutexParticipants));
                    (myGroup->curSize)--;
                    if (myGroup->curSize==myGroup->answered[quesId]){
                        sem_post(&(myGroup->mutexSendWinner));
                    }
                    sem_post(&(myGroup->mutexCurSize));
                    isLeaved=1;                   
                    break;
                }

                if ( (cc = read( ssock, buf, BUFSIZE )) <= 0 ){
                    sem_wait(&(myGroup->mutexCurSize));
                    sem_wait(&(myGroup->mutexParticipants));
                    FD_CLR(ssock, &(myGroup->participants));
                    sem_post(&(myGroup->mutexParticipants));
                    (myGroup->curSize)--;
                    if (myGroup->curSize==myGroup->answered[quesId]){
                        sem_post(&(myGroup->mutexSendWinner));
                    }
                    sem_post(&(myGroup->mutexCurSize));
                    
                    close(ssock);
                    pthread_exit( NULL );
                }

                


                buf[cc-2]='\0';
                printf("%s\n", buf);

                if (strcmp(buf, "LEAVE")==0){
                    
                    sem_wait(&(myGroup->mutexCurSize));
                    sem_wait(&(myGroup->mutexParticipants));
                    FD_CLR(ssock, &(myGroup->participants));
                    sem_post(&(myGroup->mutexParticipants));
                    (myGroup->curSize)--;
                    if (myGroup->curSize==myGroup->answered[quesId]){
                        sem_post(&(myGroup->mutexSendWinner));
                    }
                    sem_post(&(myGroup->mutexCurSize));
                    isLeaved=1;                    
                    break;
                }
                
                char *ansTokens[2];
                char *ansToken;
                char *ansRest=buf;
                int ansTokenCnt=0;
                while (ansTokenCnt<2 && (ansToken = strtok_r(ansRest, "|", &ansRest))){
                    ansTokens[ansTokenCnt++]=ansToken;
                }

                printf(".%s.%s.", ansTokens[0], ansTokens[1]);

                if (strcmp(ansTokens[0], "ANS")==0 && strcmp(ansTokens[1], curQues->ans)==0){
                    printf("%s correct\n", cname);
                    sem_wait(&(curQues->mutexWinner));
                    if (!(curQues->winnerFound)){
                        curQues->winnerFound=1;
                        strcpy(curQues->winner, cname);
                        score++;
                    }
                    sem_post(&(curQues->mutexWinner));
                    score++;
                } else if ( strcmp(ansTokens[0], "ANS")!=0 || strcmp(ansTokens[1], "NOANS")!=0 ){
                    score--;
                }

                sem_wait(&(myGroup->mutexAnswered));
                myGroup->answered[quesId]++;
                if (myGroup->curSize==myGroup->answered[quesId]){
                    sem_post(&(myGroup->mutexSendWinner));
                }
                sem_post(&(myGroup->mutexAnswered));


                toThread.quesId=quesId;
                pthread_create( &leaveThreadId, NULL, clientLeaveThread, (void*) &toThread );

                pthread_mutex_lock(&(myGroup->mutexNextQues));
                pthread_cond_wait(&(myGroup->nextQues), &(myGroup->mutexNextQues));
                pthread_cond_broadcast(&(myGroup->nextQues));
                pthread_mutex_unlock(&(myGroup->mutexNextQues));

                curQues=curQues->next;
                quesId++;

                pthread_cancel(leaveThreadId);
            }

            if (!isLeaved){


                sem_wait(&(myGroup->mutexScores));

                myGroup->scores[myGroup->scoresInd].name=cname;
                myGroup->scores[myGroup->scoresInd].score=score;
                (myGroup->scoresInd)++;
                printf("%i %i\n", myGroup->scoresInd, myGroup->curSize);
                if (myGroup->scoresInd==myGroup->curSize){
                    sem_post(&(myGroup->mutexSendRes));
                    printf("post send\n");
                }

                sem_post(&(myGroup->mutexScores));

                pthread_mutex_lock(&(myGroup->mutexGetRes));
                pthread_cond_wait(&(myGroup->getRes), &(myGroup->mutexGetRes));
                pthread_cond_broadcast(&(myGroup->getRes));
                pthread_mutex_unlock(&(myGroup->mutexGetRes));

                printf("ff");
            }

            printf("%s\n", cname);

            printf("user end\n");

            
            

        } else if (strcmp(tokens[0], "CANCEL")==0 && groupPtr!=NULL && !(groupPtr->isStarted) && tokenCnt==2 && strcmp(tokens[1], groupPtr->name)==0){
            groupPtr->isCanceled=1;
            pthread_mutex_lock(&(groupPtr->mutexIsFull));
            pthread_cond_broadcast(&(groupPtr->full));
            pthread_mutex_unlock(&(groupPtr->mutexIsFull));
        } else {

            if ( write( ssock, "BAD\r\n", strlen("BAD\r\n") ) < 0 ) {
                close( ssock );
                pthread_exit( NULL );
            }

        }

    } 
    
    return NULL;

}


/* 
    Main thread
*/
int main( int argc, char *argv[] ) {

    printf("hello\n\n");

    char            *service;
    struct sockaddr_in  fsin;
    int         alen;
    int         msock;
    int         ssock;
    int         rport = 0;

    switch (argc) 
    {
        case    1:
            // No args? let the OS choose a port and tell the user
            rport = 1;
            break;
        case    2:
            // User provides a port? then use it
            service = argv[1];
            break;
        default:
            fprintf( stderr, "usage: server [port](optional)\n" );
            exit(-1);
    }

    msock = passivesock( service, "tcp", QLEN, &rport );
    if (rport)
    {
        //  Tell the user the selected port
        printf( "server: port %d\n", rport );   
        fflush( stdout );
    }

    /*
        initializing global variables
    */
    sem_init(&mutexGroupsList, 0, 1);
    firstGroup=NULL;


    while(1)
    {
        int status;

        alen = sizeof(fsin);
        ssock = accept( msock, (struct sockaddr *)&fsin, &alen );
        if (ssock < 0)
        {
            fprintf( stderr, "accept: %s\n", strerror(errno) );
            exit(-1);
        }

        int ssockcpy=ssock;
        pthread_t thread;

        status = pthread_create( &thread, NULL, acceptClient, (void*) &ssockcpy );
    }

}