#include <iostream>
using std::cerr;
using std::endl;

#include <string>
using std::string;

#include <vector>
using std::vector;

#include <sstream>
using std::stringstream;

#include <map>
using std::map;

// note: removes blank fields
vector<string> split(string str, string on = " \t\r\n") {
	vector<string> fields;
	// there are no separators
	if(str.find_first_of(on) == string::npos) {
		fields.push_back(str);
		return fields;
	}
	size_t fsep = 0, nsep = 0;
	while(!str.empty() && ((nsep = str.find_first_not_of(on)) != string::npos)) {
		// if there are leading on, strip them
		if(nsep != 0) {
			str = str.substr(nsep);
			continue;
		}

		// there is a field at the start
		fsep = str.find_first_of(on);
		if(fsep == string::npos) {
			fields.push_back(str);
			str = "";
		} else {
			fields.push_back(str.substr(0, fsep));
			str = str.substr(fsep);
		}
	}
	return fields;
}



#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>

struct Client {
	int fd{-1};
	bool hasName{false};
	string name{};
	string color{};
	int x{0}, y{0};

	Client(int ifd = -1) : fd(ifd) { }

	void send(string msg) {
		if(msg.back() != '\n')
			msg += "\r\n";
		write(fd, msg.c_str(), msg.length());
		// TODO: checking
	}

	void process(string line);
	void close();
};

int listenfd = 0;
map<int, Client> clients;
struct Message {
	string name{"NOONE"};
	string msg{};
	Message(string n, string m) : name(n), msg(m) { }
	string format() { return "TEXT " + name + " " + msg; }
};
vector<Message> chatLog;

// NICK NAMES TEXT
// SUCCESS COLLISION

void Client::process(string line) {
	vector<string> fs = split(line);
	if(fs.size() < 1)
		return;

	if(fs[0] == "TEXT") {
		string text = line.substr(5);
		string toSend = ":" + this->name + "!" + this->name + "@localhost "
			+ "PRIVMSG #pinkserv2 :" + text;
		bool found = false;
		for(auto &c : clients)
			if(c.second.name == "pinkserv2") {
				c.second.send(toSend);
				found = true;
			}
		if(!found)
			cerr << "couldn't find pinkserv2 to send message to" << endl;
	}
	if(fs[0] == "PRIVMSG") {
		for(auto &c : clients)
			if(c.second.name != "pinkserv2")
				c.second.send(line);
	}
	if(fs[0] == "NICK") {
		this->name = fs[1];
		cerr << "our name is " << this->name << endl;
		return;
	}
	if(fs[0] == "USER") {
		this->send(" 376 "); // pretend to send end of MOTD
		cerr << "sent 376 back" << endl;
		return;
	}
	if(fs[0] == "JOIN") {
		this->send(" 332 "); // pretend to send end of NICK list
		// we can ignore this too...
		return;
	}
	if(fs[0] == "PART") {
		// we can ignore this
		return;
	}
	if(fs[0] == "QUIT") {
		return;
	}
	if(fs[0] == "PONG") {
		return;
	}

	cerr << "recieved a \"" << fs[0] << "\" command:\n" << line << endl;
}

void broadcastCN(long n, fd_set &fds) {
	/*
	stringstream ss;
	ss << n << "\n";
	string s = ss.str();

	cerr << "sending \"" << s << "\" to clients" << endl;

	for(int fd = 0; fd < FD_SETSIZE; ++fd) {
		if(!FD_ISSET(fd, &fds))
			continue;
		if(fd == listenfd)
			continue;

		write(fd, s.c_str(), s.length());
	}*/
}
void broadcastMessage(string msg, fd_set &fds, int sfd) {
	for(int fd = 0; fd < FD_SETSIZE; ++fd) {
		if(!FD_ISSET(fd, &fds))
			continue;
		if(fd == listenfd)
			continue;
		if(fd == sfd)
			continue;

		write(fd, msg.c_str(), msg.length());
	}
}

#include <signal.h>
#include <errno.h>

bool running = true;

fd_set openFDs;
void sigintHandler(int dummy = 0) {
	cerr << "^c, going down" << endl;
	running = false;
}

int main(int argc, char **argv) {
	FD_ZERO(&openFDs);
	signal(SIGINT, sigintHandler);

	struct sockaddr_in serv_addr, client_addr;

	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if(listenfd < 0) {
		cerr << "can't open listenfd" << endl;
		return 1;
	}
	memset(&serv_addr, '\0', sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(6667);

	int errc = bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
	if(errc) {
		cerr << "cannot bind" << endl;
		return 1;
	}

	listen(listenfd, 10);

	long clientNumber = 0;
	stringstream ss;
	string s;

	map<int, int> fdPing;

	FD_SET(listenfd, &openFDs);

	cerr << "FD_SETSIZE: " << FD_SETSIZE << endl;

	while(running) {
		fd_set readFDs = openFDs;
		if(select(FD_SETSIZE, &readFDs, NULL, NULL, NULL) == -1) {
			// interrupted for some reason, just try again
			if(errno == EINTR)
				continue;
			// some sort of other or fatal error, give up
			perror("select");
			return 1;
		}

		for(int fd = 0; fd < FD_SETSIZE; ++fd) {
			// skip unset
			if(!FD_ISSET(fd, &readFDs))
				continue;

			// if new connection on on listen fd
			if(fd == listenfd) {
				int size = sizeof(client_addr);
				int nfd = accept(listenfd, (struct sockaddr*)&client_addr, (socklen_t*)&size);
				if(nfd < 0) {
					perror("accept");
					return 2;
				}
				cerr << "new connection from " << inet_ntoa(client_addr.sin_addr)
					<< ":" << ntohs(client_addr.sin_port) << endl;
				FD_SET(nfd, &openFDs);

				clients[nfd] = Client(nfd);

				clientNumber++;
				broadcastCN(clientNumber, openFDs);

			} else {
				// data from existing socket
				char buf[1024 + 1];
				memset(buf, '\0', 1024 + 1);

				int rcount = read(fd, buf, 1024);
				if(rcount < 0) {
					perror("read");
					//close(listenfd);
					//return 2;
					close(fd);
					FD_CLR(fd, &openFDs);

					clients.erase(clients.find(fd));

					broadcastCN(-1, openFDs);

				} else if(rcount == 0) {
					close(fd);
					FD_CLR(fd, &openFDs);

					clients.erase(clients.find(fd));

					broadcastCN(-1, openFDs);
				} else {
					// TODO: what if the newline hasn't come yet?
					vector<string> lines = split(buf, "\r\n");
					for(auto &line : lines) {
						cerr << "recieved \"" << line << "\" from client " << fd << endl;
						clients[fd].process(line);
						//broadcastMessage(string(buf), openFDs, fd);
					}
				}
			}
		}
	}

	cerr << "closing all open sockets" << endl;
	for(int fd = 0; fd < FD_SETSIZE; ++fd)
		if(FD_ISSET(fd, &openFDs))
			close(fd);

	return 0;
}

