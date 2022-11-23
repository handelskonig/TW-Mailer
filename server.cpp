#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sstream>
#include <string>
#include <signal.h>
#include <filesystem>
#include <vector>
#include <fstream>
#include <ldap.h>
#include <ctime>


///////////////////////////////////////////////////////////////////////////////

#define BUF 3049 //max size needed for 3000 character message + command, sender, receiver data 

///////////////////////////////////////////////////////////////////////////////

int PORT;
int abortRequested = 0;
int create_socket = -1;
int new_socket = -1;
namespace fs = std::filesystem;

///////////////////////////////////////////////////////////////////////////////

void *clientCommunication(void *data, char* folder, char* ip);
void signalHandler(int sig);

time_t getTimestamp(int addition){
   using namespace std::chrono;
   auto now = std::chrono::system_clock::now();
   time_t timeT = std::chrono::system_clock::to_time_t(now);    //getting timestamp for now
   return timeT + addition;
}

//////////////////////////////////////////////
int ldap_login(std::string rawLdapUser, char* ldapBindPassword, LDAP* ldapHandle){
   ////////////////////////////////////////////////////////////////////////////
   // bind credentials
   std::string ldapBindUser = "uid=" + rawLdapUser + ",ou=people,dc=technikum-wien,dc=at";   
   int rc = 0; //return code

   BerValue bindCredentials;
   bindCredentials.bv_val = (char*)ldapBindPassword;
   bindCredentials.bv_len = strlen(ldapBindPassword);
   BerValue *servercredp; // server's credentials
   rc = ldap_sasl_bind_s(
      ldapHandle,
      ldapBindUser.c_str(),
      LDAP_SASL_SIMPLE,
      &bindCredentials,
      NULL,
      NULL,
      &servercredp);
   if (rc != LDAP_SUCCESS)
   {
      fprintf(stderr, "LDAP bind error: %s\n", ldap_err2string(rc));
      ldap_unbind_ext_s(ldapHandle, NULL, NULL);
      return rc;
   }
   return rc;
}

////////////////////////////////////////////////////////////////////////////
// setup LDAP connection
LDAP* ldap_setup_con(){
   ///////////////////////////////////////////////////////////////////////////
   // LDAP config
   const std::string ldapUri = "ldap://ldap.technikum-wien.at:389";
   const int ldapVersion = LDAP_VERSION3;

   int rc = 0; //return code

   LDAP* ldapHandle;
   rc = ldap_initialize(&ldapHandle, ldapUri.c_str());
   if (rc != LDAP_SUCCESS){
      std::cerr << "ldap_init failed" << std::endl;
      return nullptr;
   }

   std::cout << "connected to LDAP server " << ldapUri << std::endl;

   ////////////////////////////////////////////////////////////////////////////
   // set version options
   rc = ldap_set_option(
       ldapHandle,
       LDAP_OPT_PROTOCOL_VERSION, // OPTION
       &ldapVersion);             // IN-Value

   if (rc != LDAP_OPT_SUCCESS){
      std::cerr << "ldap_set_option(PROTOCOL_VERSION): " << ldap_err2string(rc) << std::endl;
      ldap_unbind_ext_s(ldapHandle, NULL, NULL);
      return nullptr;
   }
   ////////////////////////////////////////////////////////////////////////////
   // start connection secure (initialize TLS)
   rc = ldap_start_tls_s(
      ldapHandle,
      NULL,
      NULL);
       
   if (rc != LDAP_SUCCESS){
      std::cerr << "ldap_start_tls_s(): " <<  ldap_err2string(rc) << std::endl;
      ldap_unbind_ext_s(ldapHandle, NULL, NULL);
      return nullptr;
   }

   return ldapHandle;
}

///////////////////////////////////////////////////////
 ////LOGIN////
///////////////////////////////////////////////////////////
int login(std::string rawLdapUser, char* ldapBindPassword){
   LDAP* ldapHandle = ldap_setup_con();
   if (ldapHandle != nullptr){
      return ldap_login(rawLdapUser, ldapBindPassword, ldapHandle);
   }
   return EXIT_FAILURE;
}

int checkBlacklist(char* ip){
   fs::path blacklist = fs::current_path().string() + "/blacklist.txt";
   if(!fs::exists(blacklist)){
      return -1;
   }
   else{
      std::string line;
      std::ifstream file (blacklist);
      struct tm tm;

      if (file.is_open()){
         while(getline(file, line)){  //loops through all the lines in file
         
         time_t now = getTimestamp(0);
         std::string timestamp_str = line.substr(0, line.find(","));
         std::string attempts = line.substr(line.find(",") + 1, 1);

            if(line.find(ip) != std::string::npos){     //looks for every line that contains a specific user's ip address

               if (strptime(timestamp_str.c_str(), "%A %b %d %T %Y", &tm) == NULL)    //turn the lines entry timestamp into a time_t obj
                  std::cerr << "Time string could not be turned into time_t" << std::endl;
               time_t ent = mktime(&tm);
               
               std::cout << line;
               if(difftime(now, ent) < 60 && stoi(attempts) > 3)
                  return 4;
               else if (difftime(now, ent) < 60 && stoi(attempts) == 2)
                  return 2;
               else if (difftime(now, ent) < 60 && stoi(attempts) == 1)
                  return 1;
               else
                  return 0;
            }
         }
      }
      else{
         std::cerr << "Error opening blacklist... Good for you hacker!" << std::endl;
      }
         
      return 0;
   } 
}

int modBlacklist(char* ip, int attempts){
   fs::path blacklist = fs::current_path().string() + "/blacklist.txt";

   time_t timeT = getTimestamp(0); //current timestamp 
   time_t timeTp1 = getTimestamp(60); //current timestamp plus 60 secs

   std::string time = std::ctime(&timeT);//turning timestamps to strings
   std::string timep1 = std::ctime(&timeTp1);
   time.pop_back();  //removing newline from timestamp
   timep1.pop_back();


   std::ofstream entry (blacklist, std::ios_base::app);
   //current timestamp, attempts and user ip is being saved in file
   entry << time << "," << attempts << "," << ip;
   if(attempts > 2)
      entry << "," << timep1;  //if max attempts are reached than the time where user can try again is added
   entry << std::endl;
   entry.close();  
   return 0;
   
}

/////////////////////////////////////
   //SEND//
///////////////////////////////////////////////////////////////////////////////
void send(fs::path mailspooler, char* buffer, std::vector<std::string> msg, fs::path current, std::string user){
   //switching to mailspooler directory//
   try{
      fs::current_path(mailspooler);
   }
   catch(...){
      std::cerr << "An error occured with the filesystem" << std::endl;
      strcat(buffer, "ERR");
   }
   //creates subfolder in directory with name of receiver//
   fs::create_directory(msg.at(0));

   //changing to users directory
   try{
      fs::current_path(mailspooler.string() + "/"  + user);
   }
   catch(...){
      std::cerr << "An error occured with the filesystem" << std::endl;
   }
   
   /*//makes the filename the time it was sent//
   using namespace std::chrono;
   auto now = std::chrono::system_clock::now();
   auto timeT = std::chrono::system_clock::to_time_t(now);    //decided later to make the filename the subject line instead
   std::string time = std::ctime(&timeT); 
   time.pop_back();  //This is just to remove the ugly ending of the timestamp*/

   int index = std::distance(fs::directory_iterator(fs::current_path()), {});  // checks to see how many files are already in directory

   //create file
   std::ofstream user_msg(std::to_string(index + 1) + ". " + msg.at(1));

   user_msg << "Sender: " << user << std::endl << "Subject: " << msg.at(1) << std::endl << "Message: " << std::endl;
   for(unsigned int i = 2; i<msg.size(); i++)
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

///////////////////////////////////////////////////
/////////////////LIST/////////////////////////////
void list(char* buffer, fs::path mailspooler, std::string user){
   auto usersubdir = fs::path(mailspooler.string() + "/" + user);
   if (fs::exists(usersubdir)){
      int counter = 0;
      std::vector<fs::path> messages;
      for (auto& entry : std::filesystem::directory_iterator(usersubdir)){
         counter++;                                   //loops through all messages, counts them and saves in vector
         messages.push_back(entry.path());    
      }

      strcat(buffer, ("User " + user + " has " + std::to_string(counter) + " messages in his inbox : \n").c_str());
      for (auto& message : messages){
         strcat(buffer, (message.filename().string() + "\n").c_str());
      }
   }
   else
      strcat(buffer, "The user you have been looking for doesn't exist or hasn't received any messsages yet!\n");
}

//////////////////////////////////////////////////////////////
/////////////////////READ AND DELETE//////////////////////////
 void read_del(char* buffer, fs::path mailspooler, std::vector<std::string> msg, std::string command, std::string user){     
   auto usersubdir = fs::path(mailspooler.string() + "/" + user);
   if (fs::exists(usersubdir)){
      for (auto& entry : std::filesystem::directory_iterator(usersubdir)){
         memset(buffer, 0, BUF);
         //Looks for '.' in every entry and cuts off rest of string to get message index
         std::string msgindex = entry.path().filename().string().substr(0, entry.path().filename().string().find("."));

         //enters this condition if file is found
         if(msgindex.compare(msg.at(0)) == 0){
            std::ifstream message (entry.path());
            //Reading every line of file into buffer
            if (command.compare("read") == 0){
               if(message.is_open()){

                  strcat(buffer, "OK\n");  
                  std::string line;

                  while(message){
                     std::getline(message, line);
                     strcat(buffer, (line + "\n").c_str());
                  }
               }
               else{
                  strcat(buffer, "ERR\n");
                  strcat(buffer, "Could not open file\n");
               }
            }
            //trying to remove file
            else if(command.compare("del") == 0){
               if (fs::remove(entry.path()))
                  strcat(buffer, "OK\n");
               else{
                  strcat(buffer, "ERR\n");
                  strcat(buffer, "Could not delete file\n");
               }
            }          
            break;
         }
         strcat(buffer, "ERR\n");
         strcat(buffer, "Message with that number does not exist. Use command 'list' to check messages!");
      }
      /*   Giving this approach up for simplicity
      if (strtok(buffer, "\n") != "OK"){
         strcat(buffer, "ERR\n");
         strcat(buffer, "Message with that number does not exist. Use command 'list' to check messages!");
      }*/
   }
   else{
      strcat(buffer, "The user you have been looking for doesn't exist or hasn't received any messsages yet!\n");
   }
 }

int main(int argc, char** argv){

   if(argc < 2) {
      std::cerr << "More arguments needed.";
      exit(0);  
   }

   else{
      //assign a PORT number//
      PORT = atoi(argv[1]);

      //checking if folder exists//

      if(!fs::exists(argv[2])) {
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
      clientCommunication(&new_socket, argv[2], inet_ntoa(cliaddress.sin_addr)); // returnValue can be ignored
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

void *clientCommunication(void *data, char* folder, char* ip)
{
   char buffer[BUF];
   int size;
   int *current_socket = (int *)data;
   std::string user;
   int attemptedLogin = 0;

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

      std::string command = msg.at(0);  //extracting command from message
      msg.erase(msg.begin());

      memset(buffer, 0, BUF);          //emptying out buffer

      if (command.compare("quit") == 0){
         std::cout << "Client disconnected" << std::endl;
         break;
      } 

      else if (command.compare("login") == 0) {
         std::cout << msg.at(0) << " attempting to log into LDAP Server..." << std::endl;
         int rc = login(msg.at(0), (char*)msg.at(1).c_str());
         if(rc == LDAP_SUCCESS){
            attemptedLogin = 0;   //reseting attempted logins
            strcat(buffer, (msg.at(0) + " successfully logged in\n").c_str());
            //setting the user for the session
            user = msg.at(0); 
         }
         else{
            strcat(buffer, "ERR\n");
            strcat(buffer, ldap_err2string(rc));
            if(strcmp(ldap_err2string(rc), "Invalid credentials") == 0){
               attemptedLogin++;
               std::cout << "failed login attemps: " << attemptedLogin << std::endl;
            }
            modBlacklist(ip, attemptedLogin);
            checkBlacklist(ip);
            strcat(buffer, "\n");
         }
      }

      else if(command.compare("login") != 0 && user.length() > 0){
         
         //printing out clients message
         for(std::string& data : msg)       
         std::cout << data << std::endl;

         //creates path object for mailspooler directory//
         fs::path current = fs::current_path();
         fs::path mailspooler = fs::path(current.string() + "/" + folder);

         if (command.compare("send") == 0){
            send(mailspooler, buffer, msg, current, user);
         }

         else if (command.compare("list") == 0){
            list(buffer, mailspooler, user);
         }

         else if(command.compare("read") == 0 || command.compare("del") == 0){
            read_del(buffer, mailspooler, msg, command, user);
         }

         else {
            strcat(buffer, "ERR\n");
         }
      }
      else{
         strcat(buffer, "ERR\n");
         strcat(buffer, "You must log in first to use commands other than quit!\n");
      }

///////////////////////////////////
//////////////SEND RESPONSE////////
      size = strlen(buffer);

      if (send(*current_socket, buffer, size, 0) == -1)
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
