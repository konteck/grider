#include <fstream>
#include <mongo/client/dbclient.h>
#include <Magick++.h>
#include "zmq.hpp"
#include "picojson.h"
#include "UriParser.hpp"
#include "md5.h"
#include "web++.hpp"

using namespace std;
using namespace mongo;
using namespace picojson;

object CONFIG;
std::deque<std::string> LOGS;

double timer() {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return (double) tv.tv_usec/1000000 + (double) tv.tv_sec;
}

double timer(double start) {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return ((double) tv.tv_usec/1000000 + (double) tv.tv_sec) - start;
}

double random(long min, long max) // range : [min, max]
{
    timeval time;
    gettimeofday(&time, NULL);

    srand((time.tv_sec * 1000) + (time.tv_usec / 100));

    return rand() % (max - min) + min;
}

string md5(char* data, int len) {
     md5_state_t state;
     md5_byte_t digest[16];

     md5_init(&state);
     md5_append(&state, (const md5_byte_t*)data, len);
     md5_finish(&state, digest);

     std::string callid_digest((const char*)digest, 16);

     char hex_output[33];

     for(int i = 0; i < 16; ++i) {
         sprintf(hex_output + i * 2, "%02x", digest[i]);
     }

     return (string)hex_output;
}

long SaveToGrid(DBClientConnection* con, string db, char* data, int size, string type) {
    long id;

    stringstream q;
    q << "{'md5': '" << md5(data, size) << "'}";

    if(!con->findOne(db + ".fs.files", Query(q.str())).isEmpty()) return 0;

    while(true) {
        id = random(1000000000, 90000000000);

        stringstream q;
        q << "{'metadata.id': " << id << "}";

        if(con->findOne(db + ".fs.files", Query(q.str())).isEmpty()) break;
    }

    stringstream name;
    name << id << "." << type;

    GridFS gridFS = GridFS(*con, db, "fs");

    gridFS.storeFile(data, size, name.str(), "image/" + type);

    return id;
}

void* doWork(void* ctx) {
    double start;
    std::string haystack = CONFIG["mongodb_uri"].to_str();
    http::url parsed = http::ParseHttpUrl(haystack);

    string db = parsed.path.substr(1);

    std::ostringstream conn;
    conn << parsed.host << ":" << parsed.port;

    DBClientConnection con;
    con.connect(conn.str());
    zmq::context_t* context = reinterpret_cast<zmq::context_t*>(ctx);
    zmq::socket_t socket(*context, ZMQ_REP);
    socket.connect("inproc://workers");

    zmq_pollitem_t items[1] = { { socket, 0, ZMQ_POLLIN, 0 } };

    while(true) {
        try {
            if(zmq_poll(items, 1, -1) < 1) {
                cout << "Terminating worker" << endl;
                break;
            }

            zmq::message_t request;
            socket.recv(&request);

            start = timer();

            Magick::Blob b((char*) request.data(), request.size());
            Magick::Image img(b);

            double width = img.columns(); double height = img.rows(); string type = img.magick();

            if(width == 0 || height == 0) {
                throw string("Image format incorrect");
            }

            std::transform(type.begin(), type.end(), type.begin(), ::tolower);
            long id = SaveToGrid(&con, db, (char*) request.data(), request.size(), type);

            if(!id) {
                throw string("Duplicate photo");
            }

            stringstream name;
            name << id << "." << type;

            // Response back
            object tmp;
            tmp.insert(std::make_pair ("status", * new value("success")));
            tmp.insert(std::make_pair ("id", * new value((double)id)));
            tmp.insert(std::make_pair ("filename", * new value(name.str())));
            tmp.insert(std::make_pair ("width", * new value(width)));
            tmp.insert(std::make_pair ("height", * new value(height)));
            value resp(tmp);

            string resp_text = resp.serialize();
            zmq::message_t reply(resp_text.size());
            memcpy (reply.data(), resp_text.c_str(), resp_text.size());
            socket.send(reply);

            // Log
            stringstream log;
            time_t now = time(0);
            tm *ltm = std::localtime(&now);

            log << "[" << ltm->tm_hour << ":" << ltm->tm_min << ":" << ltm->tm_sec << " " << ltm->tm_mday << "/" << ltm->tm_mon << "] \t"
            << name.str() << " " << request.size() / 1024 << "kb " << width << "x" << height
            << " \t" << timer(start) << "s";

            cout << log.str() << endl;

            LOGS.push_back(log.str());
        } catch(string e) {
            // Image format incorrect
            object tmp;
            tmp.insert(std::make_pair ("status", * new value("error")));
            tmp.insert(std::make_pair ("message", * new value(e)));
            value resp(tmp);

            string resp_text = resp.serialize();
            zmq::message_t reply(resp_text.size());
            memcpy (reply.data(), resp_text.c_str(), resp_text.size());
            socket.send(reply);

            // Log
            stringstream log;
            time_t now = time(0);
            tm *ltm = std::localtime(&now);

            log << "[" << ltm->tm_hour << ":" << ltm->tm_min << ":" << ltm->tm_sec << " " << ltm->tm_mday << "/" << ltm->tm_mon << "] \t"
            << e
            << " \t" << timer(start) << "s";

            cerr << log.str() << endl;

            LOGS.push_back(log.str());
        } catch(const zmq::error_t& e) {
            cerr << "ZeroMQ Worker: " << e.what() << endl;
        } catch(const mongo::DBException &e) {
            cerr << "MongoDB: " << e.what() << endl;
        } catch(exception& e) {
            cerr << "Fatal error: " << e.what() << endl;
        }
    }

    zmq_close(socket);
}

void* run(void* arg) {
    // Config
    ifstream infile;
    infile.open ("/etc/grider.json", ifstream::in);

    if(!infile.is_open()) {
        infile.open ("./grider.json", ifstream::in);

        if(!infile.is_open()) {
            std::cerr << "CONFIG: Unable to find 'grider.json' file!" << std::endl;

//            return;
        }
    }

    value v;
    infile >> v;

    std::string err = get_last_error();

    if (!err.empty()) {
      std::cerr << "grider.json: " << err << std::endl;

//      return;
    }

    CONFIG = v.get<object>();

    try {
        zmq::context_t context(1);
        zmq::socket_t clients(context, ZMQ_ROUTER);
        clients.bind(CONFIG["zeromq_uri"].to_str().c_str());

        zmq::socket_t workers(context, ZMQ_DEALER);
        workers.bind("inproc://workers");

        int WORKERS_COUNT;

        if(CONFIG["zeromq_workers"].to_str() == "auto") {
             WORKERS_COUNT = sysconf(_SC_NPROCESSORS_ONLN); // numCPU
        } else {
             WORKERS_COUNT = atoi(CONFIG["zeromq_workers"].to_str().c_str());
        }

        pthread_t worker;

        for(int i = 0; i < WORKERS_COUNT; ++i) {
             int rc = pthread_create (&worker, NULL, &doWork, &context);
             assert (rc == 0);
        }

        const int NR_ITEMS = 2;
        zmq_pollitem_t items[NR_ITEMS] =
        {
            { clients, 0, ZMQ_POLLIN, 0 },
            { workers, 0, ZMQ_POLLIN, 0 }
        };

        while(true)
        {
            zmq_poll(items, NR_ITEMS, -1);

            if(items[0].revents & ZMQ_POLLIN) {
                int more; size_t sockOptSize = sizeof(int);

                do {
                    zmq::message_t msg;
                    clients.recv(&msg);
                    clients.getsockopt(ZMQ_RCVMORE, &more, &sockOptSize);

                    workers.send(msg, more ? ZMQ_SNDMORE : ZMQ_DONTWAIT);
                } while(more);
            }

            if(items[1].revents & ZMQ_POLLIN) {
                int more; size_t sockOptSize = sizeof(int);

                do {
                    zmq::message_t msg;
                    workers.recv(&msg);
                    workers.getsockopt(ZMQ_RCVMORE, &more, &sockOptSize);

                    clients.send(msg, more ? ZMQ_SNDMORE : ZMQ_DONTWAIT);
                } while(more);
            }
        }

        zmq_close(clients);
        zmq_close(workers);
        zmq_term(context);
        pthread_join(worker, NULL);
    }
    catch(const zmq::error_t& e) {
       cerr << "ZeroMQ: " << e.what() << endl;
    }
}

void web_interface(WPP::Request* req, WPP::Response* res) {
    array tmp;
    for (int i = 0; i < LOGS.size(); i++) {
        tmp.push_back(* new value(LOGS[i]));
    }
    value resp(tmp);

    res->type = "application/json";
    res->body << resp.serialize().c_str();
}

int main(int argc, const char* argv[]) {
    Magick::InitializeMagick(*argv);

    pthread_t main;

    int rc = pthread_create (&main, NULL, &run, NULL);
    assert (rc == 0);

//    run();

    try {
        WPP::Server server;
        server.get("/", &web_interface);
        server.start(5000);
    } catch(WPP::Exception e) {
        cerr << "WebServer: " << e.what() << endl;
    }

    return EXIT_SUCCESS;
}
