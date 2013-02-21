#include <fstream>
#include <mongo/client/dbclient.h>
#include <Magick++.h>
#include "zmq.hpp"
#include "picojson.h"
#include "md5.h"
#include "web++.hpp"

using namespace std;
using namespace mongo;
using namespace picojson;

object CONFIG;
std::deque<std::string> LOGS;
string mongo_host;
string mongo_port;
string mongo_db;
string mongo_collection;

struct MessageBlock {
    zmq::socket_t* socket;
    unsigned long header_size;
    unsigned long body_size;
    void* header;
    void* body;
};

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

long random(long min, long max) // range : [min, max]
{
    timeval time;
    gettimeofday(&time, NULL);

    srand((time.tv_sec * 1000) + (time.tv_usec / 1000));
    return ((min) + rand()/(RAND_MAX + 1.0) * ((max) - (min) + 1));
    //return rand() % (max - min) + min;
}

string md5(char* data, unsigned long len) {
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

long SaveToGrid(char* data, unsigned long size, string type) {
    long id;
    DBClientConnection mongo_conn;
    mongo_conn.connect(mongo_host + ":" + mongo_port);

    //stringstream q;
    //q << "{'md5': '" << md5(data, size) << "'}";
    
    BSONObj query = BSON("md5" << md5(data, size) << "uploadDate" << BSONObjBuilder().appendDate("$gt", time(0) * 1000 - 900 * 1000).obj());
    
    if(!mongo_conn.findOne(mongo_db + ".fs.files", Query(query)).isEmpty()) return 0;

    while(true) {
        id = random(1000000000, 90000000000);

        stringstream q;
        q << "{'metadata.id': " << id << "}";

        if(mongo_conn.findOne(mongo_db + ".fs.files", Query(q.str())).isEmpty()) break;
    }

    stringstream name;
    name << id << "." << type;

    GridFS gridFS = GridFS(mongo_conn, mongo_db, "fs");

    gridFS.storeFile(data, size, name.str(), "image/" + type);

    return id;
}

void* ThreadWorker(void* args) {
    stringstream log;
    object response;
    double start = timer();
    MessageBlock* msg = (MessageBlock*)args;
        
    try {
        Magick::Blob b((char*)msg->body, msg->body_size);
        Magick::Image img(b);
        
        double width = img.columns(); double height = img.rows(); string type = img.magick();
        
        if(width == 0 || height == 0) {
            throw string("Image format incorrect");
        }
        
        std::transform(type.begin(), type.end(), type.begin(), ::tolower);
        long id = SaveToGrid((char*)msg->body, msg->body_size, type);
        
        if(!id) {
            throw string("Duplicate photo");
        }
        
        stringstream name;
        name << id << "." << type;
        
        // Response back
        response.insert(std::make_pair ("status", * new value("success")));
        response.insert(std::make_pair ("id", * new value((double)id)));
        response.insert(std::make_pair ("filename", * new value(name.str())));
        response.insert(std::make_pair ("width", * new value(width)));
        response.insert(std::make_pair ("height", * new value(height)));
        
        // Log
        time_t now = time(0);
        tm *ltm = std::localtime(&now);
        
        log << "[" << ltm->tm_hour << ":" << ltm->tm_min << ":" << ltm->tm_sec << " " << ltm->tm_mday << "/" << ltm->tm_mon << "] \t"
        << name.str() << " " << msg->body_size / 1024 << "kb " << width << "x" << height
        << " \t" << timer(start) << "s";
    } catch(string e) {
        response.insert(std::make_pair ("status", * new value("error")));
        response.insert(std::make_pair ("message", * new value(e)));
        
        // Log
        time_t now = time(0);
        tm *ltm = std::localtime(&now);
        
        log << "[" << ltm->tm_hour << ":" << ltm->tm_min << ":" << ltm->tm_sec << " " << ltm->tm_mday << "/" << ltm->tm_mon << "] \t"
        << e
        << " \t" << timer(start) << "s";
    } catch (Magick::Exception & e) {
        response.insert(std::make_pair ("status", * new value("error")));
        response.insert(std::make_pair ("message", * new value(e.what())));
        
        // Log
        time_t now = time(0);
        tm *ltm = std::localtime(&now);
        
        log << "[" << ltm->tm_hour << ":" << ltm->tm_min << ":" << ltm->tm_sec << " " << ltm->tm_mday << "/" << ltm->tm_mon << "] \t"
        << "Magick++: " << e.what()
        << " \t" << timer(start) << "s";
    } catch(const zmq::error_t& e) {
        response.insert(std::make_pair ("status", * new value("error")));
        response.insert(std::make_pair ("message", * new value(e.what())));
        
        // Log
        time_t now = time(0);
        tm *ltm = std::localtime(&now);
        
        log << "[" << ltm->tm_hour << ":" << ltm->tm_min << ":" << ltm->tm_sec << " " << ltm->tm_mday << "/" << ltm->tm_mon << "] \t"
        << "ZeroMQ Worker: " << e.what()
        << " \t" << timer(start) << "s";
    } catch(const mongo::DBException &e) {
        response.insert(std::make_pair ("status", * new value("error")));
        response.insert(std::make_pair ("message", * new value(e.what())));
        
        // Log
        time_t now = time(0);
        tm *ltm = std::localtime(&now);
        
        log << "[" << ltm->tm_hour << ":" << ltm->tm_min << ":" << ltm->tm_sec << " " << ltm->tm_mday << "/" << ltm->tm_mon << "] \t"
        << "MongoDB: " << e.what()
        << " \t" << timer(start) << "s";
    } catch(exception& e) {
        response.insert(std::make_pair ("status", * new value("error")));
        response.insert(std::make_pair ("message", * new value(e.what())));
        
        // Log
        time_t now = time(0);
        tm *ltm = std::localtime(&now);
        
        log << "[" << ltm->tm_hour << ":" << ltm->tm_min << ":" << ltm->tm_sec << " " << ltm->tm_mday << "/" << ltm->tm_mon << "] \t"
        << "Fatal error: " << e.what()
        << " \t" << timer(start) << "s";
    }
    
    cout << log.str() << endl;
    
    //LOGS.push_back(log.str());
    
    zmq::message_t msg_header(msg->header_size);
    memcpy(msg_header.data(), (char*)msg->header, msg->header_size);
    
    value resp(response);
    string resp_text = resp.serialize();
    zmq::message_t msg_body(resp_text.size());
    memcpy(msg_body.data(), resp_text.c_str(), resp_text.size());
    
    msg->socket->send(msg_header, ZMQ_SNDMORE);
    msg->socket->send(msg_body, 0);
    
    return NULL;
}

void* run(void* arg) {
    // Config
    ifstream infile;
    infile.open ("/etc/grider.json", ifstream::in);

    if(!infile.is_open()) {
        infile.open ("./grider.json", ifstream::in);

        if(!infile.is_open()) {
            std::cerr << "CONFIG: Unable to find 'grider.json' file!" << std::endl;
        }
    }

    value v;
    infile >> v;

    std::string err = get_last_error();

    if (!err.empty()) {
      std::cerr << "grider.json: " << err << std::endl;
    }

    CONFIG = v.get<object>();

    string uri = CONFIG["zeromq"].get<object>()["uri"].to_str();
    mongo_host = CONFIG["mongodb"].get<object>()["host"].to_str();
    mongo_port = CONFIG["mongodb"].get<object>()["port"].to_str();
    mongo_db = CONFIG["mongodb"].get<object>()["db"].to_str();
    mongo_collection = CONFIG["mongodb"].get<object>()["collection"].to_str();
    
    pthread_t workers;
    zmq::message_t request;
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_ROUTER);
    
    // Print 0MQ URI
    cout << uri << endl;
    
    socket.bind(uri.c_str());
    
    try {
        while(true) {
            MessageBlock msg;
            msg.socket = &socket;
            
            socket.recv(&request);
            msg.header_size = request.size();
            msg.header = operator new(request.size());
            memcpy(msg.header, (char*) request.data(), request.size());
            
            socket.recv(&request);
            msg.body_size = request.size();
            msg.body = operator new(request.size());
            memcpy(msg.body, (char*) request.data(), request.size());
            
            pthread_create(&workers, NULL, &ThreadWorker, new MessageBlock(msg));
            
            usleep(4 * 1000);
        }
    }
    catch(const zmq::error_t& e) {
        cerr << "ZeroMQ: " << e.what() << endl;
    }
    catch(exception& e) {
        cerr << "MainThread Exception: " << e.what() << endl;
    }
    catch (...) {
        cout << "MainThread Exception" << endl;
    }
    
    zmq_close(socket);
    zmq_term(context);
    pthread_join(workers, NULL);
    
    return NULL;
}

//void web_interface(WPP::Request* req, WPP::Response* res) {
//    array tmp;
//    for (int i = 0; i < LOGS.size(); i++) {
//        tmp.push_back(* new value(LOGS[i]));
//    }
//    value resp(tmp);
//
//    res->type = "application/json";
//    res->body << resp.serialize().c_str();
//}

int main(int argc, const char* argv[]) {
    Magick::InitializeMagick(*argv);

    run(NULL);

//    try {
//        WPP::Server server;
//        server.get("/", &web_interface);
//        server.start(5000);
//    } catch(WPP::Exception e) {
//        cerr << "WebServer: " << e.what() << endl;
//    }

    return EXIT_SUCCESS;
}
