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
	int lineNum,dashIndex;
	pid_t pid=getpid();
	std::string path="/dev/shm/winrund/";
	std::string outputpath=(path+"_"+std::to_string(pid)).c_str();
	std::string line;
	std::string lastLine;
	std::ifstream readstream;
	std::ofstream writestream;

	//If user just types in "winrun"
	if(!argv[1])
	{
		std::cout<<"No command entered!"<<std::endl;
		return -1;
	}
	else
	{
		//Attempt to open "out" file and wait 1ms if it cannot be opened
		while(!writestream.is_open())
		{
			try
			{
				writestream.open((path+"out").c_str());
			}
			catch(...)
			{
				usleep(1000);
			}
		}

		writestream<<pid<<std::endl;
		writestream<<argv[1]<<std::endl;
		writestream.close();

		//Attempt to open response file and dump contents to stdout
		while(!readstream.is_open())
		{
			while(getOutputFile(path,std::to_string(pid).c_str())=="")
			{
				usleep(100);
			}
			while(getOutputFile(path,(std::to_string(pid)+".lock").c_str())!="")
			{
				usleep(100);
			}
			try
			{
				readstream.open(outputpath);
				std::getline(readstream,line);
				if(line!=lastLine&&line!=(std::to_string(pid)+std::to_string(pid)+std::to_string(pid)+std::to_string(pid)+std::to_string(pid)))
				{
					dashIndex=line.find("-");
					try
					{
						//Test if it is a valid line number before outputting
						stoi(line.substr(0,dashIndex));

						//std::cout<<"(Line "+line.substr(0,dashIndex)+"): "+line.substr((dashIndex+1),(line.length()-(line.substr(0,dashIndex+1).length())))<<std::endl;
						std::cout<<line.substr((dashIndex+1),(line.length()-(line.substr(0,dashIndex+1).length())))<<std::endl;

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

					continue;
				}
				else if(line==lastLine)
				{
					readstream.close();
					remove(outputpath.c_str());

					continue;
				}
				else
				{
					remove(outputpath.c_str());
					return 0;
				}
			}
			catch(...)
			{
				std::cout<<"Failed to open \""<<outputpath<<"\", waiting one hundred microseconds!"<<std::endl;
				usleep(100);
			}
		}
	
	}
	return 0;
}
