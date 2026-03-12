#include "net/Server.hpp"
#include "repository/InMemoryMetricsRepository.hpp"

#include <boost/asio.hpp>
#include <thread>
#include <iostream>
#include <vector>

int main() {
    try {
        boost::asio::io_context ioc;

        auto repo = std::make_shared<rmm::repository::InMemoryMetricsRepository>();

        rmm::net::Server server(ioc, 15001, repo);

        server.startAccept();

        unsigned int nthreads = std::max(2u, std::thread::hardware_concurrency());

        std::vector<std::thread> threads;
        for (unsigned int i = 0; i < nthreads; ++i) {
            threads.emplace_back([&ioc] { ioc.run(); });
        }

        std::cout << "Server running on port 15001 with " << nthreads << " threads";

        for (auto &t: threads) t.join();
    } catch (const std::exception &e) {
        std::cerr << "Fatal: " << e.what() << "";
        return 1;
    }

    return 0;
}
