#include "gtest/gtest.h"
#include "models/ClientInfo.hpp"
#include "models/Metric.hpp"
#include "services/AuthService.hpp"
#include "repository/InMemoryMetricsRepository.hpp"
#include "services/MetricsService.hpp"

#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <atomic>

/**
 * Тест Metric, проверка сериализации и парсинга.
 * Проверка, что данные не искажаются при превращении в строку и обратно.
 */
TEST(MetricTest, SerializeDeserialize) {
    rmm::models::Metric original;
    original.fields = {{"cpu", "45"}, {"ram", "1024"}};

    std::string serialized = original.serialize();
    rmm::models::Metric parsed = rmm::models::Metric::parse(serialized);

    EXPECT_EQ(parsed.fields["cpu"], "45");
    EXPECT_EQ(parsed.fields["ram"], "1024");
    EXPECT_EQ(parsed.fields.size(), 2);
}

/**
 * Тест AuthService, проверка логики входа. Проверяем как успешные сценарии (admin/user), так и отказ в доступе.
 */
TEST(AuthServiceTest, LoginValidation) {
    rmm::services::AuthService auth;

    // Успешный вход
    auto success = auth.login("admin_user", "admin");
    ASSERT_TRUE(success.has_value());
    EXPECT_EQ(success->role, "admin");

    // Ошибка: пустой логин
    auto failEmpty = auth.login("", "admin");
    EXPECT_FALSE(failEmpty.has_value());

    // Ошибка: несуществующая роль
    auto failRole = auth.login("user", "super_manager");
    EXPECT_FALSE(failRole.has_value());
}

/**
 * Тест лимита хранилища, проверка ограничения в 100 метрик.
 */
TEST(RepositoryTest, MetricsLimitTest) {
    rmm::repository::InMemoryMetricsRepository repo;
    std::string user = "tester";

    // Отправляем 110 метрик (лимит у нас 100)
    for (int i = 1; i <= 110; ++i) {
        rmm::models::Metric m;
        m.fields["id"] = std::to_string(i);
        repo.addMetric(user, m);
    }

    auto result = repo.getMetrics(user);

    // Проверяем, что осталось ровно 100
    ASSERT_EQ(result.size(), 100);

    // Проверяем, что удалились старые (1-10), и первая в списке теперь 11-я
    EXPECT_EQ(result.front().fields["id"], "11");
    // Последняя — 110-я
    EXPECT_EQ(result.back().fields["id"], "110");
}

/**
 * Тест изоляции пользователей, проверка, что данные разных юзеров не перемешиваются.
 */
TEST(MetricsServiceTest, UserIsolation) {
    auto repo = std::make_shared<rmm::repository::InMemoryMetricsRepository>();
    rmm::services::MetricsService service(repo);

    rmm::models::Metric m1; m1.fields["val"] = "data1";
    rmm::models::Metric m2; m2.fields["val"] = "data2";

    service.acceptMetric("user1", m1);
    service.acceptMetric("user2", m2);

    std::string s1 = service.getSerializedMetrics("user1");
    std::string s2 = service.getSerializedMetrics("user2");

    EXPECT_TRUE(s1.find("data1") != std::string::npos);
    EXPECT_FALSE(s1.find("data2") != std::string::npos); // Данных второго юзера быть не должно
}

/**
 * Тест корректности формата вывода, проверка разделителя '|' между метриками.
 */
TEST(MetricsServiceTest, SerializationFormat) {
    auto repo = std::make_shared<rmm::repository::InMemoryMetricsRepository>();
    rmm::services::MetricsService service(repo);

    rmm::models::Metric m1; m1.fields["v"] = "1";
    rmm::models::Metric m2; m2.fields["v"] = "2";

    service.acceptMetric("user", m1);
    service.acceptMetric("user", m2);

    std::string result = service.getSerializedMetrics("user");

    // Формат должен быть: v=1|v=2
    EXPECT_EQ(result, "v=1|v=2");
}

/**
 * Нагрузочный тест сервера, поверка потокобезопасности репозитория.
 * 10 потоков одновременно пишут и читают данные одного пользователя.
 */
TEST(RepositoryTest, MultithreadedStressTest) {
    rmm::repository::InMemoryMetricsRepository repo;
    const std::string username = "stress_user";
    const int threads_count = 10;
    const int operations_per_thread = 10000;

    std::vector<std::thread> workers;
    std::atomic<bool> start_flag{false};

    for (int i = 0; i < threads_count; ++i) {
        workers.emplace_back([&, i]() {
            // Флаг ожидания начала,
            // это гарантирует, что все 10 потоков начнут долбить репозиторий максимально одновременно
            while (!start_flag) { std::this_thread::yield(); }

            for (int j = 0; j < operations_per_thread; ++j) {
                // Чередуем запись и чтение
                if (j % 2 == 0) {
                    rmm::models::Metric m;
                    m.fields["t"] = std::to_string(i);
                    m.fields["v"] = std::to_string(j);
                    repo.addMetric(username, m);
                } else {
                    auto data = repo.getMetrics(username);
                    if (!data.empty()) { (void)data.back().fields.size(); }
                }
            }
        });
    }

    start_flag = true;

    // Ждем завершения всех потоков
    for (auto& t : workers) {
        t.join();
    }

    // В хранилище должно остаться ровно 100 последних метрик
    auto final_metrics = repo.getMetrics(username);
    EXPECT_EQ(final_metrics.size(), 100);
}