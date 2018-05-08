//#define _GNU_SOURCE 1
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
#include <vector>
#include <map>

#define USER_LIMIT	5			//最大用户数量
#define BUF_SIZE	64
#define FD_LIMIT	65536		//文件描述符限制

#define PRINT_DEBUG 1			//打印调试日志

//客户端连接数据
typedef struct client_data
{
	int			 fd_;
	sockaddr_in	 addr_;
	char*	wr_buf_;
	char buf_[BUF_SIZE];
}client_data_t;

typedef std::map< int, client_data_t > MAP_USER_DATA;

typedef std::vector< pollfd >	VEC_POLLFD;


int remove_userdata( MAP_USER_DATA& mapuserdata, int fd )
{
	int ret = -1;
	MAP_USER_DATA::iterator it = mapuserdata.find( fd );
	if( it != mapuserdata.end() )
	{
		ret = 0;
		mapuserdata.erase( it );
	}
	
	return ret;
}


int setnobloking( int fd )
{
	int old_opt = fcntl( fd, F_GETFL );
	int new_opt = old_opt | O_NONBLOCK;
	fcntl( fd, F_SETFL, new_opt );
	return old_opt;
}

int main( int argc, char* argv[] )
{
	if( argc <= 2 )
	{
		printf( "usage: %s ip port\n", argv[0] );
		return 1;
	}
	
	const char* ip = argv[1];
	int port = atoi( argv[2] );
	int ret = 0;
	
	struct sockaddr_in addr;
	bzero( &addr, sizeof( addr ) );
	addr.sin_family = AF_INET;
	inet_pton( AF_INET, ip, &addr.sin_addr );
	addr.sin_port = htons( port );
	
	int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
	assert(  listenfd != -1 );
	
	
	ret = bind( listenfd, ( struct sockaddr* )&addr, sizeof( addr ) );
	assert( ret != -1 );
	
	ret = listen( listenfd, 5 );
	assert( ret != -1 );
	printf( "listen[%s:%d] success fd[%d]\n", ip, port, listenfd );
	
	MAP_USER_DATA map_userdata;
	
	VEC_POLLFD vec_fds;
	pollfd pollfd_svr;
	pollfd_svr.fd = listenfd;
	pollfd_svr.events = POLLIN /*| POLLERR*/;
	pollfd_svr.revents = 0;
	vec_fds.push_back( pollfd_svr );
	
	int user_count = 0;
	VEC_POLLFD vec_newcli_fds;
	while( 1 )
	{
		ret = poll( &( *vec_fds.begin() ), vec_fds.size(), -1 );
		if( ret < 0 )
		{
			printf( "poll failed err[%d]\n", errno );
			break;
		}
		
		
		VEC_POLLFD::iterator itvec = vec_fds.begin();
		for( ; itvec != vec_fds.end() ; )
		{
#if PRINT_DEBUG
			printf( "fd[%d] revents[0x%x]\n", itvec->fd, itvec->revents );
#endif
			if( ( itvec->fd == listenfd ) && ( itvec->revents & POLLIN ) )
			{
				struct sockaddr_in cli_addr;
				socklen_t cli_addrlen = sizeof(  cli_addr );
				int clifd = accept( listenfd, ( struct sockaddr* )&cli_addr, &cli_addrlen );
				if(  clifd < 0 )
				{
					printf( "accept failed err[%d]\n", errno );
					itvec++;
					continue;
				}
				if( user_count > USER_LIMIT )
				{
					//TODO?
				}
				
				printf( "new client[%s:%d] connect fd[%d] user count[%d]\n", 
				inet_ntoa( cli_addr.sin_addr ), ntohs( cli_addr.sin_port ), clifd, ++user_count );
				
				client_data_t clidata = {0};
				clidata.fd_ = clifd;
				clidata.addr_ = cli_addr;
				map_userdata[clifd] = clidata;
				
				setnobloking( clifd );
	
				pollfd pollfd_cli;
				pollfd_cli.fd = clifd;
				pollfd_cli.events = POLLIN | POLLRDHUP | POLLERR;
				pollfd_cli.revents = 0;
				vec_newcli_fds.push_back( pollfd_cli );
			}//new client connection coming
			else if( itvec->revents & POLLERR )
			{
				//TODO:
				//printf( "fd[%d] err\n", itvec->fd );
				//continue;
			}
			else if( itvec->revents & POLLRDHUP )
			{
				//client close connection
				//FIXME:useing recv() = 0 to judge peer close connection
				printf( "client close connection fd[%d] POLLRDHUP\n", itvec->fd );
				user_count--;
				close( itvec->fd );
				remove_userdata( map_userdata, itvec->fd );
				itvec = vec_fds.erase(itvec);
				continue;
			}
			else if( itvec->revents & POLLIN )
			{
				MAP_USER_DATA::iterator it = map_userdata.find( itvec->fd );
				if( it == map_userdata.end() )
				{
					printf( "can not find user data by fd[%d]!\n", itvec->fd );
					itvec++;
					continue;
				}
				client_data_t& userdata = it->second;
				memset( userdata.buf_, 0, sizeof( userdata.buf_ ) );
				ret = recv( userdata.fd_, userdata.buf_, sizeof( userdata.buf_ ) - 1 , 0 );
				if( ret < 0 )
				{
						if( errno != EAGAIN && errno != EWOULDBLOCK )
						{
							//read error
							printf( "client close connection fd[%d] err[%d] vec_fds size[%d]\n", itvec->fd, errno, vec_fds.size() );
							user_count--;
							close( itvec->fd );
							remove_userdata( map_userdata, itvec->fd );
							itvec = vec_fds.erase(itvec);
							continue;
						}
				}
				else if( ret == 0 )
				{
					//peer close connection
					printf( "client close connection fd[%d]\n", itvec->fd );
					user_count--;
					close( itvec->fd );
					remove_userdata( map_userdata, itvec->fd );
					itvec = vec_fds.erase(itvec);
					continue;			
				}
				else
				{
					printf( "client[%d] --> msg[%s]\n", userdata.fd_, userdata.buf_ );
					//notify other client socket to ready to send data
					MAP_USER_DATA::iterator itmap_temp;
					VEC_POLLFD::iterator itvec_tmp = vec_fds.begin();
					for( ; itvec_tmp != vec_fds.end(); itvec_tmp++ )
					{
						if( itvec_tmp->fd == listenfd )
						{
							continue;
						}
						itvec_tmp->events |= ~POLLIN;
						itvec_tmp->events |= POLLOUT;
						itmap_temp = map_userdata.find( itvec_tmp->fd );
						if( itmap_temp == map_userdata.end() )
						{
							printf( "not find fd[%d]!", itvec_tmp->fd );
							continue;
						}
						itmap_temp->second.wr_buf_ = userdata.buf_;
					}
					
				}
			}
			else if( itvec->revents & POLLOUT )
			{
				MAP_USER_DATA::iterator itmap_tmp = map_userdata.find( itvec->fd );
				if( itmap_tmp == map_userdata.end() )
				{
					itvec++;
					continue;
				}
				client_data_t& userdata = itmap_tmp->second;
				if( !userdata.wr_buf_ )
				{
					itvec++;
					continue;
				}
				
				ret = send( userdata.fd_, userdata.wr_buf_, strlen( userdata.wr_buf_ ), 0 );
				printf( "--> client[%d] msg[%s]\n", userdata.fd_, userdata.wr_buf_ );
				userdata.wr_buf_ = NULL;
				itvec->events |= ~POLLOUT;
				itvec->events |= POLLIN;
			}
			itvec++;
		}
		//add new client fds
		if( vec_newcli_fds.size() > 0 )
		{
			vec_fds.insert( vec_fds.end(), vec_newcli_fds.begin(), vec_newcli_fds.end() );
			vec_newcli_fds.clear();
		}
	}//while
	
	close( listenfd );
	
	return 0;
}




