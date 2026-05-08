/*#############################################################################
Copyright (C) 2014 Dr. Thomas Glomann

Author:		Dr. Thomas Glomann
                t.glomann@googlemail.com (subject [RAIO])

Version:        0.3 (initial release)

last update:    2014-05-06
*##############################################################################
Description:

This is a template for a simple LCD program.
From now on, we will assume that you have installed the RAIO8870 library as
described in the "INSTALL" instructions !

You can compile this template with the supplied Makefile.
Just adapt as neccessary and compile using the 'make' command.

Happy Coding :)

*##############################################################################
License:

This file is part of the C-Berry LCD C++ driver library.

The C-Berry LCD C++ driver library is free software: you can redistribute
it and/or modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, either version 3 of
the License, or (at your option) any later version.

The C-Berry LCD C++ driver library is distributed in the hope that it will
be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the C-Berry LCD C++ driver library.
If not, see <http://www.gnu.org/licenses/>.
*############################################################################*/

#include <iostream>   // cout, cerr, cin
#include <sstream>    // convert int to string
#include <string>     // c++ strings
#include <unistd.h>   // usleep
#include <fstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <string.h> /* for strncpy */
#include <arpa/inet.h>
#include <algorithm>
#include <cstring>
#include <boost/algorithm/string.hpp>
#include <vector>
#include "json/src/jansson.h"
#include "header/RAIO8870.h"
#include <curl/curl.h>
#include <time.h>

#define BUFFER_SIZE  (256 * 1024)  /* 256 KB */
#define URL_FORMAT "https://maps.googleapis.com/maps/api/directions/json?origin=Pfaffenhofen&destination=Munich&key=AIzaSyDiM8zJWlwlrtI_JAeTSLba1-EpvMOAUmU&mode=transit&departure_time=%d&mode=trans"

//#define URL_FORMAT "sers%d"
#define URL_SIZE     2048

// some little helper functions in seconds
inline void wait( unsigned long ms ) {usleep( ms * 1000 );}; // delay the code execution
CURL *curl;

struct write_result
{
    char *data;
    int pos;
};

 size_t write_response(void *ptr, size_t size, size_t nmemb, void *stream)
{
    struct write_result *result = (struct write_result *)stream;

    if(result->pos + size * nmemb >= BUFFER_SIZE - 1)
    {
        fprintf(stderr, "error: too small buffer\n");
        return 0;
    }

    memcpy(result->data + result->pos, ptr, size * nmemb);
    result->pos += size * nmemb;

    return size * nmemb;
}

 char * stringReplace(char *search, char *replace, char *string)
 {
 	char *tempString, *searchStart;
 	int len=0;

 	// preuefe ob Such-String vorhanden ist
 	searchStart = strstr(string, search);
 	if(searchStart == NULL) {
 		return string;
 	}

 	// Speicher reservieren
 	tempString = (char*) malloc(strlen(string) * sizeof(char));
 	if(tempString == NULL) {
 		return NULL;
 	}

 	// temporaere Kopie anlegen
 	strcpy(tempString, string);

 	// ersten Abschnitt in String setzen
 	len = searchStart - string;
 	string[len] = '\0';

 	// zweiten Abschnitt anhaengen
 	strcat(string, replace);

 	// dritten Abschnitt anhaengen
 	len += strlen(search);
 	strcat(string, (char*)tempString+len);

 	// Speicher freigeben
 	free(tempString);

 	return string;
 }

 char* stringReplaceAll(char *search, char *replace, std::string string)
 {

 	char *searchStart;
 	char * buffer = new char[string.length()];
 	strcpy(buffer, string.c_str());

 	do {
 		searchStart = strstr(buffer, search);
 		buffer = stringReplace(search, replace, buffer);
 	} while (searchStart != NULL);

 	return buffer;
 }

 //get ip
 struct ifreq  getIP()
 {

 	int fd;
 	struct ifreq ifr;

 	fd = socket(AF_INET, SOCK_DGRAM, 0);

 	/* I want to get an IPv4 IP address */
 	ifr.ifr_addr.sa_family = AF_INET;

 	/* I want IP address attached to "eth0" */
 	strncpy(ifr.ifr_name, "eth0", IFNAMSIZ - 1);

 	ioctl(fd, SIOCGIFADDR, &ifr);

 	close(fd);

 	return ifr;

 }

 //call a cmd
 std::string callCmd(char * cmd)
 {
   FILE *fp;
   int status;
   char path[1035];
   std::string output;

   /* Open the command for reading. */
   fp = popen(cmd, "r");
   if (fp == NULL) {
     printf("Failed to run command\n" );
     return 0;
   }

   /* Read the output a line at a time - output it. */
   while (fgets(path, sizeof(path)-1, fp) != NULL)
   {
     //printf("%s", path);
    output =output+ path;
   }

   /* close */
   pclose(fp);

   return output;
 }


 json_t * request(const char *url)
{

	json_t *root;
    CURLcode status;
    json_error_t error;
    long code;
    char *data;

    data = (char*)malloc(BUFFER_SIZE);//256*1024

    if(data==NULL||!curl)
    {
    	return NULL;

    }

    struct write_result write_result;
    write_result.data = data;
    write_result.pos =0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_result);

    status = curl_easy_perform(curl);
    if(status != 0)
    {
        fprintf(stderr, "error: unable to request data from %s:\n", url);
        fprintf(stderr, "%s\n", curl_easy_strerror(status));
        return NULL;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if(code != 200)
    {
        fprintf(stderr, "error: server responded with code %ld\n", code);
        return NULL;
    }

    /* zero-terminate the result */
    data[write_result.pos] = '\0';

    root = json_loads(data, 0, &error);
    free (data);

    return root;
}



void getArrivalAndDepature(std::string &out_arrival, std::string &out_depature, int offsetInSek=0)
{

	char url[URL_SIZE];

	json_t *root;
	json_t *current;
	json_t *arrival;
	json_t *depature;


//std::cout <<"current time stamp "<<time(0)+offsetInSek<<std::endl;
	snprintf(url, URL_SIZE, URL_FORMAT, time(0) + offsetInSek);
	root= request(url);

	if (root==NULL)
	{
		std::cout<<"error";
		return ;
	}

	current = json_object_get(root, "routes");
//std::cout<<"*** enter routes"<<std::endl;
	current = json_array_get(current, 0);

//json_object_foreach(root, key, value)
//{
//std::cout << key<<std::endl;
//}
	current = json_object_get(current, "legs");
//std::cout<<"*** enter legs"<<std::endl;
	current = json_array_get(current, 0);

//json_object_foreach(root, key, value)
//{
//std::cout << key<<std::endl;
//}

	arrival = json_object_get(current, "arrival_time");
	arrival = json_object_get(arrival, "text");

	depature = json_object_get(current, "departure_time");
	depature = json_object_get(depature, "text");

	std::cout << "depature: " << json_string_value(depature) << std::endl;
	std::cout << "arrival: " << json_string_value(arrival) << std::endl;

	out_depature.assign(const_cast<char *>(json_string_value(depature)), 8);
	out_arrival.assign(const_cast<char *>(json_string_value(arrival)), 8);

	json_decref(current);

	json_decref(arrival);
	json_decref(depature);
	json_decref(root);
}


void printOnScreen(RAIO8870 * raio)
{
	struct ifreq ifr;

//	std::string io = callCmd("iostat -m md0");
//	std::vector<std::string> strs;
//	boost::iter_split(strs, io, boost::first_finder("   "));
	std::string arrival;
	std::string depature;

	ifr = getIP();

	raio->setTextWindow(1, 1, 318, 50, BLUE);
	//raio->addText(callCmd("uname -sn"));
	raio->addText(inet_ntoa(((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr));
	raio->addText("\n");
	raio->addText(callCmd("date '+DATE: %d/%m/%y%nTIME: %H:%M:%S'"));
	raio->setTextWindow(1, 53, 318, 238);
	//raio->setTextWindow(153, 1, 318, 150,BLUE);
	raio->addText("\n");
	//raio->addText(callCmd("less /proc/mdstat"));
	raio->addText("Pfaffenhofen -> Muenchen\n");

	getArrivalAndDepature(arrival, depature);
	raio->addText(depature + "\n");
	raio->addText(" -> ");
	raio->addText(arrival + "\n");
	raio->addText("\n");
	raio->addText("  -- 15 min later --\n");

	getArrivalAndDepature(arrival, depature, 900);
	raio->addText(depature + "\n");
	raio->addText(" -> ");
	raio->addText(arrival + "\n");
	raio->addText("\n");
	raio->addText("  -- 15 min later --\n");

	getArrivalAndDepature(arrival, depature, 1800);
	raio->addText(depature + "\n");
	raio->addText(" -> ");
	raio->addText(arrival + "\n");

}


// THIS IS THE MAIN ROUTINE ---> START OF THE PROGRAM

int main(int argc, char *argv[])
{
//	RAIO8870 *raio = new RAIO8870(CM_65K); // use the 65k color mode
	RAIO8870 *raio = new RAIO8870(); // use the 4k color mode, default
	int count=0;

	 curl = curl_easy_init();


//  std::cout<<"size: " << strs.size() << std::endl;

	if (raio->getState() == Disconnected)
	{ // check if we can connect to the LCD module
		std::cerr << "\nError: no access to LCD module\n";
		std::cerr << "Did you start the program as root (sudo ./example) ?!\n"	<< std::endl;
		return -1; // exit program
	}

	raio->setBacklightValue(10);

	while(count<=10)
	{
		std::cout<< "new request "<<count<<std::endl;
		printOnScreen(raio);
		count++;
	#ifndef __DEBUG
			sleep(60*5);//sleep 6 min
	#endif
	}

	//raio->clearAll();
	raio->~RAIO8870();
	curl_easy_cleanup(curl);
	curl_global_cleanup();


	return 0;
}

