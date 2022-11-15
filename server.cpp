#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sstream>
#include <string.h>
#include <signal.h>
#include <filesystem>
#include <dirent.h>
#include <vector>
#include <fstream>


///////////////////////////////////////////////////////////////////////////////

#define BUF 3049 //max size needed for 3000 character message + command, sender, receiver data 

///////////////////////////////////////////////////////////////////////////////

int PORT;
int abortRequested = 0;
int create_socket = -1;
int new_socket = -1;
namespace fs = std::filesystem;

///////////////////////////////////////////////////////////////////////////////

void *clientCommunication(void *data, char* folder);
void signalHandler(int sig);

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv)
{
   if(argc < 2) {
      std::cerr << "More arguments needed.";
      exit(0);  
   }

   else{
      //assign a PORT number//
      PORT = atoi(argv[1]);

      //checking if folder exists//
      DIR* folder = opendir(argv[2]);

      if(folder == NULL) {
         //create directory//

         bool succ_cre = fs::create_directory(argv[2]);

         if(succ_cre) {
            std::cout << "Directory created" << std::endl;
         }
         else {
            std::cerr << "Unable to create directory" << std::endl;
            exit(1);
         } 
      }
   }
   socklen_t addrlen;
   struct sockaddr_in address, cliaddress;
   int reuseValue = 1;

   ////////////////////////////////////////////////////////////////////////////
   // SIGNAL HANDLER
   // SIGINT (Interrup: ctrl+c)
   // https://man7.org/linux/man-pages/man2/signal.2.html
   if (signal(SIGINT, signalHandler) == SIG_ERR)
   {
      perror("signal can not be registered");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // CREATE A SOCKET
   // https://man7.org/linux/man-pages/man2/socket.2.html
   // https://man7.org/linux/man-pages/man7/ip.7.html
   // https://man7.org/linux/man-pages/man7/tcp.7.html
   // IPv4, TCP (connection oriented), IP (same as client)
   if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
   {
      perror("Socket error"); // errno set by socket()
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // SET SOCKET OPTIONS
   // https://man7.org/linux/man-pages/man2/setsockopt.2.html
   // https://man7.org/linux/man-pages/man7/socket.7.html
   // socket, level, optname, optvalue, optlen
   if (setsockopt(create_socket,
                  SOL_SOCKET,
                  SO_REUSEADDR,
                  &reuseValue,
                  sizeof(reuseValue)) == -1)
   {
      perror("set socket options - reuseAddr");
      return EXIT_FAILURE;
   }

   if (setsockopt(create_socket,
                  SOL_SOCKET,
                  SO_REUSEPORT,
                  &reuseValue,
                  sizeof(reuseValue)) == -1)
   {
      perror("set socket options - reusePort");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // INIT ADDRESS
   // Attention: network byte order => big endian
   memset(&address, 0, sizeof(address));
   address.sin_family = AF_INET;
   address.sin_addr.s_addr = INADDR_ANY;
   address.sin_port = htons(PORT);

   ////////////////////////////////////////////////////////////////////////////
   // ASSIGN AN ADDRESS WITH PORT TO SOCKET
   if (bind(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1)
   {
      perror("bind error");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // ALLOW CONNECTION ESTABLISHING
   // Socket, Backlog (= count of waiting connections allowed)
   if (listen(create_socket, 5) == -1)
   {
      perror("listen error");
      return EXIT_FAILURE;
   }

   while (!abortRequested)
   {
      /////////////////////////////////////////////////////////////////////////
      std::cout << "Waiting for connections...\n" << std::endl;

      /////////////////////////////////////////////////////////////////////////
      // ACCEPTS CONNECTION SETUP
      // blocking, might have an accept-error on ctrl+c
      addrlen = sizeof(struct sockaddr_in);
      if ((new_socket = accept(create_socket,
                               (struct sockaddr *)&cliaddress,
                               &addrlen)) == -1)
      {
         if (abortRequested)
         {
            perror("accept error after aborted");
         }
         else
         {
            perror("accept error");
         }
         break;
      }

      /////////////////////////////////////////////////////////////////////////
      // START CLIENT
      // ignore printf error handling
      printf("Client connected from %s:%d...\n",
             inet_ntoa(cliaddress.sin_addr),
             ntohs(cliaddress.sin_port));
      clientCommunication(&new_socket, argv[2]); // returnValue can be ignored
      new_socket = -1;
   }

   // frees the descriptor
   if (create_socket != -1)
   {
      if (shutdown(create_socket, SHUT_RDWR) == -1)
      {
         perror("shutdown create_socket");
      }
      if (close(create_socket) == -1)
      {
         perror("close create_socket");
      }
      create_socket = -1;
   }

   return EXIT_SUCCESS;
}

void *clientCommunication(void *data, char* folder)
{
   char buffer[BUF];
   int size;
   int *current_socket = (int *)data;

   ////////////////////////////////////////////////////////////////////////////
   // SEND welcome message
   strcpy(buffer, "Welcome to the Mailsystem!\n");
   if (send(*current_socket, buffer, strlen(buffer), 0) == -1)
   {
      perror("send failed");
      return NULL;
   }

   do
   {
      /////////////////////////////////////////////////////////////////////////
      // RECEIVE COMMAND
      size = recv(*current_socket, buffer, BUF - 1, 0);
      if (size == -1)
      {
         if (abortRequested)
         {
            perror("recv error after aborted");
         }
         else
         {
            perror("recv error");
         }
         break;
      }

      if (size == 0)
      {
         std::cerr << "Error: Could not receive Command!" << std::endl;
         break;
      }

      buffer[size] = '\0';
      std::cout << "Message received:\n" << std::endl; // ignore error


      std::stringstream ss(buffer); //turning buffer to stringstream
      std::string line;              //splitting string into lines
      std::vector<std::string> msg;  //adding lines to vector


      while(std::getline(ss, line, '\n')) {    //splitting the string into lines
         msg.push_back(line);
      }

      std::string command = msg.at(0);
      msg.erase(msg.begin());

      for(std::string& data : msg)
         std::cout << data << std::endl;

      memset(buffer, 0, BUF);

/////////////////////////////////////
        //SEND//
        if(command.compare("send") == 0) {
            //creates path object for mailspooler directory//
            fs::path current = fs::current_path();
            fs::path mailspooler = fs::path(current.string() + "/" + folder);
            std::cout << mailspooler.string();

            //switching to mailspooler directory//
            try{
                fs::current_path(mailspooler);
            }
            catch(...){
                std::cerr << "An error occured with the filesystem" << std::endl;
                strcat(buffer, "ERR");
            }
            //creates subfolder in directory with name of receiver//
            fs::create_directory(msg.at(1));

            //changing to users directory
            try{
                fs::current_path(mailspooler.string() + "/"  + msg.at(1));
            }
            catch(...){
                std::cerr << "An error occured with the filesystem" << std::endl;
            }
            
            //makes the filename the time it was sent//
            using namespace std::chrono;
            auto now = std::chrono::system_clock::now();
            auto timeT = std::chrono::system_clock::to_time_t(now);
            std::string time = std::ctime(&timeT); 
            time.pop_back();  //This is just to remove the ugly ending of the timestamp

            //create file
            std::ofstream user_msg(time + ".txt");

            user_msg << "Sender: " << msg.at(0) << std::endl << "Subject: " << msg.at(2) << std::endl << "Message: " << std::endl;
            for(int i = 3; i<msg.size(); i++)
               user_msg << msg.at(i) << std::endl;           //writing every single line from the message into file

            user_msg.close();

            //changing back to base directory
            try{
                fs::current_path(current);
                strcat(buffer, "OK");
            }
            catch(...){
                std::cerr << "An error has occured with the filesystem" << std::endl;
                strcat(buffer, "ERR");
            }
        } 

      if (send(*current_socket, "OK", 3, 0) == -1)
      {
         perror("send answer failed");
         return NULL;
      }
   } while (strcmp(buffer, "quit") != 0 && !abortRequested);

   // closes/frees the descriptor if not already
   if (*current_socket != -1)
   {
      if (shutdown(*current_socket, SHUT_RDWR) == -1)
      {
         perror("shutdown new_socket");
      }
      if (close(*current_socket) == -1)
      {
         perror("close new_socket");
      }
      *current_socket = -1;
   }

   return NULL;
}

void signalHandler(int sig)
{
   if (sig == SIGINT)
   {
      printf("abort Requested... "); // ignore error
      abortRequested = 1;
      /////////////////////////////////////////////////////////////////////////
      // With shutdown() one can initiate normal TCP close sequence ignoring
      // the reference count.
      // https://beej.us/guide/bgnet/html/#close-and-shutdownget-outta-my-face
      // https://linux.die.net/man/3/shutdown
      if (new_socket != -1)
      {
         if (shutdown(new_socket, SHUT_RDWR) == -1)
         {
            perror("shutdown new_socket");
         }
         if (close(new_socket) == -1)
         {
            perror("close new_socket");
         }
         new_socket = -1;
      }

      if (create_socket != -1)
      {
         if (shutdown(create_socket, SHUT_RDWR) == -1)
         {
            perror("shutdown create_socket");
         }
         if (close(create_socket) == -1)
         {
            perror("close create_socket");
         }
         create_socket = -1;
      }
   }
   else
   {
      exit(sig);
   }
}
