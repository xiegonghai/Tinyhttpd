/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
/*
isspace是包含在ctype的头文件
检查参数c是否为空格字符，也就是判断是否为空格(' ')、水平定位字符
('\t')、归位键('\r')、换行('\n')、垂直定位字符('\v')或翻页('\f')的情况
若参数c为空格字符，则返回TRUE，否则返回NULL(0)，

*/
#define ISspace(x) isspace((int)(x)) 

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

void accept_request(void *);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
void accept_request(void *client1)
{
 int client = *(int *)client1;
 pthread_detach(pthread_self());
 char buf[1024];
 int numchars;
 char method[255];
 char url[255];
 char path[512];
 size_t i, j;
 struct stat st;
 int cgi = 0;      //如果服务器确定是CGI程序，则置为true
 char *query_string = NULL;
 printf("accept_request is accept %d\n", client);
 numchars = get_line(client, buf, sizeof(buf));
 printf("total get %d from client socket\n",numchars);
 i = 0; j = 0;
 while (!ISspace(buf[j]) && (i < sizeof(method) - 1))
 {
  method[i] = buf[j];//非空则读取BUFF，知道读到空或者读满了method
  i++; j++;
 }
 method[i] = '\0';

 if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))//strcasecmp是忽略大小写比较
 {
  unimplemented(client);//如果既不是GET也不是POST返回未实现的错误
  return ;
 }

 if (strcasecmp(method, "POST") == 0)//如果是POST方法
  cgi = 1; //开放执行CGI命令

 i = 0;
 while (ISspace(buf[j]) && (j < sizeof(buf)))//过滤buf空字符
  j++; 
 while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))//读取url
 {
  url[i] = buf[j];
  i++; j++;
 }
 url[i] = '\0';

 if (strcasecmp(method, "GET") == 0)//如果是GET方法
 {
  query_string = url;             //将url赋给查询字符串
  while ((*query_string != '?') && (*query_string != '\0'))//读到？或者休止符停止
   query_string++;
  if (*query_string == '?')//以问号结尾的话，需要给查询字符串赋休止符停止
  {
   cgi = 1;
   *query_string = '\0'; 
   query_string++;
  }
 }

 sprintf(path, "htdocs%s", url);//确定相应的查询路径
 if (path[strlen(path) - 1] == '/')//最后一个是/的话
  strcat(path, "index.html");//加上主页
 if (stat(path, &st) == -1) {  
  while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
   numchars = get_line(client, buf, sizeof(buf));
  not_found(client);
 }
 else
 {
  if ((st.st_mode & S_IFMT) == S_IFDIR)
   strcat(path, "/index.html");
  if ((st.st_mode & S_IXUSR) ||
      (st.st_mode & S_IXGRP) ||
      (st.st_mode & S_IXOTH)    )
   cgi = 1;
  if (!cgi)
   serve_file(client, path);//如果非cgi，提供网页文件
  else
   execute_cgi(client, path, method, query_string);//根据查询字符提供CGI服务
 }
 free(client1);
 close(client);
 //return NULL;
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/*利用send来告诉客户端其请求产生了什么问题*/
/**********************************************************************/
void bad_request(int client)
{
 char buf[1024];

 sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "Content-type: text/html\r\n");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "<P>Your browser sent a bad request, ");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "such as a POST without a Content-Length.\r\n");
 send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
 /*将整个file文件的数据写入到socket*/
/**********************************************************************/

void cat(int client, FILE *resource)
{
 char buf[1024];

 fgets(buf, sizeof(buf), resource);
 while (!feof(resource))//是否已读到文件末尾
 {
  send(client, buf, strlen(buf), 0);//读一buf部分或者文件的一行数据(buf太大也是仅一行)
  fgets(buf, sizeof(buf), resource);
 }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/*告诉client这个CGI脚本不能运行*/
/**********************************************************************/
void cannot_execute(int client)
{
 char buf[1024];

 sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
 perror(sc);
 exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
 /*执行一个CGI程序，需要设定一些环境变量*/
/**********************************************************************/
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string)
{
 char buf[1024];
 int cgi_output[2];
 int cgi_input[2];
 pid_t pid;
 int status;
 int i;
 char c;
 int numchars = 1;
 int content_length = -1;

 buf[0] = 'A'; buf[1] = '\0';
 if (strcasecmp(method, "GET") == 0)
  while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
   numchars = get_line(client, buf, sizeof(buf));
 else    /* POST */
 {
  numchars = get_line(client, buf, sizeof(buf));
  while ((numchars > 0) && strcmp("\n", buf))//未读到换行符
  {
   buf[15] = '\0';
   if (strcasecmp(buf, "Content-Length:") == 0)//直到读到了相应的关键字
    content_length = atoi(&(buf[16]));//读取相应的内容长度
   numchars = get_line(client, buf, sizeof(buf));
  }
  if (content_length == -1) {
   bad_request(client);
   return;
  }
 }

 sprintf(buf, "HTTP/1.0 200 OK\r\n");
 send(client, buf, strlen(buf), 0);
/*
pipe（建立管道）：
1) 头文件 #include<unistd.h>
2) 定义函数： int pipe(int filedes[2]);
3) 函数说明： pipe()会建立管道，并将文件描述词由参数filedes数组返回。
              filedes[0]为管道里的读取端
              filedes[1]则为管道的写入端。
4) 返回值：  若成功则返回零，否则返回-1，错误原因存于errno中。

    错误代码: 
         EMFILE 进程已用完文件描述词最大量
         ENFILE 系统已无文件描述词可用。
         EFAULT 参数 filedes 数组地址不合法。
*/
 if (pipe(cgi_output) < 0) {//父子进程使用管道通信
  cannot_execute(client);
  return;
 }
 if (pipe(cgi_input) < 0) {//建立输入输出管道
  cannot_execute(client);
  return;
 }

 if ( (pid = fork()) < 0 ) {//fork出错了
  cannot_execute(client);
  return;
 }
 if (pid == 0)  /* 子进程child: CGI script */
 {
  char meth_env[255];
  char query_env[255];
  char length_env[255];

  dup2(cgi_output[1], 1);// 输出重定向
  dup2(cgi_input[0], 0);//输入重定向
  close(cgi_output[0]);//提前关闭这两个子进程没用到的管道
  close(cgi_input[1]);
  sprintf(meth_env, "REQUEST_METHOD=%s", method);
  putenv(meth_env);
  if (strcasecmp(method, "GET") == 0) {
   sprintf(query_env, "QUERY_STRING=%s", query_string);
   putenv(query_env);//设定环境变量
  }
  else {   /* POST */
   sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
   putenv(length_env);//设定环境变量
  }
  execl(path, path, NULL);//执行path路径下的path命令,null是参数
  exit(0);
 } else {    /*父进程 parent */
  close(cgi_output[1]);//提前关闭这两个父进程没用到的管道
  close(cgi_input[0]); 
  if (strcasecmp(method, "POST") == 0)//处理POST
   for (i = 0; i < content_length; i++) {
    recv(client, &c, 1, 0); //从client接收数据从内核拷贝至c
    write(cgi_input[1], &c, 1);
   }
  while (read(cgi_output[0], &c, 1) > 0)//从输出里读取到c发送给client
   send(client, &c, 1, 0);

  close(cgi_output[0]);
  close(cgi_input[1]);
  waitpid(pid, &status, 0);//返回子进程结束状态值。 子进程的结束状态值会由参数 status 返回,
 }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
 /**********************************************************************/
/* 
*从socket读取一行，无论是回车，换行还是回车换行，读到一个空结束
*如果在buffer的末尾之前读到换行符，字符串空结束
*int recv( _In_ SOCKET s, _Out_ char *buf, _In_ int len(buf长度), _In_ int flags（指定调用方式）);

成功执行时，返回接收到的字节数。
另一端已关闭则返回0。
失败返回-1，
*/
/**********************************************************************/
int get_line(int sock, char *buf, int size)
{
 int i = 0;
 char c = '\0';
 int n;
 printf("%d\n",size);
 //int ntimeout = 1000;
 struct timeval timeout = {3,0}; 
 setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,(char *)&timeout,sizeof(struct timeval));
 while ((i < size - 1) && (c != '\n'))
 {
  //recv函数在建立连接后把接收的数据copy到buf中
  n = recv(sock, &c, 1, 0);  //注意如果恰好此时sock中没有数据，那么将阻塞于此
  /* DEBUG printf("%02X\n", c); */
  printf("1 %c\n ", c);
  printf("The recv get %d\n ", n);
  if (n > 0)
  {
   if (c == '\r')
   {
    n = recv(sock, &c, 1, MSG_PEEK);
    /* DEBUG printf("%02X\n", c); */
    printf("2 %c\n", c); 
    if ((n > 0) && (c == '\n'))
     recv(sock, &c, 1, 0);
    else
     c = '\n';
   }
   buf[i] = c;
   i++;
  }
  else
   c = '\n';
 }
 buf[i] = '\0';
 printf("get_line return %d\n",i);
 return(i);//接收的数据并返回copy到buf的数据长度
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/*返回请求的HTTP头部，200表示请求成功*/
/**********************************************************************/
void headers(int client, const char *filename)
{
 char buf[1024];
 (void)filename;  /* could use filename to determine file type */

 strcpy(buf, "HTTP/1.0 200 OK\r\n");
 send(client, buf, strlen(buf), 0);
 strcpy(buf, SERVER_STRING);
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-Type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 strcpy(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/*404网页未找到错误*/
/**********************************************************************/
void not_found(int client)
{
 char buf[1024];

 sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, SERVER_STRING);
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-Type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "your request because the resource specified\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "is unavailable or nonexistent.\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "</BODY></HTML>\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
 /*发送一个常规文件给客户端*/
/**********************************************************************/
void serve_file(int client, const char *filename)
{
 FILE *resource = NULL;
 int numchars = 1;
 char buf[1024];

 buf[0] = 'A'; buf[1] = '\0';
 while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers ？？干嘛的？*/
  numchars = get_line(client, buf, sizeof(buf));

 resource = fopen(filename, "r");//读取文件
 if (resource == NULL)
  not_found(client);//告诉client请求的文件未找到
 else
 {
  headers(client, filename);//将头部发给client
  cat(client, resource);//提取文件内容发送到client
 }
 fclose(resource);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
 /*这个函数主要是监听指定端口的web连接，如果端口是0动态分布的一个端口
 修改原始端口为实际端口 */
/**********************************************************************/
int startup(u_short *port)
{
 int httpd = 0;
 struct sockaddr_in name;

 httpd = socket(PF_INET, SOCK_STREAM, 0);//创建套接口描述字
 if (httpd == -1)
  error_die("socket");
 memset(&name, 0, sizeof(name));//全置为0
 name.sin_family = AF_INET;
 name.sin_port = htons(*port);
 name.sin_addr.s_addr = htonl(INADDR_ANY);
 if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)//将本地协议地址赋给套接口
  error_die("bind");
 if (*port == 0)  /* if dynamically allocating a port 如果端口为0动态分配*/
 {
  socklen_t namelen = sizeof(name);
  if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
   error_die("getsockname");
  *port = ntohs(name.sin_port);//网络字节序转为主机字节序
 }
 if (listen(httpd, 5) < 0)//转换为被动套接口并规定套接口排队的最大连接个数
  error_die("listen");
 return(httpd);
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
 char buf[1024];

 sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, SERVER_STRING);
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-Type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "</TITLE></HEAD>\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "</BODY></HTML>\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void)
{
 int server_sock = -1;
 u_short port = 0;
 //int client_sock = -1;
 struct sockaddr_in client_name;
 socklen_t client_name_len = sizeof(client_name);
 pthread_t newthread;

 server_sock = startup(&port);
 printf("httpd running on port %d\n", port);

 int *client_sock;
 while (1)
 {
  /*若果accept成功返回一个内核自而动生成的一个全新的描述字，代表与所返回的客户的TCP连接*/
  printf("now client_sock is %d\n",*client_sock);
  client_sock = (int *)malloc(sizeof(int));
  *client_sock = accept(server_sock,
                       (struct sockaddr *)&client_name,
                       &client_name_len);
  printf("current socket is %d\n",*client_sock);
  if (*client_sock == -1)
       error_die("accept");

 /* accept_request(client_sock); */
 /*pthread_create第一个参数为指向线程标识符的指针。
第二个参数用来设置线程属性。
第三个参数是线程运行函数的起始地址。
最后一个参数是运行函数的参数*/
 void *acceptfunc = &accept_request;
 void *clientsock = client_sock; 
 if (pthread_create(&newthread , NULL, acceptfunc, clientsock) != 0)
   perror("pthread_create");
 }

 close(server_sock);

 return(0);
}
