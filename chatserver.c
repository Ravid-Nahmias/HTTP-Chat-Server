#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <signal.h>

#define BUF_SIZE 4096

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

typedef struct node
{
    char *msg;
    int fd;
    struct node *next;
} node;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~global attributes~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

fd_set readfds, c_readfds, c_writefds;
char buf[BUF_SIZE];
int newSocket = -1;
int main_socket, serving_socket, rc, fd, maxFD = 0, count = 0;
node *head = NULL, *newList = NULL;
static int keepRunning = 0;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void systemError(char *msg)
{ //function to print errors of system functions
    perror(msg);
    printf("\n");
    exit(EXIT_FAILURE);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void commandError()
{ //function to print errors for wrong command
    fprintf(stderr, "Command line usage: ./server <port>\n");
    exit(EXIT_FAILURE);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

node *createNode(int fd_wrote_the_msg, int fd_read)
{ // this function creates new node in the first time, with the right format of the messege
    node *cur = (node *)malloc(sizeof(node));
    if (cur == NULL) // allocation check
        systemError("malloc");

    cur->msg = calloc((strlen(buf) + 100), sizeof(char));
    if (cur->msg == NULL)
        systemError("calloc");
    sprintf(cur->msg, "guest%d: %s", fd_wrote_the_msg, buf); // new messege according the format
    cur->next = NULL;
    cur->fd = fd_read;

    count++;
    return cur;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void addNode(node **head, node *toAdd)
{ // this function add a new messege to the list
    node *temp = *head;
    if (*head == NULL)
    { // empty list
        *head = toAdd;
    }
    else
    { // there are elements in the list
        while (temp->next != NULL)
        {
            temp = temp->next;
        }
        temp->next = toAdd;
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void freeList(node **head)
{   // free all the nodes in a single list
    node *temp = *head, *next = *head;
    while (temp->next != NULL)
    {
        next = temp->next;
        free(temp->msg);
        free(temp);
        temp = next;
    }
    free(*head);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void sig_handler(int sig)
{ // this function handle the ctrl+c signal - close and free memory
    keepRunning = 1;
    //close(fd);
    if (head != NULL)
    { // free nodes in the head list
        freeList(&head);
    }
    if (newList != NULL)
    { // free nodes in new list
        freeList(&newList);
    }
    for (int i = 3; i < maxFD; i++)
        close(i);

    exit(EXIT_SUCCESS);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

int writeMessegesInList()
{
    // this function go over the list and write the messeges to the sockets
    node *temp = head, *next = NULL;
    int written = 0;
    newList = NULL;
    for (int i = 0; i < count; i++)
    {
        if (FD_ISSET(temp->fd, &c_writefds))
        {
            printf("server is ready to write from socket %d\n", temp->fd);
            // the specific fd is ready to write
            write(temp->fd, temp->msg, strlen(temp->msg));
            written++; // each messege written, add to counter
        }
        else
        { // create and add a copy of the temp node to a temporary list
            node *copy = (node *)malloc(sizeof(node));
            if (copy == NULL) // allocation check
                systemError("malloc");
            copy->msg = calloc((strlen(temp->msg) + 100), sizeof(char));
            if (copy->msg == NULL)
                systemError("calloc");
            strcpy(copy->msg, temp->msg); // copy the exist messege
            copy->next = NULL;
            copy->fd = temp->fd;
            addNode(&newList, copy);
        }
        next = temp->next;
        free(temp->msg);
        free(temp);
        temp = next;
    }
    count -= written; // new size of list
    if (count == 0)
    { // all the messeges written
        head = NULL;
        newList = NULL;
    }
    else // there are messeges in the temp list, repoint the head pointer
        head = newList;

    return 0;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

int main(int argc, char *argv[])
{
    if (argc < 2)
        commandError();
    int port = atoi(argv[1]);
    if (port < 0)
        commandError();

    signal(SIGINT, sig_handler);

    /* socket descriptor */
    if ((main_socket = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        systemError("socket");
    }

    struct sockaddr_in srv; /* used by bind() */

    srv.sin_family = AF_INET;            /* use the Internet addr family */
    srv.sin_port = htons(atoi(argv[1])); /* bind socket ‘fd’ to port */
    srv.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(main_socket, (struct sockaddr *)&srv, sizeof(srv)) < 0)
    {
        systemError("bind");
    }

    if (listen(main_socket, 5) < 0)
    {
        systemError("listen");
    }

    FD_ZERO(&readfds);
    FD_SET(main_socket, &readfds);
    maxFD = main_socket + 1;
    while (keepRunning == 0)
    {
        // initialize the fd sets
        c_readfds = readfds;
        c_writefds = readfds;
        if (head == NULL)
        { // if the list in empty, resets
            FD_ZERO(&c_writefds);
        }
        else
        {
            c_writefds = readfds;
        }

        rc = select(maxFD, &c_readfds, &c_writefds, NULL, NULL);

        if (FD_ISSET(main_socket, &c_readfds))
        {                                                     // new client accepted
            serving_socket = accept(main_socket, NULL, NULL); // receive new fd for the new client
            if (maxFD <= serving_socket)
                maxFD = serving_socket + 1;
            if (serving_socket > 0)
            {
                FD_SET(serving_socket, &readfds); // turn the bit of the new socket on
            }
            if (serving_socket < 0) // error
                systemError("accept");
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~read all the messeges~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

        for (fd = main_socket + 1; fd < maxFD; fd++)
        { // a loop that reads all the messeges received and add them to the queue
            if (FD_ISSET(fd, &c_readfds))
            { // if the bit is up the client has a change
                memset(buf, 0, BUF_SIZE);
                printf("server is ready to read from socket %d\n", fd);
                rc = read(fd, &buf, BUF_SIZE);
                if (rc == 0)
                { // the client closed the connection
                    close(fd);
                    FD_CLR(fd, &readfds);   // delete the fd from the read set
                    FD_CLR(fd, &c_readfds); // delete the fd from the read copy set
                }
                else if (rc > 0)
                { // he wrote a messege and we need to add a node to the list and write it to the other clients
                    fd_set copy = readfds;
                    for (int fd_to_write = main_socket + 1; fd_to_write < maxFD; fd_to_write++)
                    {
                        if (FD_ISSET(fd_to_write, &copy) && fd != fd_to_write) // check if the fd who read the messege does not equal to the fd who will write the messege
                        // and fd didnt closed and if it does- doesn't send the messege
                        {
                            node *toAdd = createNode(fd, fd_to_write);
                            addNode(&head, toAdd);
                        }
                    }
                }
                else
                {
                    systemError("read() failed");
                }
            }
        }

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~write all the messeges~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

        if (head != NULL)
            writeMessegesInList();
    }

    exit(EXIT_SUCCESS);
}