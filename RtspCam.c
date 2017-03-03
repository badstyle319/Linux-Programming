#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOCKET int
#define BUFFERSIZE 2048
#define LINEBUFFER 128
#define closesocket(x) close(x)

typedef enum{
	false, true
}bool;

typedef unsigned char	BYTE;
typedef unsigned char	UINT8;
typedef unsigned short	UINT16;
typedef unsigned int	UINT32;
typedef unsigned		UINT;

char IBF[BUFFERSIZE];
char OBF[BUFFERSIZE];

static char COMM_base[] = "rtsp://%s:%d/axis-media/media.amp?";
static char COMM_a[] = "rtsp://%s:%d/axis-media/media.amp?videocodec=%s&resolution=352x240 RTSP/1.0\r\n";
static char CSEQ[] = "CSeq: %d\r\n";
static char UA[] = "User-Agent: CamAgent\r\n";
static char SESN[] = "Session: %d\r\n";
static char TNSP[] = "Transport: RTP/AVP;unicast;client_port=%d-%d\r\n";
static char ACCP[] = "Accept: application/sdp\r\n";
static char AUTH[] = "Authorization: Basic %s\r\n";

static char Base64Table[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

typedef enum{
	RC_DESCRIBE,
	RC_SETUP,
	RC_OPTIONS,
	RC_PLAY,
	RC_PAUSE,
	RC_TEARDOWN
}COMMAND;

void encode_base64(char* out, char* str){
	int len = strlen(str);
	int l,r;

	if(str[0]==0)
		return;

	while((3-len)>0){
		str[len]=0;
		len++;
	}
	
	l=str[0]>>2;
	out[0]=Base64Table[l];
	r=(str[0]%4)<<4;
	l=(str[1]>>4)+r;
	out[1]=Base64Table[l];
	if(str[1]==0){
		out[2]='=';
		out[3]='=';
		return;
	}
	r=(str[1]%16)<<2;
	l=(str[2]>>6)+r;
	out[2]=Base64Table[l];
	if(str[2]==0){
		out[3]='=';
		return;
	}
	r=(str[2]%64);
	out[3]=Base64Table[r];

	if(len>3)
		encode_base64(out+4,str+3);
}

SOCKET tcp_bind(char* src_ip, int src_port){	
	SOCKET sockfd;
	struct sockaddr_in mysock;
	const int on = 1;

	if(-1==(sockfd = socket(AF_INET, SOCK_STREAM, 0)))
		return (-1);

	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));
	memset(&mysock, 0, sizeof(mysock));
	mysock.sin_family = AF_INET;

	if(src_ip == NULL)
		mysock.sin_addr.s_addr = htonl(INADDR_ANY);
	else
		mysock.sin_addr.s_addr = inet_addr(src_ip);

	mysock.sin_port = htons(src_port);
	
	if(0!=bind(sockfd, (struct sockaddr *) &mysock, sizeof(mysock))){
		closesocket(sockfd);
		return (-1);
	}
	return sockfd;
}

SOCKET tcp_connect(char* src_ip, int src_port, char* dest_ip, int dest_port){
	SOCKET sockfd;
	struct sockaddr_in remote_sock;

	sockfd = tcp_bind(src_ip, src_port);

	memset(&remote_sock, 0, sizeof(remote_sock));
	remote_sock.sin_family = AF_INET;
	remote_sock.sin_addr.s_addr = inet_addr(dest_ip);//140.124.13.3
	remote_sock.sin_port = htons(dest_port);
	
	if(-1 == connect(sockfd, (struct sockaddr *) &remote_sock, sizeof(remote_sock))){
		closesocket(sockfd);
		return (-1);
	}

	return sockfd;
}

bool parse_response(char* buffer, int *ofst){
	char* s = buffer+(*ofst);
	char* p = NULL;
	int co=0;

	p = strstr(s, "\r\n");
	if(!p)
		return false;
	*p=0;
	p+=2;
	
	if(!sscanf(s,"RTSP/%*f %d %*s\0\n", &co))
		return false;
	
	s=p;
	
	//parse line by line and save http response attribute
	while(*s){
		p=strstr(s, "\r\n");
		//change \r\n to \0\n
		if(!p) return false;
		*p=0;
		//only \0\n
		if(strlen(s)==0){
			s=p+2;
			break;
		}
		s=p+2;
	}
	*ofst = s-buffer;
	return true;
}

bool prepare_rtsp_request(COMMAND cmd, char* s_ip, int s_port, char* codec, char* rso){
	char* request = NULL;
	char* pos = OBF;
	memset(OBF, 0, sizeof(OBF));

	switch(cmd){
		case RC_DESCRIBE:
			request = "DESCRIBE";break;
		case RC_SETUP:
			request = "SETUP";break;
		case RC_OPTIONS:
			request = "OPTIONS";break;
		case RC_PLAY:
			request = "PLAY";break;
		case RC_PAUSE:
			request = "PAUSE";break;
		case RC_TEARDOWN:
			request = "TEARDOWN";break;
	}
	if(!request)
		return false;

	pos+=sprintf(pos, "%s ", request);

	if(cmd==RC_OPTIONS)
		pos+=sprintf(pos, "*");
	else{
		pos+=sprintf(pos, COMM_base, s_ip, s_port);
		if(codec){
			pos+=sprintf(pos, "videocodec=%s", codec);
			if(rso)
				pos+=sprintf(pos, "&resolution=%s", rso);
		}
	}

	pos+=sprintf(pos, " RTSP/1.0\r\n");
	pos+=sprintf(pos, CSEQ, 0);
	pos+=sprintf(pos, UA);
	pos+=sprintf(pos, SESN, 12345678);
	char out[128]={0};
	encode_base64(out, "root:root");
	pos+=sprintf(pos,AUTH, out);

	pos+=sprintf(pos,"\r\n");

	fprintf(stdout, "%s", OBF);

}

int main(){
	//*****************connect to server********************
	SOCKET sockfd;
	sockfd = tcp_connect(NULL, 0, "10.2.0.160", 80);

	if(-1==sockfd){
		fprintf(stderr, "Error with client connecting to server\n");
		closesocket(sockfd);
		exit(0);
	}else
		fprintf(stdout, "Client connect to server successfully !!\n");

	//*****************prepare and send request*****************

	prepare_rtsp_request(RC_OPTIONS, "10.2.0.160", 554, "h264", "640x480");

	if(send(sockfd, OBF, strlen(OBF),0)<0){
		fprintf(stderr, "ERROR: Can't send to server!\n");
		closesocket(sockfd);
		exit(0);
	}

	//********************receiving data process*****************

	bool bRUN = true;
	int  pos=0;
	char *buffer = IBF;
	fd_set fds;
	struct timeval tmo;

	tmo.tv_sec = 2;
	tmo.tv_usec = 0;

	while(bRUN){		
		FD_ZERO(&fds) ;
		FD_SET(sockfd, &fds);
		int ret = select(sockfd+1, &fds, NULL, NULL, &tmo);
		if(ret > 0){
			int len = recv(sockfd, buffer, BUFFERSIZE, 0);
			if(len>0){
				printf("%s", buffer);
			}else
				bRUN=false;
    		}
	}

	//close socket
	closesocket(sockfd);

	return 0;
}
