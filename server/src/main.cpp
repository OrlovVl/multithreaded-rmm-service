#include "rmm/server/business/auth_service.hpp"
#include "rmm/server/business/session_registry.hpp"
#include "rmm/server/data/metric_repository.hpp"
#include "rmm/server/network/server.hpp"

#include <boost/asio.hpp>
#include <iostream>

int main() {
    try {
        boost::asio::io_context ioContext;
        rmm::server::data::MetricRepository repository("rmm_server.sqlite3");
        rmm::server::business::AuthService authService(repository);
        rmm::server::business::SessionRegistry registry;
        rmm::server::network::Server server(ioContext, 5555, repository, authService, registry);
        std::cout << "RMM server listening on 0.0.0.0:5555\n";
        server.run();
    }
    catch (const std::exception& ex) {
        std::cerr << "Server error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}