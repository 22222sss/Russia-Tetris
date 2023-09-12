#include"Server.h"
#include"Tetris.h"
#include"UserInfo.h"

extern Block blockDefines[7][4];//���ڴ洢7�ֻ�����״����ĸ��Ե�4����̬����Ϣ����28��

extern map<int, UserInfo*> g_playing_gamer;


int main() {
    signal(SIGPIPE, SIG_IGN);  // ����SIGPIPE�ź�
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        printf("create socket Error: %s (errno: %d)\n", strerror(errno), errno);
        return -1;
    }
    // ��Ӧα�����е�bind(sockfd, ip::port��һЩ����);
    struct sockaddr_in addr;    // ���ڴ��ip�Ͷ˿ڵĽṹ
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(DEFAULT_PORT);
    if (-1 == bind(serverSocket, (struct sockaddr*)&addr, sizeof(addr)))
    {
        close(serverSocket);
        printf("bind Error: %s (errno: %d)\n", strerror(errno), errno);
        return -1;
    }
    // ��Ӧα�����е�listen(sockfd);    
    if (-1 == listen(serverSocket, MAXLINK))
    {
        close(serverSocket);
        printf("listen Error: %s (errno: %d)\n", strerror(errno), errno);
        return -1;
    }

    int epollfd = epoll_create(MAXSIZE);
    if (epollfd < 0)	//����epollʵ��
    {
        close(serverSocket);
        printf("epoll_create Error: %s (errno: %d)\n", strerror(errno), errno);
        exit(-1);
    }

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = serverSocket;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, serverSocket, &event) == -1)
    {
        printf("add server socket to epoll Error: %s (errno: %d)\n", strerror(errno), errno);
        close(serverSocket);
        close(epollfd);
        return -1;
    }

    // ������ʱ��
    int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
    struct itimerspec timer_spec;
    timer_spec.it_interval.tv_sec = 0;  // �����ظ�����
    timer_spec.it_interval.tv_nsec = 0;
    timer_spec.it_value.tv_sec = 1;     // ��ʼ��ʱһ��
    timer_spec.it_value.tv_nsec = 0;
    timerfd_settime(timerfd, 0, &timer_spec, NULL);


    // ����ʱ������ epoll ��������
    struct epoll_event timer_event;
    timer_event.events = EPOLLIN;
    timer_event.data.fd = timerfd;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, timerfd, &timer_event) == -1)
    {
        printf("add timerfd to epoll Error: %s (errno: %d)\n", strerror(errno), errno);
        close(serverSocket);
        close(epollfd);
        return 1;
    }

    printf("======waiting for client's request======\n");

    Server server(serverSocket, epollfd, timerfd);

    server.handleNewClientConnection();

    server.Run();
    return 0;
}