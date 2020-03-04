// $Id: cix.cpp,v 1.9 2019-04-05 15:04:28-07 - - $

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <sstream>
using namespace std;

#include <libgen.h>
#include <sys/types.h>
#include <unistd.h>

#include "protocol.h"
#include "logstream.h"
#include "sockets.h"

logstream outlog (cout);
struct cix_exit: public exception {};

unordered_map<string,cix_command> command_map {
   {"exit", cix_command::EXIT},
   {"help", cix_command::HELP},
   {"ls"  , cix_command::LS  },
   {"get" , cix_command::GET },
   {"put" , cix_command::PUT },
   {"rm" ,  cix_command::RM  },
};

static const char help[] = R"||(
exit         - Exit the program.  Equivalent to EOF.
get filename - Copy remote file to local host.
help         - Print help summary.
ls           - List names of files on remote server.
put filename - Copy local file to remote host.
rm filename  - Remove file from remote server.
)||";

void cix_help() {
   cout << help;
}
void cix_ls (client_socket& server) {
   cix_header header;
   header.command = cix_command::LS;
   outlog << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   outlog << "received header " << header << endl;
   if (header.command != cix_command::LSOUT) {
      outlog << "sent LS, server did not return LSOUT" << endl;
      outlog << "server returned " << header << endl;
   }else {
      auto buffer = make_unique<char[]> (header.nbytes + 1);
      recv_packet (server, buffer.get(), header.nbytes);
      outlog << "received " << header.nbytes << " bytes" << endl;
      buffer[header.nbytes] = '\0';
      cout << buffer.get();
   }
}
void cix_get(client_socket& server, string filename){
   cix_header header;
   header.command = cix_command::GET;
   strncpy(header.filename, filename.c_str(),filename.size());
   outlog << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);   
   recv_packet (server, &header, sizeof header);
   outlog << "received header " << header << endl;

   if(header.nbytes == 0){
      std::ofstream file(header.filename, std::ofstream::binary);
      cerr << "Get 0 byte file!" << endl;
      return;
   }

   if (header.command == cix_command::FILEOUT) {
      std::ofstream file(header.filename, std::ofstream::binary);
      auto buffer = make_unique<char[]> (header.nbytes + 1);
      recv_packet (server, buffer.get(), header.nbytes);
      file.write(buffer.get(),header.nbytes);
      buffer[header.nbytes] = '\0';
      file.close();
   }
   else{
      outlog << "Error! please enter valid filename" << endl;
   }
}

void cix_put(client_socket& server, string filename){
   cix_header header; 
   strncpy(header.filename, filename.c_str(),filename.size());
   ifstream file {header.filename};

   if (file) {
      header.command = cix_command::PUT;
      file.seekg(0, file.end);
      int length = file.tellg();
      file.seekg(0, file.beg);
      auto buffer = make_unique<char[]> (length);
      header.nbytes = length;
      if(length == 0){
         outlog << "Your file is 0 byte" << endl;
         outlog << "Made 0 byte file on local" << endl;
         return;
      }
      file.read(buffer.get(), length);

      send_packet (server, &header, sizeof header);
      send_packet (server, buffer.get(), length);
      recv_packet (server, &header, sizeof header);
      buffer[header.nbytes] = '\0';
      file.close();

   }
   else if(!file){
      outlog << "Your file does not exist!" << endl;
   }
      
   if (header.command == cix_command::ACK){
      outlog << "Success to put file on server" << endl;
   }
   if (header.command == cix_command::NAK){
      outlog << "Failed to put file on server" << endl;
   }

}
void cix_rm(client_socket& server, string filename){
   cix_header header;
   header.command = cix_command::RM;
   strncpy(header.filename, filename.c_str(),filename.size());
   header.nbytes = 0;
   
   outlog << "sending header " << header << endl;
   send_packet (server, &header, sizeof header);
   recv_packet (server, &header, sizeof header);
   outlog << "received header " << header << endl;
   
   if (header.command == cix_command::NAK){
      outlog << "NAK: Error" << endl;
   }    
   if (header.command == cix_command::ACK){
      outlog << "Process on remove." << endl;
   }
}


void usage() {
   cerr << "Usage: " << outlog.execname() << " [host] [port]" << endl;
   throw cix_exit();
}

int main (int argc, char** argv) {
   outlog.execname (basename (argv[0]));
   outlog << "starting" << endl;
   vector<string> args (&argv[1], &argv[argc]);
   string host;
   in_port_t port;
   if (args.size() > 2) usage();
   if(args.size() == 1){
      host = get_cix_server_port (args, 1);
      port = get_cix_server_port (args, 0);
      outlog << to_string (hostinfo()) << endl;
   }
   else{
      host = get_cix_server_host (args, 0);
      port = get_cix_server_port (args, 1);
      outlog << to_string (hostinfo()) << endl;
   }

   try {
      outlog << "connecting to " << host << " port " << port << endl;
      client_socket server (host, port);
      outlog << "connected to " << to_string (server) << endl;
      for (;;) {
         string line;
         getline (cin, line);
         if (cin.eof()) throw cix_exit();

         vector<string> read;
         std::istringstream ss(line);
         string token;
         while(getline(ss,token,' ')) read.push_back(token);
         outlog << "command " << line << endl;
         const auto& itor = command_map.find (read[0]);
         cix_command cmd = itor == command_map.end()
                         ? cix_command::ERROR : itor->second;
         switch (cmd) {
            case cix_command::EXIT:
               throw cix_exit();
               break;
            case cix_command::HELP:
               cix_help();
               break;
            case cix_command::LS:
               cix_ls (server);
               break;
            case cix_command::GET:
               cix_get (server, read[1]);
               read.erase(read.begin(),read.end());
               break;
            case cix_command::RM:
               cix_rm(server, read[1]);
               read.erase(read.begin(),read.end());
               break;
            case cix_command::PUT:
               cix_put(server, read[1]);
               read.erase(read.begin(),read.end());
               break;
            default:
               outlog << line << ": invalid command" << endl;
               break;
         }
      }
   }catch (socket_error& error) {
      outlog << error.what() << endl;
   }catch (cix_exit& error) {
      outlog << "caught cix_exit" << endl;
   }
   outlog << "finishing" << endl;
   return 0;
}

