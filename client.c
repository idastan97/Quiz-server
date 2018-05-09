#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <pthread.h>

#define BUFSIZE		4096
#define MAXMESSIZE    10
#define MAXSIZELEN    10

int connectsock( char *host, char *service, char *protocol );

_Bool quit=0;
_Bool serverEnd=0;


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

    int len = 0;
    while (xCopy>0){
        xCopy/=10;
        len++;
    }

    res[fromInd+len]='\0';

    while (len>0){
        res[fromInd+len-1]=(x%10)+'0';
        x/=10;
        len--;
    }

    return res;
}

void* sendThread(void* csockcpy){
    char		buf[BUFSIZE];
    int		csock=*((int*)csockcpy);
    while ( !serverEnd && fgets( buf, BUFSIZE, stdin ) != NULL )
    	{  
            buf[strlen(buf)-1]='\0';
            if (strcmp(buf, "quiz")==0){
                fgets( buf, BUFSIZE, stdin );
                buf[strlen(buf)-1]='\0';
                int fd = open( buf, O_RDONLY);
                int len = lseek(fd, 0, SEEK_END);
                lseek(fd, 0, SEEK_SET);

                strcpy(buf, "QUIZ|");
                strcat(buf, intToString(len));
                strcat(buf, "|");

                if ( write( csock, buf, strlen(buf) ) < 0 ) {
                    fprintf( stderr, "client write: %s\n", strerror(errno) );
                    close(fd);
                    quit=1;
                    printf("end sending\n");
                    pthread_exit( NULL );
                }

                int cc;
                while( (cc = read(fd, buf, MAXMESSIZE-1))>0 ) {
                    buf[cc]='\0';
                    printf("%s", buf);
                    if ( write( csock, buf, strlen(buf) ) < 0 ) {
                        fprintf( stderr, "client write: %s\n", strerror(errno) );
                        close(fd);
                        quit=1;
                        printf("end sending\n");
                        pthread_exit( NULL );
                    }
                }
                close(fd);
                continue;
            }
    		// Send to the server
            strcat(buf, "\r\n");
    		if ( write( csock, buf, strlen(buf) ) < 0 )
    		{
    			fprintf( stderr, "client write: %s\n", strerror(errno) );
    			break;
    		}

    	}
    quit=1;
    printf("end sending\n");
    pthread_exit( NULL );
}

void* receiveThread(void* csockcpy){

    char		buf[BUFSIZE];
    int		cc;
    int		csock= *((int*)csockcpy);

    while(!quit){
        // Read the echo and print it out to the screen
        if ( (cc = read( csock, buf, BUFSIZE )) <= 0 )
        {
           	printf( "The server has gone.\n" );
            break;
        }
        else
        {
            buf[cc] = '\0';
            printf( "%s", buf );
        }
    }
    serverEnd=1;
    close(csock);
    printf("end receiving\n");
    pthread_exit( NULL );
}


/*
**	Client
*/
int
main( int argc, char *argv[] )
{
	char		buf[BUFSIZE];
	char		*service;		
	char		*host = "localhost";
	int		cc;
	int		csock;
	
	switch( argc ) 
	{
		case    2:
			service = argv[1];
			break;
		case    3:
			host = argv[1];
			service = argv[2];
			break;
		default:
			fprintf( stderr, "usage: chat [host] port\n" );
			exit(-1);
	}

	/*	Create the socket to the controller  */
	if ( ( csock = connectsock( host, service, "tcp" )) == 0 )
	{
		fprintf( stderr, "Cannot connect to server.\n" );
		exit( -1 );
	}

	printf( "The server is ready, please start sending to the server.\n" );
	printf( "Type q or Q to quit.\n" );
	fflush( stdout );

    int csockcpy=csock;
    pthread_t sendId, rcvId;

	pthread_create( &sendId, NULL, sendThread, (void*) &csockcpy );
	pthread_create( &rcvId, NULL, receiveThread, (void*) &csockcpy );

    pthread_join( sendId, NULL );
    pthread_join( rcvId, NULL );

	close( csock );

}


