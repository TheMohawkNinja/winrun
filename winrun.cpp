#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <cstdlib>
#include <stdio.h>
#include <fcntl.h>
#include <fstream>

int bytesReceived,sendRes,sock;
int connectRes=INT16_MAX;
const int bufsize=4096;
char buf[bufsize];
std::string user=getlogin();
std::string ip="";
std::string configpath="/home/"+user+"/.config/winrun/config";
std::ifstream readstream;

void sendData(std::string d, int s)
{
	std::string breakCode;

	//Send command
        sendRes=send(s,d.c_str(),d.size()+1,0);

        if(sendRes==-1)
        {
              	std::cout<<"Could not send data to server!\r\n";
                sleep(1000);
        }

	//Recieve break code
	breakCode=std::string(buf,recv(s,buf,bufsize,0));

	//Wait for response and exit program when break code is recieved.
	while(true)
	{
       	        memset(buf,0,bufsize);
	        bytesReceived=recv(s,buf,bufsize,0);

		if(std::string(buf,bytesReceived).find(breakCode)!=std::string::npos)
		{
			break;
		}
		else
		{
	        	//Display response
		        if(bytesReceived>0)
		        {
		                std::cout<<std::string(buf,bytesReceived);
	        	}
		}
	}
}
bool fexists(const char *filename)
{
	std::ifstream ifile(filename);
	return (bool)ifile;
}
int main(int argc, char** argv)
{
	if(!argv[1])
	{
		std::cout<<"No command entered!"<<std::endl;
		return -2;
	}

	if(!fexists(configpath.c_str()))
	{
		std::cout<<"Config file not found at \""+configpath+"\". Please create the file at this location and put the target IP in it."<<std::endl;
		return -3;
	}
	else
	{
		//Get IP address
		readstream.open(configpath);
		getline(readstream,ip);
		readstream.close();

		//Create a hint structure for the server we're connecting with
	        sock=socket(AF_INET,SOCK_STREAM,0);
	        int port=55000;
	        std::string ipAddress=ip;
	        sockaddr_in hint;
	        hint.sin_family=AF_INET;
	        hint.sin_port=htons(port);
	        inet_pton(AF_INET,ipAddress.c_str(),&hint.sin_addr);

	        if(sock==-1)
	        {
	                std::cout<<"Unable to create socket!"<<std::endl;
	        }

	        //Connect to the server on the socket
	        connectRes=connect(sock,(sockaddr*)&hint,sizeof(hint));

	        if(connectRes==-1)
	        {
	                std::cout<<"Could not connect to server!"<<std::endl;
	                return -1;
	        }

		sendData(("\""+std::string(argv[1])+"\"").c_str(),sock);
	}

	return 0;
}
