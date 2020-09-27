//g++ winrun.cpp -o winrun -lstdc++fs -std=c++17

#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <cstdlib>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <fstream>
#include <time.h>
#include <filesystem>

std::string getOutputFile(std::string filepath, const char *filename)
{
	std::string entry8;
	for (const auto & entry : std::filesystem::directory_iterator(filepath))
	{
		entry8=entry.path().u8string();
		if(entry8.find(filename)!=std::string::npos)
		{
			return entry8;
		}
	}
	return "";
}
int main(int argc, char** argv)
{
	int ms=1000;
	int timeout=5;
	int lineNum,dashIndex;
	pid_t pid=getpid();
	std::string path="/dev/shm/winrund/";
	std::string outputpath=(path+"_"+std::to_string(pid)).c_str();
	std::string line;
	std::string lastLine;
	std::string command;
	std::ifstream readstream;
	std::ofstream writestream;

	//If user just types in "winrun"
	if(!argv[1])
	{
		fprintf(stderr,"No command entered\n");
		return -1;
	}
	else
	{
		for(int i=1; i<argc; i++)
		{
			try
			{
				timeout=std::stoull(argv[i]);
			}
			catch(...)
			{
				command=argv[i];
			}
		}

		//Attempt to open "out" file and wait 1ms if it cannot be opened
		while(!writestream.is_open())
		{
			while(getOutputFile(path,"out.lock")!="")
			{
				usleep(ms);
			}
			writestream.open((path+"out.lock").c_str());
			writestream.close();
			writestream.open((path+"out").c_str());
		}

		writestream<<pid<<std::endl;
		writestream<<command<<std::endl;
		writestream<<timeout<<std::endl;
		writestream.close();
		remove((path+"out.lock").c_str());

		//Attempt to open response file and dump contents to stdout
		while(!readstream.is_open())
		{
			while(getOutputFile(path,std::to_string(pid).c_str())=="")
			{
				usleep(ms);
			}
			while(getOutputFile(path,(std::to_string(pid)+".lock").c_str())!="")
			{
				usleep(ms);
			}

			readstream.open(outputpath);
			std::getline(readstream,line);
			if(line!=lastLine&&line!=(std::to_string(pid)+std::to_string(pid)+std::to_string(pid)+std::to_string(pid)+std::to_string(pid)))
			{
				dashIndex=line.find("-");
				try
				{
					//Test if it is a valid line number before outputting
					stoull(line.substr(0,dashIndex));

					//std::cout<<"(Line "+line.substr(0,dashIndex)+"): "+line.substr((dashIndex+1),(line.length()-(line.substr(0,dashIndex+1).length())))<<std::endl;
					fprintf(stdout,"%s\n",line.substr((dashIndex+1),(line.length()-(line.substr(0,dashIndex+1).length()))).c_str());

					readstream.close();
					remove(outputpath.c_str());

					lineNum=stoi(line.substr(0,line.find("-")));

					lastLine=line;
				}
				catch(...)
				{
					readstream.close();
					remove(outputpath.c_str());
				}
			}
			else if(line==lastLine)
			{
				readstream.close();
				remove(outputpath.c_str());
			}
			else
			{
				remove(outputpath.c_str());
				return 0;
			}
		}
	
	}
	return 0;
}
