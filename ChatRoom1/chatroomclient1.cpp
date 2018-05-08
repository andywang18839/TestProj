#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <fcntl.h>
#include <assert.h>

#define BUF_SIZE 64


int main( int argc, char* argv[] )
{
	if( argc <=2 )
	{
		printf("usage: %s ip port\n", argv[0]);
		return 1;
	}
	
	const char* ip = argv[1];
	int port = atoi( argv[2] );
	
	struct sockaddr_in svr_addr;
	bzero( &svr_addr, sizeof( svr_addr ) );
	svr_addr.sin_family = AF_INET;
	inet_pton( AF_INET, ip, &svr_addr.sin_addr );
	svr_addr.sin_port = htons( port );
	
	int sockfd = socket( PF_INET, SOCK_STREAM, 0 );
	assert( sockfd >= 0 );
	if( connect( sockfd, (struct sockaddr*)&svr_addr, sizeof(svr_addr) ) < 0 )
	{
		printf( "connect [%s:%d] failed err[%d]\n", ip, port, errno );
		close( sockfd );
		return 1;
	}
	printf( "connect [%s:%d] success\n", ip, port );
	//注册文件描述符0(标准输入)和文件描述符sockfd上的可读事件
	pollfd fds[2];
	fds[0].fd = 0;
	fds[0].events = POLLIN;
	fds[0].revents = 0;
	fds[1].fd = sockfd;
	fds[1].events = POLLIN | POLLRDHUP;
	fds[1].revents = 0;
	
	char rd_buf[BUF_SIZE];
	char stdin_buf[BUF_SIZE];
	int pipefd[2];
	int ret = pipe( pipefd );
	assert( ret != -1 );
	
	while( 1 )
	{
		ret = poll( fds, 2, -1 );
		if( ret < 0 )
		{
			printf( "poll failed err[%d]\n", errno );
			break;
		}
		
		if(  fds[1].revents & POLLRDHUP )
		{
			printf( "server close the connect!\n" );
			break;
		}
		
		if( fds[1].revents & POLLIN )
		{
			memset( rd_buf, 0, sizeof( rd_buf ) );
			ret = recv( fds[1].fd, rd_buf, BUF_SIZE - 1, 0 );
			if( ret > 0 && strlen( rd_buf ) > 0 )
			{
				printf( "svr --> %s\n", rd_buf );
			}
			
		}
		
		if( fds[0].revents & POLLIN )
		{
			//使用splice将用户输入数据直接写到sockfd上（零拷贝）
			//FIXME:不支持splice，不知道是否是虚拟机系统问题
			/*ret = splice( 0, NULL, pipefd[1], NULL, 100,
					SPLICE_F_MORE | SPLICE_F_MOVE );
					printf( "splice 1 err[%d]\n", errno );
			ret = splice( pipefd[0], NULL, sockfd, NULL, 100,
					SPLICE_F_MORE | SPLICE_F_MOVE );
			*/
			
			memset( stdin_buf, 0, sizeof( stdin_buf ) );
			ret = read( fds[0].fd, stdin_buf, BUF_SIZE - 1 );
			if( ret <= 0 )
			{
				printf( "read stdin failed err[%d]", errno );
				continue;
			}
			char* rdata = strtok( stdin_buf, "\n" );
			if( strcmp( rdata, "q" ) == 0 || strcmp( rdata, "Q" ) == 0 )
			{
				printf( "user exit chatroom\n" );
				break;
			}
			else if( strcmp( rdata, "d" ) == 0 || strcmp( rdata, "D" ) == 0 )
			{
				printf( "i will core dump!\n" );
				char* p;
				*p = 0;
				break;
			}
			ret = send( sockfd, rdata, strlen( rdata ), 0 );
			printf( "send msg[ %s ] --> svr\n", rdata );
		}
		
	}//while
	
	
	close( pipefd[0] );
	close(  pipefd[1] );
	close( sockfd );
	return 0;
}

