#include "server_http.hpp"
#include "client_http.hpp"

#include "logconfig.h"
#include "logger.h"

//Added for the json-example
#define BOOST_SPIRIT_THREADSAFE
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

//Added for the default_resource example
#include <fstream>
#include <boost/filesystem.hpp>
#include <vector>
#include <algorithm>
#include <csignal>
#ifdef HAVE_OPENSSL
#include "crypto.hpp"
#endif

using namespace std;
//Added for the json-example:
using namespace boost::property_tree;

typedef SimpleWeb::Server<SimpleWeb::HTTP> HttpServer;
typedef SimpleWeb::Client<SimpleWeb::HTTP> HttpClient;

//Added for the default_resource example
void default_resource_send(const HttpServer &server, const shared_ptr<HttpServer::Response> &response,
                           const shared_ptr<ifstream> &ifs);
void sig_handler(int signum){
  LOG(INFO) << "http server gracefully shutting down.";
  exit(0);
}
int main() {
  if(std::signal(SIGINT, sig_handler) == SIG_ERR){
    LOG(CRITICAL) << "Can't register a signal handler for SIGINT";
    return -1;
  }

    HttpServer server;
    server.config.port=8080;
    LOG(INFO) << "http server booting.";

    // on POST request event
    server.resource["^/string$"]["POST"]=[](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
        //Retrieve string:
        auto content=request->content.string();
        auto address=request->remote_endpoint_address;
        auto port=request->remote_endpoint_port;
        LOG(INFO) << "POST Received - " << address << ":" << port;
        *response << "HTTP/1.1 200 OK\r\nContent-Length: " << content.length() << "\r\n\r\n" << content;
        LOG(DEBUG) << "POST Responded - "<< address << ":" << port;
    };

    //POST-example for the path /json, responds firstName+" "+lastName from the posted json
    //Responds with an appropriate error message if the posted json is not valid, or if firstName or lastName is missing
    server.resource["^/json$"]["POST"]=[](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
        auto address=request->remote_endpoint_address;
        auto port=request->remote_endpoint_port;
        try {
            ptree pt;
            read_json(request->content, pt);
            LOG(INFO) << "POSTJSON Received - " << address << ":" << port;
            string name=pt.get<string>("firstName")+" "+pt.get<string>("lastName");

            *response << "HTTP/1.1 200 OK\r\n"
                      << "Content-Type: application/json\r\n"
                      << "Content-Length: " << name.length() << "\r\n\r\n"
                      << name;
            LOG(DEBUG) << "POSTJSON Responded - "<< address << ":" << port;
        }
        catch(exception& e) {
            *response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n" << e.what();
            LOG(WARNING) << "POSTJSON Bad Request - "<< address << ":" << port;
        }
    };

    //GET-example for the path /info
    //Responds with request-information
    server.resource["^/info$"]["GET"]=[](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
        auto address=request->remote_endpoint_address;
        auto port=request->remote_endpoint_port;
        LOG(INFO) << "GETINFO Received - " << address << ":" << port;

        stringstream content_stream;
        content_stream << "<h1>Request from " << request->remote_endpoint_address << " (" << request->remote_endpoint_port << ")</h1>";
        content_stream << request->method << " " << request->path << " HTTP/" << request->http_version << "<br>";
        for(auto& header: request->header) {
            content_stream << header.first << ":" << header.second << "<br>";
        }
        //find length of content_stream (length received using content_stream.tellp())
        content_stream.seekp(0, ios::end);
        *response <<  "HTTP/1.1 200 OK\r\nContent-Length: " << content_stream.tellp() << "\r\n\r\n" << content_stream.rdbuf();
        LOG(DEBUG) << "GETINFO Responded - "<< address << ":" << port;
    };

    //GET-example for the path /match/[number], responds with the matched string in path (number)
    //For instance a request GET /match/123 will receive: 123
    server.resource["^/match/([0-9]+)$"]["GET"]=[&server](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
      // CLAYER: not performing tests
        string number=request->path_match[1];
        *response << "HTTP/1.1 200 OK\r\nContent-Length: " << number.length() << "\r\n\r\n" << number;
    };
    
    //Get example simulating heavy work in a separate thread
    server.resource["^/work$"]["GET"]=[&server](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
      auto address=request->remote_endpoint_address;
      auto port=request->remote_endpoint_port;
      thread work_thread([response, address,port]() {
            LOG(INFO) << "GETQUERY Received - "<< address << ":"<<port;
            this_thread::sleep_for(chrono::milliseconds(500));
            string message="Work done";
            *response << "HTTP/1.1 200 OK\r\nContent-Length: " << message.length() << "\r\n\r\n" << message;
            LOG(DEBUG) << "GETQUERY Responded - "<< address << ":"<<port;
        });
        work_thread.detach();
    };
    
    //Default GET-example. If no other matches, this anonymous function will be called. 
    //Will respond with content in the web/-directory, and its subdirectories.
    //Default file: index.html
    //Can for instance be used to retrieve an HTML 5 client that uses REST-resources on this server
    server.default_resource["GET"]=[&server](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
      auto address=request->remote_endpoint_address;
      auto port=request->remote_endpoint_port;
      LOG(INFO) << "GET Received - "<< address << ":"<<port;
        try {
            auto web_root_path=boost::filesystem::canonical("web");
            auto path=boost::filesystem::canonical(web_root_path/request->path);
            //Check if path is within web_root_path
            if(distance(web_root_path.begin(), web_root_path.end())>distance(path.begin(), path.end()) ||
               !equal(web_root_path.begin(), web_root_path.end(), path.begin()))
                throw invalid_argument("path must be within root path");
            if(boost::filesystem::is_directory(path))
                path/="index.html";
            if(!(boost::filesystem::exists(path) && boost::filesystem::is_regular_file(path)))
                throw invalid_argument("file does not exist");

            std::string cache_control, etag;

            cache_control="Cache-Control: max-age=86400\r\n";

            auto ifs=make_shared<ifstream>();
            ifs->open(path.string(), ifstream::in | ios::binary | ios::ate);

            if(*ifs) {
                auto length=ifs->tellg();
                ifs->seekg(0, ios::beg);

                *response << "HTTP/1.1 200 OK\r\n" << cache_control << etag << "Content-Length: " << length << "\r\n\r\n";
                default_resource_send(server, response, ifs);
                LOG(DEBUG) << "GET Responded - " <<address << ":" <<port;
            }
            else
                throw invalid_argument("could not read file");
        }
        catch(const exception &e) {
            string content="Could not open path "+request->path+": "+e.what();
            LOG(CRITICAL) << "GET Illegal Access - " << address << ":" << port;
            *response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << content.length() << "\r\n\r\n" << content;
        }
    };

    thread server_thread([&server](){
        //Start server
        server.start();
    });

    //Wait for server to start so that the client can connect
    this_thread::sleep_for(chrono::seconds(1));
    server_thread.join();

    return 0;
}

void default_resource_send(const HttpServer &server, const shared_ptr<HttpServer::Response> &response,
                           const shared_ptr<ifstream> &ifs) {
    //read and send 128 KB at a time
    static vector<char> buffer(131072); // Safe when server is running on one thread
    streamsize read_length;
    if((read_length=ifs->read(&buffer[0], buffer.size()).gcount())>0) {
        response->write(&buffer[0], read_length);
        if(read_length==static_cast<streamsize>(buffer.size())) {
            server.send(response, [&server, response, ifs](const boost::system::error_code &ec) {
                if(!ec)
                    default_resource_send(server, response, ifs);
                else
                    cerr << "Connection interrupted" << endl;
            });
        }
    }
}
