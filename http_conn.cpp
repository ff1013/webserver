#include"http_conn.h"

// 定义HTTP响应的一些状态信息
//200:一切正常,一般用于相应GET和POST请求
//400:请求错误
//403：禁止访问
//404：未找到
//500：服务器错误
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

//网站根目录
const char* doc_root="/root/webserver/resources";

//设置文件描述符非阻塞
int setnonblocking(int fd){
    //非阻塞I/O使我们的操作要么成功，要么立即返回错误，不被阻塞。
    int old_flag=fcntl(fd,F_GETFL);
    int new_flag=old_flag|O_NONBLOCK;
    fcntl(fd,F_SETFL,new_flag);
    return old_flag;
}

//向epoll中添加需要监听的文件描述符,epolloneshot事件实现一个socket任何时刻都只被一个线程处理
void addfd(int epollfd,int fd,bool one_shot){
    epoll_event event;
    event.data.fd=fd;
    event.events=EPOLLIN|EPOLLRDHUP;//EPOLLIN对端有数据写入，EPOLLRDHUP代表对端断开连接//水平触发？

    if(one_shot){
        //防止同一个通信被不同的线程处理
        event.events|=EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);//追加事件
    //设置文件描述符非阻塞
    setnonblocking(fd);
}

//向epoll中移除监听的文件描述符
void removefd(int epollfd,int fd){
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);//移除
    close(fd);//移除后关闭文件描述符
}

//修改文件描述符，重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLL事件能够被触发
void modfd(int epollfd,int fd,int ev){
    epoll_event event;
    event.data.fd=fd;
    event.events=ev|EPOLLET|EPOLLONESHOT|EPOLLRDHUP;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

//静态成员要初始化
int http_conn::m_epollfd =-1;//所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
int http_conn::m_user_count=0;//所有客户数

//初始化新接收的连接，外部调用初始化套接字地址
void http_conn::init(int sockfd,const sockaddr_in & addr){
    m_sockfd=sockfd;
    m_address=addr;

    //端口复用
    int reuse=1;//当reuse是1时进行复用
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    //添加到epoll对象中
    addfd(m_epollfd,m_sockfd,true);
    m_user_count++;//总用户数加一

    init();//初始化状态
}
//初始化状态
void http_conn::init(){
    m_check_state=CHECK_STATE_REQUESTLINE;//初始化状态为解析请求首行
    m_linger=false;//默认不保持连接【Connection : keep-alive保持连接】
    m_method=GET;//默认请求方式为GET

    m_checked_index=0;
    m_start_line=0;
    m_read_index=0;
    m_url=0;
    m_version=0;
    m_content_length = 0;
    m_host = 0;
    m_write_index = 0;
    bytes_to_send=0;
    bytes_have_send=0;

    bzero(m_read_buf,READ_BUFFER_SIZE);//初始化清零
    bzero(m_write_buf, READ_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

//关闭连接
void http_conn::close_conn(){
    if(m_sockfd!=-1){
        removefd(m_epollfd,m_sockfd);
        m_sockfd=-1;
        m_user_count--;//关闭一个连接，客户总数量减一
    }
}

//循环读取客户数据，直到无数据可读或者对方关闭连接
bool http_conn::read(){
    if(m_read_index>=READ_BUFFER_SIZE){//超出缓冲区大小
        return false;
    }

    //读取到的字节
    int bytes_read=0;
    while (true)
    {
        // 从m_read_buf+m_read_index索引处开始保存数据，大小是READ_BUFFER_SIZE-m_read_index
        bytes_read=recv(m_sockfd,m_read_buf+m_read_index,READ_BUFFER_SIZE-m_read_index,0);
        if(bytes_read==-1){
            if(errno==EAGAIN||errno==EWOULDBLOCK){
                //没有数据
                break;
            }
            return false;
        }
        else if(bytes_read==0){
            //对方关闭连接
            return false;
        }
        m_read_index+=bytes_read;//更新标识
    }
    printf("读取到了数据：%s\n",m_read_buf);
    return true;
}

//解析一行，判断依据\r\n【\r回车、\n换行】
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for(;m_checked_index<m_read_index;++m_checked_index){//不超出已经读入的
        temp =m_read_buf[m_checked_index];
        if(temp=='\r'){
            if((m_checked_index+1)==m_read_index){
                return LINE_OPEN;//行数据尚不完整
            }
            else if(m_read_buf[m_checked_index+1]=='\n'){
                m_read_buf[m_checked_index++]= '\0';//数组结束符\0
                m_read_buf[m_checked_index++]='\0';//数组结束符\0
                return LINE_OK;//成功读取一行
            }
            return LINE_BAD;//出错
        }
        else if(temp=='\n'){
            if((m_checked_index>1)&&(m_read_buf[m_checked_index-1]=='\r')){
                m_read_buf[m_checked_index-1]='\0';//数组结束符\0
                m_read_buf[m_checked_index++]='\0';//数组结束符\0
                return LINE_OK;//成功读取一行
            }
            return LINE_BAD;//出错
        }
    }
    return LINE_OPEN;//行数据尚不完整
}

//主状态机，解析请求
//解析HTTP请求
http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_state=LINE_OK;
    HTTP_CODE ret=NO_REQUEST;

    char *text=0;
    while (((m_check_state==CHECK_STATE_CONTENT)&&(line_state==LINE_OK))
       ||(line_state=parse_line())==LINE_OK){
        //解析到了一行完整的数据，或者解析到了请求体，也是完整的数据
        
        //获取一行数据
        text=get_line();
        
        m_start_line=m_checked_index;
        printf("获得1行http行:%s\n",text);

        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:
            {
                ret=parse_request_line(text);
                if(ret==BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret=parse_headers(text);
                if(ret==BAD_REQUEST){
                    return BAD_REQUEST;
                }
                else if (ret==GET_REQUEST)
                {
                    return do_request();
                }
            }
            case CHECK_STATE_CONTENT:
            {
                ret=parse_content(text);
                if (ret==GET_REQUEST)
                {
                    return do_request();
                }
                line_state=LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}
//解析请求首行，获得请求方法、目标URL、HTTP版本
http_conn::HTTP_CODE http_conn::parse_request_line(char *text){
    //GET /index.html HTTP/1.1
    m_url=strpbrk(text," \t");//检索两个字符串中首个相同字符的位置[m_url= /index.html HTTP/1.1]
    if (!m_url) { 
        return BAD_REQUEST;
    }
    //GET\0/index.html HTTP/1.1
    *m_url++='\0';//[m_url=/index.html HTTP/1.1]
    
    char* method=text;
    if(strcasecmp(method,"GET")==0){
        m_method=GET;
    }
    else{
        return BAD_REQUEST;
    }

    //m_url=/index.html HTTP/1.1
    m_version=strpbrk(m_url," \t");//[m_version= HTTP/1.1]
    if(!m_version){
        return BAD_REQUEST;
    }
    //m_version=/index.html\0HTTP/1.1
    *m_version++='\0';//[m_version=HTTP/1.1]
    if(strcasecmp(m_version,"HTTP/1.1")!=0){
        return BAD_REQUEST;
    }

    //http://192.168.1.1:10000/index.html
    if(strncasecmp(m_url,"http://",7)==0){
        m_url+=7;//192.168.1.1:10000/index.html
        m_url=strchr(m_url,'/');// /index.html
    }

    if(!m_url||m_url[0]!='/'){
        return BAD_REQUEST;
    }

    m_check_state=CHECK_STATE_HEADER;//主状态机检测状态变成检查请求头
    return NO_REQUEST;//检查不完整
}
//解析请求头，获得
http_conn::HTTP_CODE http_conn::parse_headers(char *text){
    //遇到空行，表示头部字段解析完毕
    if(text[0]=='\0'){
        //如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体
        //状态机转移到CHECK_STATE_CONTENT状态
        if(m_content_length!=0){
            m_check_state=CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        //否则说明已经得到一个完整的HTTP请求
        return GET_REQUEST;
    }
    //处理Connection字段 Connection: keep-alive
    else if(strncasecmp(text,"Connection:",11)==0){
        text+=11;
        text+=strspn(text," \t");
        if(strcasecmp(text,"keep-alive")==0){
            m_linger=true;
        }
    }
    //处理Content-Length头部字段
    else if(strncasecmp(text,"Content-Length:",15)==0){
        text+=15;
        text+=strspn(text," \t");
        m_content_length=atol(text);
    }
    //处理Host头部字段
    else if(strncasecmp(text,"Host:",5)==0){
        text+=5;
        text+=strspn(text," \t");
        m_host=text;
    }
    else{
        printf("oop! unknow header %s\n",text);
    }
    return NO_REQUEST;
}
//解析请求体,没有真正解析，只是判断是否完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text){
    if(m_read_index>=(m_content_length+m_checked_index)){
        text[m_content_length]='\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//获取请求的资源文件,使用mmap进行文件内存映射
http_conn::HTTP_CODE http_conn::do_request(){
//当得到一个完整的、正确的HTTP请求时，我们就分析目标文件的属性
//如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其映射到内存地址m_file_address处，并告诉调用者获取文件成功

    //  "/root/webserver/resources"
    strcpy(m_real_file,doc_root);
    int len=strlen(doc_root);
    strncpy(m_real_file+len,m_url,FILENAME_LEN-len-1);
    //获取m_real_file文件的相关的状态信息，-1失败，0成功
    if(stat(m_real_file,&m_file_stat)<0){
        printf("没有该资源");
        return NO_RESOURCE;//没有该资源
    }
    //判断访问权限
    if(!(m_file_stat.st_mode&S_IROTH)){
        printf("禁止访问");
        return FORBIDDEN_REQUEST;//禁止访问
    }
    //判断是否是目录
    if(S_ISDIR(m_file_stat.st_mode)){
        printf("不是目录，错误的请求");
        return BAD_REQUEST;//错误的请求
    }
    //以只读方式打开文件
    int fd=open(m_real_file,O_RDONLY);
    //创建内存映射
    //mmap函数用于申请一段内存空间，我们可以将这段内存作为进程间通信的共享内存，也可以将文件直接映射到其中。
    m_file_address=(char*)mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    close(fd);
    return FILE_REQUEST;//请求文件获取成功
}
//对内存映射区执行munmap操作,munmap函数用于释放mmap创建的内存空间。
void http_conn::unmap(){
    if(m_file_address){
        munmap(m_file_address,m_file_stat.st_size);
        m_file_address=0;
    }
}

//非阻塞写HTTP响应
bool http_conn::write(){
   int temp = 0;
    int bytes_have_send = 0;    //已经发送的字节
    int bytes_to_send = m_write_index;//将要发送的字节【m_write_index：写缓冲区中待发送的字节数】
    
    if ( bytes_to_send == 0 ) {
        //将要发送的字节数为0，说明这一次响应结束。
        modfd( m_epollfd, m_sockfd, EPOLLIN ); 
        init();
        return true;
    }

    while(1) {
        //分散写
        //分散写writev是为了将内存映射准备的
        //因为对内存映射来说，使用一个大的缓冲区将正文以外的部分和正文拼合在缓冲区中再发送无疑是很不现实的
        //一个是内存映射的大小无法确定，另一个是再次拷贝再发送很占内存，因此使用分散写writev是必须的。
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if ( temp <= -1 ) {
            //如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
         if (bytes_have_send>=m_iv[0].iov_len){
            m_iv[0].iov_len=0;
            m_iv[1].iov_base=m_file_address+(bytes_have_send-m_write_index);
            m_iv[1].iov_len=bytes_to_send;
        }
        else{
            m_iv[0].iov_base=m_write_buf+bytes_have_send;
            m_iv[0].iov_len=m_iv[0].iov_len-temp;
        }
        if (bytes_to_send<=0){
            //没有数据要发送了
            unmap();
            modfd( m_epollfd, m_sockfd, EPOLLIN );
            if(m_linger) {
                init();
                return true;
            } 
            else{
                return false;
            } 
        }
    }
}

// 往写缓冲中写入待发送的数据, 这一组函数被process_write调用以填充HTTP应答。
bool http_conn::add_response( const char* format, ... ) {
    if( m_write_index >= WRITE_BUFFER_SIZE ) {
        return false;//超出范围
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf(m_write_buf+ m_write_index, WRITE_BUFFER_SIZE- 1 - m_write_index, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_index ) ) {
        return false;
    }
    m_write_index += len;
    va_end( arg_list );
    return true;
}
//响应首行
bool http_conn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

//响应头部
bool http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}
//响应头部的Content_Length
bool http_conn::add_content_length(int content_len) {
    //Content-Length: 2
    return add_response( "Content-Length: %d\r\n", content_len );
}
//响应头部的Content-Type
bool http_conn::add_content_type() {
    //Content-Type: text/html
    return add_response("Content-Type:%s\r\n", "text/html");
}
//响应头部的Connection
bool http_conn::add_linger()
{
    //Connection: keep-alive
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}
//响应头部的空行
bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}
//响应体内容
bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}



// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        case INTERNAL_ERROR://内部错误
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST://语法错误
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE://没有资源
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST://禁止访问
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST://文件访问
            add_status_line(200, ok_200_title );
            add_headers(m_file_stat.st_size);
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_index;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send=m_write_index+m_file_stat.st_size;
            return true;
        default:
            return false;
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_index;
    m_iv_count = 1;
    return true;
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process(){
    //解析HTTP请求
    HTTP_CODE read_ret=process_read();
    if(read_ret==NO_REQUEST){//数据不完整，继续修改
        modfd(m_epollfd,m_sockfd,EPOLLIN);
        return;
    }

    //生成响应
    bool write_ret=process_write(read_ret);
    if(!write_ret){
        close_conn();
    }
    modfd(m_epollfd,m_sockfd,EPOLLOUT);
}


