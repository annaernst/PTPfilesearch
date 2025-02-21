#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <ifaddrs.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

void receive_file(int fd, char* file_name) {
    char buffer[512];
    int bytes_received;

    // Open file for writing
    FILE *fp = fopen(file_name, "w");
    if (fp == NULL) {
        perror("Failed to open file\n");
        return;
    }

    // int saved_flags = fcntl(fd, F_GETFL);
    // fcntl(fd, F_SETFL, saved_flags & ~O_NONBLOCK);

    // Receive the file data
    if ((bytes_received = read(fd, buffer, 512)) <= 0) {
        perror("read");
    }
    fprintf(fp, buffer);
    // printf("buffer: %s", buffer);
    // printf("bytes received: %d", bytes_received);
    // if (fwrite(buffer, sizeof(char), bytes_received, fp) <= 0){
    //     printf("frwite error\n");
    // }

    fclose(fp);
    // fcntl(fd, F_SETFL, saved_flags);
    printf("File %s received successfully.\n", file_name);
}

int main(int argc, char *argv[])
{ 

    struct sockaddr_in servaddr;
    int sockfd, listenfd, connfd, searchfd;
    fd_set rfds;

    // read file with peer IPs

    char *nodefilename = "../nodelist.txt";
    const int IP_LENGTH = 50;
    char ips[30][IP_LENGTH]; 
    memset(ips, 0, sizeof(ips));
    char buffer[IP_LENGTH];

    FILE *fp = fopen(nodefilename, "r");
    if (fp == NULL)
    {
        printf("\nError: Could not open %s\n", nodefilename);
        return 1;
    }
    int num_peers = 0;
    while(fgets(buffer, IP_LENGTH, fp))
    {
        for(char* ptr = buffer; *ptr; ptr++)
        {
            if(*ptr == '\n') 
            { 
                *ptr = '\0'; 
                break;
            }
        } 
        strcpy(ips[num_peers], buffer);
        num_peers++;
    }
    num_peers = num_peers/2;
    fclose(fp);

    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        printf("\nError: Could not create listen socket\n");
        return 1;
    }

    // server
    
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    int port = 4999;
    do {
        port++;
        servaddr.sin_port = htons(port);
    } while (bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) == -1);

    // listening for new connections
    if (listen(listenfd, 14) < 0)
    {
        printf("\nError: listen()\n");
    }

    FD_ZERO(&rfds);
    FD_SET(listenfd, &rfds); 
    int connections[14]; // fds of connected peers
    int num_connections = 0;
    memset(connections, 0, sizeof(connections));
    int highest_fd = listenfd;


    // log new peer in the list
    
    struct ifaddrs *ifap, *ifa;
    struct sockaddr_in *sa;
    char *addr;

    getifaddrs (&ifap);
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family==AF_INET && strcmp(ifa->ifa_name,"lo") ) {
            sa = (struct sockaddr_in *) ifa->ifa_addr;
            addr = inet_ntoa(sa->sin_addr);
            printf("Interface: %s\tAddress: %s\n", ifa->ifa_name, addr);
        }
    }

    fp = fopen(nodefilename, "a");
    freeifaddrs(ifap);
     // ip of self

    char* myip = malloc(strlen(addr)+1);
    strcpy(myip,addr);
    fprintf(fp, "%s\n", addr);
    fprintf(fp, "%d\n", port);
    fclose(fp);

    // make fd for stdin for reading search requests
    searchfd = fileno(stdin);
    FD_SET(searchfd, &rfds);

    // client

    if(num_peers != 0) // if it is not the first node in the network, find another node to connect to
    {
        if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            printf("\n Error: Could not create client socket \n");
            return 1;
        }
        // for incoming/outgoing messages
        connections[num_connections] = sockfd;
        num_connections++;
        FD_SET(sockfd, &rfds);
        if (highest_fd < sockfd) {
            highest_fd = sockfd;
        } 
        // pick a peer to connect to
        srand(time(NULL));
        int rand_int = rand() % num_peers;
        char *rand_ip = ips[2 * rand_int];
        
        if (inet_pton(AF_INET, rand_ip, &servaddr.sin_addr) <= 0)
        {
            printf("\nError: inet_pton()\n");
            return 1;
        }
        servaddr.sin_port = htons(atoi(ips[2 * rand_int + 1]));

        if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1)
        {
            printf("\n Error: Could not connect the socket\n");
            return 1;
        }

        printf("Adjacency list:\n");
        printf("%s\n", rand_ip);
    }
    fd_set out_rfds;
    char inputbuffer[30];
    char fileslistbuffer[32];
    int n = 0;
    char messagebuffer[512];
    char messagebuffer_const[512];
    char requestedfile[28];
    char id[11];
    struct timeval tv;
    struct timeval timeout;
    int requests_buf_size = 30;
    char parsed[8][30];
    unsigned long request_timestamp[requests_buf_size];
    int request_fd[30];
    memset(request_fd, 0, sizeof(request_fd));
    int request_ctr = 0;
    char* endptr;
    int state = 0;
    int hops = 0;
    char searchmessagebuffer[512];
    unsigned long expirationtime = -1;
    while(1)
    {
        printf("To initiate file search, type <filename> or <keyword>, e.g. tomato.txt (or <q> to quit)\n");
        
        out_rfds = rfds;
        sleep(0.01);
        int num_requests = -1;
        if (state == 0){
            num_requests = select(highest_fd+1, &out_rfds, NULL, NULL, NULL);
        } else if (state == 1){
            if (gettimeofday(&tv,NULL) < 0){
                perror("gettimeofday:");
            }
            printf("time of day: %lu, %lu", tv.tv_sec, tv.tv_usec);
            unsigned long cur_time = tv.tv_sec * 1000000 + tv.tv_usec;
            printf("cur time: %d\n");
            printf("expiration time: %d\n");
            if (cur_time > expirationtime) {
                //ongoing search request expired
                printf("Request expired\n");
                if (hops <= 15) { // try again with higher hopcount
                    hops = hops * 2;
                    expirationtime = tv.tv_sec * 1000000 + tv.tv_usec + 100000*hops;// in microseconds
                    sprintf(searchmessagebuffer,"0 %lu %s %d %s", tv.tv_sec * 1000000 + tv.tv_usec, myip, hops, requestedfile);
                } else {
                    printf("Requested file not found in the network\n");
                    state = 0;
                    continue;
                }
            }
            timeout.tv_sec = 0;
            timeout.tv_usec = expirationtime - cur_time;
            num_requests = select(highest_fd+1, &out_rfds, NULL, NULL, &timeout);
        }
        if (num_requests < 0)
        {
            perror("select() error");
        } else if (num_requests == 0) {
            printf("Requested file not found in the network (timeout)\n");
            state = 0;
            continue;
        }

        //NEW NEIGHBOR (CONNECTION TO NETWORK)
        if(FD_ISSET(listenfd, &out_rfds)) 
        {
            // accept will not block here so call it & put into connections
            connfd = accept(listenfd, (struct sockaddr*)NULL, NULL);
            connections[num_connections] = connfd;
            num_connections++;
            FD_SET(connfd, &rfds);
            if (highest_fd < connfd) {
                highest_fd = connfd;
            } 
            // print the adjacency list using getpeername
            struct sockaddr *peeraddr;
            peeraddr = malloc(1024);
            socklen_t bufsize = (socklen_t)1024;
            
            
            printf("Adjacency list:\n");
            for(int i = num_connections-1; i >= 0; i--){
                socklen_t tmp = bufsize;
                if (getpeername(connections[i], peeraddr, &tmp) < 0){
                    perror("Error while looking up peername\n");
                }
                
                printf("%s\n", inet_ntoa(((struct sockaddr_in *)peeraddr)->sin_addr));
            }
            free(peeraddr);
        }

        // INCOMING MESSAGE
        for(int i = 0; i < num_connections; i++)
        {
            // check if this connection is selected
            if (!(FD_ISSET(connections[i], &out_rfds))) {continue;}//if it's not set, no message here

            memset(messagebuffer, 0, sizeof(messagebuffer));
            if ((n = read(connections[i], messagebuffer, sizeof(messagebuffer) - 1)) < 0)
            {
                printf("Error: reading message from socket\n");
            }
            if (n <= 1) { continue; } // do not read in messages that are too small (it's junk metadata)
            printf("message read: %s, # bytes = %d\n", messagebuffer, n);
            memcpy(messagebuffer_const,messagebuffer,sizeof(messagebuffer));// because parsing mutates messagebuffer
            //parse
            char* token = strtok(messagebuffer, " ");
            int parsed_ctr = 0;
            while(token != NULL)
            {
                strncpy(parsed[parsed_ctr++],token,29);
                token = strtok(NULL, " ");
            }

            //act on the message
            if (parsed[0][0]=='0')
            {
                // search request
                // make sure i haven't already processed this id
                printf("search request\n");
                int flag = 0;
                for (int b = 0; b < 30; b++){
                    if (request_timestamp[b] == strtoul(parsed[1], &endptr, 10))
                    {
                        printf("already processed request with this timestamp\n");
                        flag = 1;
                    }
                }
                if (flag == 1){
                    continue;
                }

                request_timestamp[request_ctr % requests_buf_size] = strtoul(parsed[1], &endptr, 10);
                request_fd[request_ctr % requests_buf_size] = connections[i];
                request_ctr++;

                //check that the request hasn't expired yet
                gettimeofday(&tv,NULL);
                unsigned long cur_time = tv.tv_sec * 1000000 + tv.tv_usec;
                // printf("current time:   %lu\n", cur_time);
                // printf("initiated time: %lu\n", strtoul(parsed[1], &endptr, 10));
                
                if (cur_time > (strtoul(parsed[1], &endptr, 10) + 10000*16)) {
                    printf("expired\n");
                    continue;}
                //then see if i have the file; if yes, initiate response

                FILE *fp = fopen("files.txt", "r"); 
                if (fp == NULL)
                {
                    printf("\nError: Could not open %s\n", nodefilename);
                    return 1;
                }
                flag = 0;
                while(fgets(fileslistbuffer, 50, fp))
                {
                    for(char* ptr = fileslistbuffer; *ptr; ptr++)
                    {
                        if(*ptr == '\n') 
                        { 
                            *ptr = '\0'; 
                            break;
                        }
                    }
                    token = strtok(fileslistbuffer, " "); //filename
                    // printf("I have file name '%s' and I am looking for '%s'\n",token,parsed[4]);
                    if ((strcmp(token, parsed[4])==0) || (strcmp(strtok(NULL," "), parsed[4])==0))
                    {
                        //file found - initiate response message
                        printf("File found on this machine!\n");
                        memset(messagebuffer, 0, sizeof(messagebuffer));
                        sprintf(messagebuffer,"1 %s %s %s %d", parsed[1], myip, token, port);
                        write(connections[i], messagebuffer, sizeof(messagebuffer));
                        flag = 1;
                        break;
                        printf("end of if statement\n");
                    }
                }
                if (flag == 0)
                {
                    // no file found - forward request to neighbors
                    for(int d = 0; d < num_connections; d++)
                    {
                        if (d != i){
                            write(connections[d], messagebuffer_const, sizeof(messagebuffer_const));
                        }
                    }
                }
                fclose(fp);
            } else if (parsed[0][0]=='1'){
                // response rerout (file found - there is no response if files are not found)
                printf("response request\n");
                //find the request by timestamp id
                int index = -1;
                for (int b = 0; b < 30; b++){
                    if (request_timestamp[b] == strtoul(parsed[1], &endptr, 10))
                    {
                        index = b;
                    }
                }
                if (request_fd[index] == 0 && state == 1){
                    // this is a response for me
                    if (state == 1){
                        //TODO: set up connection to the ip in parse[2] to download the file
                        if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
                        {
                            printf("\n Error: Could not create downloading socket\n");
                            return 1;
                        }
                        
                        if (inet_pton(AF_INET,parsed[2], &servaddr.sin_addr) <= 0)
                        {
                            printf("\nError: inet_pton() for download socket\n");
                            return 1;
                        }
                        servaddr.sin_port = htons(atoi(parsed[4]));

                        if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1)
                        {
                            printf("\n Error: Could not connect the download socket\n");
                            return 1;
                        }

                        // send download request message
                        memset(searchmessagebuffer, 0, sizeof(searchmessagebuffer));
                        sprintf(searchmessagebuffer,"2 %s", parsed[3]);
                        write(sockfd, searchmessagebuffer, sizeof(searchmessagebuffer));
                        receive_file(sockfd, parsed[3]);
                        close(sockfd);
                        state = 0;
                        gettimeofday(&tv, NULL);
                        printf("It took %d hops and %lu total microseconds (found at machine %s)\n", hops, tv.tv_sec * 1000000 + tv.tv_usec - strtoul(parsed[1], NULL, 10), parsed[2]);
                    } else {
                        printf("I received a response I wasn't looking for\n");
                    }
                } else {
                    //rerout the received message to the fd corresponding to that timestamp (i.e. where the original search was received from)
                    write(request_fd[index], messagebuffer_const, sizeof(messagebuffer_const));
                }
            } else if (parsed[0][0] == '2') {
                // file download request
                printf("sending file\n");
                const char *file_path = parsed[1];
                const char *file_name = strrchr(file_path, '/');
                if (!file_name) {
                    file_name = file_path;  // No directory part
                } else {
                    file_name++;  // Move past '/'
                }

                // Open the file for reading
                FILE *fp = fopen(file_path, "rb");
                if (fp == NULL) {
                    perror("Failed to open file");
                    return 1;
                }

                // Send the file data
                char buffer[512];
                size_t bytes_read;
                while ((bytes_read = fread(buffer, sizeof(char), 512, fp)) > 0) {
                    send(connections[i], buffer, bytes_read, 0);
                }

                fclose(fp);
                printf("File %s sent successfully.\n", file_name);
                // remove connection[i] fd & close the connection
                FD_CLR(connections[i], &rfds);
                close(connections[i]);
                num_connections--;
                for (int l = i; l < num_connections; l++){
                    connections[l] = connections[l+1];
                }
            } else if (parsed[0][0] == '3') {
                printf("terminate connection msg: %s\n", messagebuffer_const);
                // closing connection (a p2p network connection, not a file downloading connection)
                if (strcmp(parsed[1], "none") == 0){
                    memset(inputbuffer, 0, sizeof(inputbuffer));
                    sprintf(inputbuffer, "%s %d", myip, port);
                    write(connections[i], inputbuffer, sizeof(inputbuffer));
                } else {
                    // I've been selected as the adoptive parent of a bunch of nodes and now need to create socket connections to all of them
                    for(int g = 1; g < parsed_ctr; g+=2){
                        if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
                        {
                            printf("\n Error: Could not create client socket \n");
                            return 1;
                        }
                        connections[num_connections] = sockfd;
                        num_connections++;
                        FD_SET(sockfd, &rfds);
                        if (highest_fd < sockfd) {
                            highest_fd = sockfd;
                        }  

                        printf("parsed[g] %s\n", parsed[g]);
                        
                        if (inet_pton(AF_INET, parsed[g], &servaddr.sin_addr) <= 0)
                        {
                            printf("\nError: inet_pton()\n");
                            return 1;
                        }
                        servaddr.sin_port = htons(atoi(parsed[g+1]));

                        if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1)
                        {
                            printf("\n Error: Could not connect the socket\n");
                            return 1;
                        }
                    }
                }
                FD_CLR(connections[i], &rfds);
                close(connections[i]);
                num_connections--;
                for (int l = i; l < num_connections; l++){
                    connections[l] = connections[l+1];
                }
                printf("Adjacency list:\n");
                struct sockaddr *peeraddr;
                peeraddr = malloc(1024);
                socklen_t bufsize = (socklen_t)1024;
                for(int i = num_connections-1; i >= 0; i--){
                    socklen_t tmp = bufsize;
                    if (getpeername(connections[i], peeraddr, &tmp) < 0){
                        perror("Error while looking up peername\n");
                    }
                    
                    printf("%s\n", inet_ntoa(((struct sockaddr_in *)peeraddr)->sin_addr));
                }
            } else {
                printf("Could not parse message: %s", messagebuffer_const);
            }
            
        }

        // NEW SEARCH REQUEST INITIATED (or QUIT)
        if(FD_ISSET(searchfd, &out_rfds)) 
        {
            memset(inputbuffer, 0, sizeof(inputbuffer));
            if ((n = read(searchfd, inputbuffer, sizeof(inputbuffer) - 1)) < 0)
            {
                printf("Error: reading command line input\n");
                return 1;
            }

            if(inputbuffer[0] == 'q' && inputbuffer[1] == '\n') {
                //quitting
                //remove self from list of nodes
                FILE *fp = fopen(nodefilename, "r");
                FILE *fp2 = fopen("temp.txt", "w");
                if (fp == NULL)
                {
                    printf("\nError: Could not open %s\n", nodefilename);
                    return 1;
                }
                if (fp2 == NULL) {
                    printf("\nError: Could not open temp.txt\n");
                    return 1;
                }
                // read every line into the buffer
                while(fgets(buffer, IP_LENGTH, fp))
                {
                    char backup_buffer[512];
                    strcpy(backup_buffer, buffer);
                    for(char* ptr = buffer; *ptr; ptr++)
                    {
                        if(*ptr == '\n') 
                        { 
                            *ptr = '\0'; 
                            break;
                        }
                    } 
                    if (strcmp(buffer, myip) != 0){
                        fputs(backup_buffer, fp2);
                    } else {
                        fgets(backup_buffer, IP_LENGTH, fp); // skips the next line, too (corresponding port #)
                    }
                }
                fclose(fp2);
                fclose(fp);
                unlink(nodefilename);
                rename("temp.txt", nodefilename);
                //if have > 1 neighbor - choose a random one to adopt the others; send it the IPs of all the others
                memset(inputbuffer, 0, sizeof(inputbuffer));
                char msg[512] = "3";
                if (num_connections > 1) {
                    srand(time(NULL));
                    int rand_int = rand() % num_connections;
                    printf("random connection index: %d\n", rand_int);
                    int rand_fd = connections[rand_int];
                    printf("num_connections %d\n", num_connections);
                    for(int v = 0; v < num_connections; v++){
                        printf("%d\n", v);
                        if (connections[v] != rand_fd){
                            write(connections[v], "3 none", 10);
                            read(connections[v], inputbuffer, sizeof(inputbuffer));
                            close(connections[v]);
                            strcat(msg, " ");
                            strcat(msg, inputbuffer);
                            printf("%s\n", msg);
                        }
                    }
                    printf("out of the loop\n");
                    write(rand_fd, msg, 512);
                    close(rand_fd);
                } else {
                    write(connections[0], "3 none", 10);
                    close(connections[0]);
                }
                return 0;
            }

            int c = 0;
            while (inputbuffer[c] != '\n'){
                c++;
            }
            inputbuffer[c] = 0;

            state = 1;
            hops = 1;

            // put together a request message
            gettimeofday(&tv,NULL);
            strcpy(requestedfile, inputbuffer);
            expirationtime = tv.tv_sec * 1000000 + tv.tv_usec + 1000000*hops;// in microseconds
            sprintf(searchmessagebuffer,"0 %lu %s %d %s", tv.tv_sec * 1000000 + tv.tv_usec, myip, hops, requestedfile);

            // add to list of processed requests so that i do not process it when it comes back to me
            request_timestamp[request_ctr % requests_buf_size] = tv.tv_sec * 1000000 + tv.tv_usec;
            request_fd[request_ctr % requests_buf_size] = 0;
            request_ctr++;

            //send request to all neighbors
            for (int i = 0; i < num_connections; i++)
            {
                write(connections[i], searchmessagebuffer, sizeof(searchmessagebuffer));
            }
        }
    }
}



