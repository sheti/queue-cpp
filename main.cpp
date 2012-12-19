#include <yaml-cpp/yaml.h>
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <cstring>
#include <errno.h>
#include <pthread.h>
#define BUF_SIZE 256

struct thread_arg {
  std::string task_name;
  std::string command;
  int num;
};

static void *start_com(void *vptr_args) {
  std::clog << "Start child: \"" << ((thread_arg *)vptr_args)->task_name << "\" number " << ((thread_arg *)vptr_args)->num << std::endl;
  std::string command = ((thread_arg *)vptr_args)->command;
  int num = ((thread_arg *)vptr_args)->num;
  delete (thread_arg *)vptr_args;
  int return_value = system(command.c_str());
  std::clog << "Child number " << num << " exited with value " << return_value << std::endl;
}

int main() {
  
  std::string sLog;
  std::string sPid;
  std::string sSocket;
  std::map<std::string,std::string> tasks;
  
  try {
    YAML::Node config = YAML::LoadFile("/etc/queue.conf.yaml");
    sLog = config["log"].as<std::string>();
	sPid = config["pid"].as<std::string>();
	sSocket = config["socket"].as<std::string>();
	YAML::Node tasks_node = config["tasks"];
	
	for(YAML::const_iterator it=tasks_node.begin();it!=tasks_node.end();++it) {
	  tasks.insert(std::pair<std::string,std::string>(it->first.as<std::string>(), it->second.as<std::string>()));
    }
  } catch(YAML::ParserException& e) {
    std::cerr << e.what() << std::endl;
  }
  
  std::ofstream outfilepid;
  try {
    outfilepid.open(sPid.c_str());
    outfilepid << getpid();
  } catch (std::ofstream::failure e) {
    std::cerr << "Exception opening pid file" << std::endl;
	return EXIT_FAILURE;
  }
  outfilepid.close();
  
  std::ofstream outlog;
  try {
    outlog.open(sLog.c_str());
  } catch (std::ofstream::failure e) {
    std::cerr << "Exception opening log file" << std::endl;
	return EXIT_FAILURE;
  }
  std::clog.rdbuf(outlog.rdbuf());

  int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
  struct sockaddr srvr_name;
  if (sock < 0) {
    std::clog << "Socket failed" << std::endl;
    return EXIT_FAILURE;
  }
  srvr_name.sa_family = AF_UNIX;
  strcpy(srvr_name.sa_data, sSocket.c_str());
  if (bind(sock, &srvr_name, strlen(srvr_name.sa_data) + sizeof(srvr_name.sa_family)) < 0) {
    std::clog << "Bind failed" << std::endl;
    return EXIT_FAILURE;
  }
  
  char buf[BUF_SIZE];
  std::string buf_task_name, buf_task_com;
  pthread_t thread;
  int tasck_i = 0;
  std::clog << "Queue start" << std::endl;
  while(1) {
    int bytes = recvfrom(sock, buf, sizeof(buf),  0, NULL, NULL);
	if(bytes < 0) 
	  break;
	buf[bytes] = 0;
	if(strcoll(buf, "exit\n") == 0)
	  break;
	for (std::map<std::string,std::string>::iterator it=tasks.begin() ; it != tasks.end(); it++ ) {
      buf_task_name = (*it).first + "\n";
      buf_task_com = (*it).second;
	  if(strcoll(buf, buf_task_name.c_str()) == 0) {
		struct thread_arg *ta = new thread_arg;
		ta->task_name = (*it).first;
		ta->command = buf_task_com;
		ta->num = ++tasck_i;
		pthread_create(&thread, NULL, start_com, ta);
		break;
	  }
    }
  }
  close(sock);
  unlink(sSocket.c_str());
  std::clog << "Queue stop" << std::endl;
  outlog.close();
  return EXIT_SUCCESS;
}
