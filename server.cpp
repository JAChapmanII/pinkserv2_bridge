#include <iostream>
using std::cerr;
using std::endl;

#include <string>
using std::string;

#include <vector>
using std::vector;

#include <typeinfo>

#include <tuple>
using std::tuple;
using std::tuple_size;
using std::tuple_element;
using std::get;

#include <sstream>
using std::stringstream;

#include <map>
using std::map;

#include <sqlite3.h>

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

string join(vector<string> strs, string with) {
	if(strs.empty())
		return "";
	string res;
	for(int i = 0; i < strs.size(); ++i)
		res += strs[i] + (i == strs.size() - 1 ? "" : with);
	return res;
}

template<typename T>
T fromString(string str) {
	stringstream ss;
	ss << str;
	T t;
	ss >> t;
	return t;
}

template<typename T>
string asString(T t) {
	stringstream ss;
	ss << t;
	return ss.str();
}

struct Table;

template<typename Tuple>
struct ResultSet;

struct Database {
	Database(string fname) : _fname(fname), _db(nullptr) { open(); }
	~Database() { close(); }

	bool tableExists(string tname) {
		string s = "select name from sqlite_master where type='table' and name='" + tname + "'";
	}

	template<typename Row_t, typename... Ts>
	ResultSet<Row_t> execute(string text, Ts... args);

	template<typename... Ts>
	void executeVoid(string text, Ts... args);

	sqlite3 *get_db() { return _db; }

	void create(Table table);
	void truncate(Table table);

	protected:
		void open() {
			int errc = sqlite3_open(_fname.c_str(), &_db);
			if(errc) {
				cerr << "Database::open: " << sqlite3_errmsg(_db) << endl;
				throw errc;
			}
		}
		void close() {
			if(_db) {
				sqlite3_close(_db);
			}
		}

		string _fname{};
		sqlite3 *_db{nullptr};
};


struct Statement {
	template<typename... Ts>
	Statement(Database &db, string text, Ts... args) {
		int errc = sqlite3_prepare_v2(db.get_db(), text.c_str(), -1,
				&_statement, NULL);
		if(errc) {
			cerr << "Statement::Statement: " << sqlite3_errmsg(db.get_db()) << endl;
			throw -1;
		}
		bindAll(args...);
	}

	~Statement() {
		sqlite3_finalize(_statement);
	}

	void bind(int idx, int value) {
		sqlite3_bind_int(_statement, idx, value);
	}
	void bind(int idx, double value) {
		sqlite3_bind_double(_statement, idx, value);
	}
	void bind(int idx, const char *value) {
		sqlite3_bind_text(_statement, idx, value, -1, SQLITE_TRANSIENT);
	}
	void bind(int idx, string value) {
		bind(idx, value.c_str());
	}

	template<int idx, typename T>
	void bindAll(T value) {
		bind(idx + 1, value);
	}
	
	template<int idx, typename T1, typename... Ts>
	void bindAll(T1 arg1, Ts... args) {
		bindAll<idx>(arg1);
		bindAll<idx + 1>(args...);
	}

	template<typename... Ts>
	void bindAll(Ts... args) {
		bindAll<0>(args...);
	}

	template<int>
	void bindAll() {
	}

	void execute() { 
		int code = sqlite3_step(_statement);
		while(code == SQLITE_ROW) {
			code = sqlite3_step(_statement);
		}
	}

	sqlite3_stmt *get_statement() { return _statement; }

	protected:
		sqlite3_stmt *_statement{nullptr};
};


template<typename Row_t, typename... Ts>
ResultSet<Row_t> Database::execute(string text, Ts... args) {
	Statement statement(*this, text, args...);
	ResultSet<Row_t> results;
	results.consume(statement);
	return results;
}

template<typename... Ts>
void Database::executeVoid(string text, Ts... args) {
	Statement statement(*this, text, args...);
	statement.execute();
}



namespace RSHelper {
	template<typename Tuple, int idx, typename ColumnType>
	struct setColumn_ {
		setColumn_(Tuple &r, sqlite3_stmt *s) {
			cerr << "this type is not implemented in RSHelper::setColumn_" << endl;
			throw -1;
		}
	};

	template<typename Tuple, int idx>
	struct setColumn_<Tuple, idx, int> {
		setColumn_(Tuple &r, sqlite3_stmt *s) {
			get<idx>(r) = sqlite3_column_int(s, idx);
		}
	};
	template<typename Tuple, int idx>
	struct setColumn_<Tuple, idx, string> {
		setColumn_(Tuple &r, sqlite3_stmt *s) {
			get<idx>(r) = string((const char *)sqlite3_column_text(s, idx));
		}
	};


	template<typename Tuple, int idx>
	void setColumn(Tuple &r, sqlite3_stmt *s) {
		setColumn_<Tuple, idx, typename tuple_element<idx, Tuple>::type> fs(r, s);
	}

	template<typename Tuple, int idx>
	struct RowBuilder {
		RowBuilder(Tuple &r, sqlite3_stmt *s) : sub(r, s) {
			setColumn<Tuple, idx>(r, s);
		}
		RowBuilder<Tuple, idx - 1> sub;
	};

	template<typename Tuple>
	struct RowBuilder<Tuple, 0> {
		RowBuilder(Tuple &r, sqlite3_stmt *s) {
			setColumn<Tuple, 0>(r, s);
		}
	};

	template<typename Tuple>
	void createRow(Tuple &r, sqlite3_stmt *s) {
		RowBuilder<Tuple, tuple_size<Tuple>::value - 1> cr(r, s);
	}
};

template<typename Tuple>
struct ResultSet {
	typedef Tuple Row;

	void consume(Statement &st) {
		sqlite3_stmt *_statement = st.get_statement();

		int code = sqlite3_step(_statement);
		while(code == SQLITE_ROW) {
			Row r;
			RSHelper::createRow<Tuple>(r, _statement);

			_rows.push_back(r);

			code = sqlite3_step(_statement);
		}
	}

	bool empty() { return _rows.empty() || !getColumnCount(); }
	int getRowCount() { return _rows.size(); }
	int getColumnCount() { return tuple_size<Tuple>::value; }

	Row operator[](int idx) { return _rows[idx]; }
	vector<Row> getRows() { return _rows; }

	typename vector<Row>::iterator begin() { return _rows.begin(); }
	typename vector<Row>::iterator end() { return _rows.end(); }

	protected:
		Tuple _tuple;
		vector<Row> _rows;
};

struct Column {
	string name{};
	string type{};
	bool primaryKey{false};
	bool nullable{true};

	Column(string in, string it, bool pk = false, bool n = true)
			: name(in), type(it), primaryKey(pk), nullable(n) { }

	string formatCreate() {
		return name + " "
			+ type + " "
			+ (primaryKey ? "primary key" : "") + " "
			+ (nullable ? "" : "not null");
	}
};

struct Table {
	string name{};
	vector<Column> columns{};

	Table(string in) : name(in) { }

	string formatCreate() {
		string cs = "create table if not exists " + name + "(\n";
		for(int i = 0; i < columns.size(); ++i)
			cs += columns[i].formatCreate()
				+ (i == columns.size() - 1 ? "" : ",") + "\n";
		cs += ");";
		return cs;
	}
	string formatTruncate() {
		return "delete from " + name;
	}
};

void Database::create(Table table) {
	executeVoid(table.formatCreate());
}
void Database::truncate(Table table) {
	executeVoid(table.formatTruncate());
}

typedef tuple<string, int, int> UserRow;

struct User {
	string name;
	int x, y;

	User(string in, int a = 0, int b = 0) : name(in), x(a), y(b) { }

	static bool exists(Database &db, string name) {
		return db
			.execute<UserRow>("select * from users where name=?", name)
			.getRowCount() > 0;
	}
	void save(Database &db) {
		db.executeVoid("insert into users values(?, ?, ?)", name, x, y);
	}
	void remove(Database &db) {
		User::remove(db, name);
	}
	static void remove(Database &db, string name) {
		db.executeVoid("delete from users where name=?", name);
	}
};

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

	Database db("sp_plus.db");

	Table users("users");
	users.columns.push_back({ "name", "varchar(32)", true, false });
	users.columns.push_back({ "posX", "int" });
	users.columns.push_back({ "posY", "int" });
	db.create(users);

	User("jac", 9, 10).save(db);
	cerr << "jac exists? " << User::exists(db, "jac") << endl;

	User("bob", 1, 2).save(db);

	auto results = db.execute<UserRow>("select * from users");
	cerr << "returned rows: " << results.getRowCount() << endl;
	for(auto row : results) {
		cerr << "{ "
				<< "name: " << get<0>(row) << ", "
				<< "pos: { " << get<1>(row) << ", " << get<2>(row) << " }"
			<< " }" << endl;
	}

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

