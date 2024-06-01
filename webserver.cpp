#include "webserver.h"

WebServer::WebServer()
{
    //http_conn类对象
    users = new http_conn[MAX_FD];      

    //root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);   // getcwd用于获取当前工作目录的绝对路径
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);   // 用于两个字符串的拼接，将一个字符串追加到另一个字符串的末尾

    //定时器
    users_timer = new client_data[MAX_FD];
}
// 析构函数释放资源。
WebServer::~WebServer() 
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

// 初始化
void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

// 设置触发模式
void WebServer::trig_mode()
{
    //LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write()
{
    if (0 == m_close_log)
    {
        //初始化日志
        if (1 == m_log_write)
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

void WebServer::sql_pool()
{
    //初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    //初始化数据库读取表
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool()
{
    //线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

// 事件监听函数：设置网络编程基础步骤和epoll事件
void WebServer::eventListen()
{
    //网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0); // 参数含义：底层协议族、服务类型（TCP/UDP）、具体协议
    assert(m_listenfd >= 0); // 断言

    //优雅关闭连接
    if (0 == m_OPT_LINGER)
    {
        struct linger tmp = {0, 1};
        // setsockopt 是一个用于设置套接字选项的系统调用，它允许你对套接字的行为进行各种配置。
        // 参数：要设置选项的套接字文件描述符、指定选项的协议层次、指向包含新选项值的缓冲区的指针、缓冲区的长度（字节为单位）
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));   // 用于将内存块中的数据全部清零
    address.sin_family = AF_INET;       // TCP/IPv4地址族
    // 如htonl表示“host to network long”，即将长整型（32 bit）的主机字节序数据转化为网络字节序数据。
    address.sin_addr.s_addr = htonl(INADDR_ANY);    
    address.sin_port = htons(m_port);   // 短整形

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    // bind将address所指的socket地址分配给未命名的m_listenfd文件描述符，addrlen参数指出该socket地址的长度。
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));   // bind成功时返回0，失败则返回-1并设置errno
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);  // 参数：被监听的socket、内核监听队列的最大长度。listen成功时返回0，失败则返回-1并设置errno。
    assert(ret >= 0);

    utils.init(TIMESLOT);   // 通常用于设置定时器的时间间隔。

    //epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];   // 定义一个 epoll_event 数组，用于存储 epoll 事件。
    m_epollfd = epoll_create(5);    // 创建一个 epoll 实例，5 是给内核的提示，表示预期的事件数量（这个值一般不会影响实际性能）。
    assert(m_epollfd != -1);    // 检查 epoll_create 是否成功创建了 epoll 文件描述符。如果失败，程序会中止。

    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);  // 将监听文件描述符 m_listenfd 添加到 epoll 实例中。这里的 false 表示是否启用边缘触发，
    http_conn::m_epollfd = m_epollfd;  // 将 m_epollfd 赋值给 http_conn 类的静态成员变量 m_epollfd，使得 http_conn 类的所有实例都能使用这个 epoll 文件描述符。

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);  // 创建一对相互连接的套接字，用于进程间通信。m_pipefd[0] 和 m_pipefd[1] 分别是这对套接字的文件描述符。
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);  // 将 m_pipefd[1] 设置为非阻塞模式。
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);  // 将 m_pipefd[0] 添加到 epoll 实例中，监视其读事件。
    // 设置信号处理
    utils.addsig(SIGPIPE, SIG_IGN);     // 忽略 SIGPIPE 信号。当向一个已关闭的 socket 写数据时，系统会发送 SIGPIPE 信号给进程，默认行为是终止进程。这里通过忽略该信号来防止进程被终止。
    utils.addsig(SIGALRM, utils.sig_handler, false);  // 将 SIGALRM 信号与 utils.sig_handler 处理函数关联。false 表示不重启系统调用。
    utils.addsig(SIGTERM, utils.sig_handler, false);  // 将 SIGTERM 信号与 utils.sig_handler 处理函数关联。false 表示不重启系统调用。

    alarm(TIMESLOT);  // 启动定时器

    // alarm函数会定期触发SIGALRM信号，这个信号交由sig_handler来处理，
    // 每当监测到有这个信号的时候，都会将这个信号写到pipefd[1]里面，传递给主循环(信号处理dealwithsignal)：

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd; // 使得工具类中的函数可以访问这个管道文件描述符。
    Utils::u_epollfd = m_epollfd; // 使得工具类中的函数可以访问这个 epoll 文件描述符。
}

void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    // 初始化http连接
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    // 创建定时器
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;   // 设置定时器的回调函数 cb_func。
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;  // 设置定时器的过期时间为当前时间加上 3 * TIMESLOT 秒。
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;  // 获取当前时间，将定时器的过期时间延长 3 * TIMESLOT 秒。
    utils.m_timer_lst.adjust_timer(timer);  // 调用工具类 utils 中的定时器链表的 adjust_timer 方法，根据新的过期时间调整定时器在链表中的位置。

    LOG_INFO("%s", "adjust timer once");
}

void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);   // 调用定时器的回调函数 cb_func，传入 users_timer[sockfd] 作为参数。通常情况下，这个回调函数用于处理连接超时等事件。
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer); // 从链表中删除该定时器
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

bool WebServer::dealclientdata()
{   
    // 定义客户端地址的结构client_address和其长度client_addrlength
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    if (0 == m_LISTENTrigmode)      // LT 模式
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0) // 是否连接成功
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD) // 检查用户数是否达到最大值MAX_FD
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address); // 初始化新连接的定时器
    }
    else    // ET 模式
    {
        while (1)   // 在 ET 模式下，使用循环接受连接，直到 accept 返回小于 0 的值 或 到达最大连接数量。
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    // recv读取sockfd上的数据，buf和len参数分别指定读缓冲区的位置和大小，flags参数的含义见后文，通常设置为0即可。
    // recv可能返回0，这意味着通信对方已经关闭连接了。recv出错时返回-1并设置errno。
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else    // 处理接受的信号
    { 
        // 当我们在读端pipefd[0]读到这个信号的的时候，就会将timeout变量置为true并跳出循环，
        // 让timer_handler()函数取出来定时器容器上的到期任务，该定时器容器是通过升序链表来实现的，
        // 从头到尾对检查任务是否超时，若超时则调用定时器的回调函数cb_func()，关闭该socket连接，
        // 并删除其对应的定时器del_timer。
        for (int i = 0; i < ret; ++i)   // 遍历接收到的信号数组，根据不同的信号类型执行相应操作。
        {
            switch (signals[i])
            {
            case SIGALRM:   // SIGALRM：设置 timeout 为 true。
            {
                timeout = true;
                break;
            }
            case SIGTERM:   // SIGTERM：设置 stop_server 为 true。
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

void WebServer::dealwithread(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;  // 获取定时器

    //reactor   Reactor 模式处理
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);    // 调整定时器
        }

        //若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);

        while (true)    // 循环等待 users[sockfd].improv 状态改变。
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)  // 如果设置了定时器标志，则处理定时器。
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor Proactor 模式处理
        if (users[sockfd].read_once())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //若监测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;  // 获取定时器
    //reactor 模式
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);   // 调整定时器
        }

        m_pool->append(users + sockfd, 1);  // 将写事件加入线程池请求队列

        while (true)
        {
            if (1 == users[sockfd].improv)  // 循环等待 users[sockfd].improv 状态改变。
            {
                if (1 == users[sockfd].timer_flag) // 如果设置了定时器标志，则处理定时器。
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].write())  // 直接写数据
        {   
            // 写成功、记录日志并调整定时器
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));  

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);  // 写入失败，处理定时器
        }
    }
}
// Web 服务器的主事件循环，负责监听和处理各种事件，包括新连接、读写事件、定时事件和信号处理。
void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {   
        // 等待epoll事件发生，参数：epoll文件描述符、用于存储发生的事件、可以处理的最大事件数量、-1阻塞等待
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)   // = EINTR 系统调用被中断
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)    // 遍历所有发生的事件，获取每个事件对应的文件描述符 sockfd。
        {
            int sockfd = events[i].data.fd;

            //处理新到的客户连接
            if (sockfd == m_listenfd) // // 如果事件文件描述符是监听套接字 m_listenfd，调用 dealclientdata 函数处理新连接。
            {
                bool flag = dealclientdata();
                if (false == flag)
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) // 如果事件包含 EPOLLRDHUP（对端关闭连接）、EPOLLHUP（挂起）或 EPOLLERR（错误），则表示连接出错或关闭。
            {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //处理信号
            // 如果事件文件描述符是信号管道 m_pipefd[0]，且事件是 EPOLLIN（可读），调用 dealwithsignal 函数处理信号。
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)    // 如果事件是 EPOLLOUT（可写），调用 dealwithwrite 函数处理写事件。
            {
                dealwithread(sockfd);
            }
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        if (timeout)
        {
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}
