#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <iostream>
#include <sstream> 
#include "mypw.cpp"

///////////////////////////////////////////////////////////////////////////////

#define BUF 3049 //max size needed for 3000 character message + command, sender, receiver data


///////////////////////////////////////////////////////////////////////////////

int PORT;

int main(int argc, char **argv)
{
   int create_socket;
   char buffer[BUF];
   struct sockaddr_in address;
   int size;
   PORT = atoi(argv[2]);
   bool isQuit = false;


   ////////////////////////////////////////////////////////////////////////////
   // CREATE A SOCKET
   // https://man7.org/linux/man-pages/man2/socket.2.html
   // https://man7.org/linux/man-pages/man7/ip.7.html
   // https://man7.org/linux/man-pages/man7/tcp.7.html
   // IPv4, TCP (connection oriented), IP (same as server)
   if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
   {
      perror("Socket error");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // INIT ADDRESS
   // Attention: network byte order => big endian
   memset(&address, 0, sizeof(address)); // init storage with 0
   address.sin_family = AF_INET;         // IPv4
   // https://man7.org/linux/man-pages/man3/htons.3.html
   address.sin_port = htons(PORT);
   // https://man7.org/linux/man-pages/man3/inet_aton.3.html
   if(argc < 2) {
      std::cerr << "More arguments needed.";
    }
    else{
      //assign IP-address
      inet_aton(argv[1], &address.sin_addr);
      //assign PORT number
      address.sin_port = htons(PORT);
    }

   ////////////////////////////////////////////////////////////////////////////
   // CREATE A CONNECTION
   // https://man7.org/linux/man-pages/man2/connect.2.html
   if (connect(create_socket,
      (struct sockaddr *)&address,
      sizeof(address)) == -1)
   {
      // https://man7.org/linux/man-pages/man3/perror.3.html
      perror("Connect error - no server available");
      return EXIT_FAILURE;
   }

   // ignore return value of printf
   printf("Connection with server (%s) established\n",
      inet_ntoa(address.sin_addr));

   ////////////////////////////////////////////////////////////////////////////
   // RECEIVE DATA
   // https://man7.org/linux/man-pages/man2/recv.2.html
   size = recv(create_socket, buffer, BUF - 1, 0);
   if (size == -1)
   {
      perror("recv error");
   }
   else if (size == 0)
   {
      printf("Server closed remote socket\n"); // ignore error
   }
   else
   {
      buffer[size] = '\0';
      printf("%s", buffer); // ignore error
   }

   do
   {
      memset(buffer, 0, BUF); //empying out buffer

      //////////////////SENDING COMMAND TO SERVER////////////////
      std::string command;
      std::cout << "Please enter your command:\n>> ";
      std::getline(std::cin, command);
      for(char& c : command){
         c = tolower(c);         //converting string to all lowercase letters so that commands are case insensitive
      }
      strcat(buffer, (command + "\n").c_str());

      if(command.compare("login") == 0){
         std::string username;

         do{
            if (username.length() > 8)
               std::cout << "Username can not be longer than 8 characters!" << std::endl;
            std::cout << "Enter username (FHTW username): " <<  std::endl;
            std::getline(std::cin, username);
         }while(username.length() > 8);

         std::string pw = getpass();

         strcat(buffer, (username + "\n").c_str());
         strcat(buffer, (pw + "\n").c_str());

      }

      else if(command.compare("send") == 0){
         std::string receiver;
         std::string subject;
         std::string message;
         std::string line;


         //Getting usernames input -> max 16 charcaters
         do{
            if(receiver.length() > 8)
               std::cout << "Receiver's username can not be longer than 8 characters" << std::endl;
            std::cout << "Enter receiver username (max. 8 characters): " <<  std::endl;
            std::getline(std::cin, receiver);
         } while(receiver.length() > 8);


         //Getting subject and message max 
         do{
            if (subject.length() + message.length() > 3000)
               std::cout << "Message too long! " << std::endl;
            do{
               if (subject.length() > 80)
                  std::cout << "Subject line can not be longer than 80 characters" << std::endl;
               std::cout << "Enter subject line : " <<  std::endl;
               std::getline(std::cin, subject);
            }while (subject.length() > 80);
            std::cout << "Enter message (end message with single '.' in line and enter): " <<  std::endl;
            while (std::getline(std::cin, line)){
               if (line == ".")
                  break;
              message += (line + "\n");
            }
         }while(subject.length() + message.length() > 3000); //check to prevent buffer overflow
            
            strcat(buffer, (receiver + "\n").c_str());
            strcat(buffer, (subject + "\n").c_str());
            strcat(buffer, (message + "\n").c_str());
      }

      else if (command.compare("quit") == 0){
         strcat(buffer, "QUIT");
         isQuit = true;
      }
///////////////////////////////
      //LIST, READ, DEL//
      else if(command.compare("list") == 0 || command.compare("read") == 0 || command.compare("del") == 0) {
         
         ///////This part only for read and del command /////////
         if(command.compare("read") == 0 || command.compare("del") == 0){
            std::string msgindex;
            std::cout << "Please enter the message number you want to " << command << ": ";
            std::getline(std::cin, msgindex);
            strcat(buffer, (msgindex + "\n").c_str());
         }
      }

      else{
         std::cout << "That's not a valid Command!\nValid commands are:\nSEND\nLIST\nREAD\nDEL\n" << std::endl;
         strcat(buffer, "Invalid command error\n");
      }
      
      int size = strlen(buffer);
      //////////////////////////////////////////////////////////////////////
      // SEND DATA 
      if ((send(create_socket, buffer, size, 0)) == -1) {
         std::cerr << "send error" << std::endl;
         break;
      }
      
      if (isQuit){
         break;
      }

      //////////////////////////////////////////////////////////////////////
      // RECEIVE FEEDBACK

      size = recv(create_socket, buffer, BUF - 1, 0);
      if (size == -1)
      {
         perror("recv error");
         break;
      }
      else if (size == 0)
      {
         printf("Server closed remote socket\n"); // ignore error
         break;
      }
      else
      {
         buffer[size] = '\0';
         std::cout << buffer << std::endl;; // ignore error
      }
   } while (!isQuit);

   ////////////////////////////////////////////////////////////////////////////
   // CLOSES THE DESCRIPTOR
   if (create_socket != -1)
   {
      if (shutdown(create_socket, SHUT_RDWR) == -1)
      {
         // invalid in case the server is gone already
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
