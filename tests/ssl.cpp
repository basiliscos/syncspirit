#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>

namespace bfs = std::filesystem;
namespace ssl = boost::asio::ssl;

int main(int argc, char** argv) {
    try {
#if 0
        auto cert_path = bfs::path(argv[1]);
        auto size = bfs::file_size(cert_path);
        auto buff = std::vector<char>(size);
        auto in = std::ifstream(cert_path.string(), std::ios_base::binary | std::ios_base::in);
        in.read(buff.data(), size);
#endif

        // The io_context is required for all I/O operations
        boost::asio::io_context io_context;

        // Create SSL context and set default options
        boost::asio::ssl::context ssl_context(boost::asio::ssl::context::sslv23);
        // ssl_context.set_default_verify_paths();
        // ssl_context.add_certificate_authority(boost::asio::buffer(buff));
        // SSL_CTX_load_verify_store(ssl_context.native_handle(), "org.openssl.winstore://");
        // SSL_CTX_load_verify_store(ssl_context.native_handle(), "/etc/ssl/cert.pem");
        SSL_CTX_load_verify_store(ssl_context.native_handle(), "/tmp/cacert.pem");

        // Create SSL stream
        boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket(io_context, ssl_context);

        // Resolve the hostname
        boost::asio::ip::tcp::resolver resolver(io_context);
        auto endpoints = resolver.resolve("relays.syncthing.net", "443");

        // Connect to the server
        boost::asio::connect(socket.next_layer(), endpoints);

        // Perform SSL handshake
        auto mode = ssl::verify_peer | ssl::verify_fail_if_no_peer_cert | ssl::verify_client_once;
        // auto mode = ssl::verify_peer;
        // auto mode = ssl::verify_none;
        socket.set_verify_mode(mode);
        socket.set_verify_depth(10);
        // socket.set_verify_mode(boost::asio::ssl::verify_none);
        // socket.set_verify_callback([](bool preverified, boost::asio::ssl::verify_context& ctx) {
        //     // Simple verification callback - in production, you'd want proper certificate validation
        //     // return preverified;
        //     printf("preverified: %d\n", preverified);
        //     return true;
        // });
        SSL_set_tlsext_host_name(socket.native_handle(), "relays.syncthing.net");

        socket.handshake(boost::asio::ssl::stream_base::client);

        // Prepare HTTP GET request
        std::string request = 
            "GET /endpoint HTTP/1.1\r\n"
            "Host: relays.syncthing.net\r\n"
            "Connection: close\r\n\r\n";

        // Send the request
        boost::asio::write(socket, boost::asio::buffer(request));

        // Read the response
        boost::asio::streambuf response;
        boost::asio::read_until(socket, response, "\r\n");

        // Print the status line
        std::istream response_stream(&response);
        std::string status_line;
        std::getline(response_stream, status_line);
        std::cout << "Status: " << status_line << std::endl;

        // Read and print the rest of the response headers
        boost::asio::read_until(socket, response, "\r\n\r\n");
        std::string header;
        while (std::getline(response_stream, header) && header != "\r") {
            std::cout << header << std::endl;
        }

        // Read and print the response body
#if 0
        std::string body;
        boost::system::error_code error;
        while (boost::asio::read(socket, response, boost::asio::transfer_at_least(1), error)) {
            std::cout << &response;
        }
        if (error != boost::asio::error::eof) {
            throw boost::system::system_error(error);
        }
#endif

        // SSL shutdown (not strictly necessary but good practice)
        socket.shutdown();

    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

