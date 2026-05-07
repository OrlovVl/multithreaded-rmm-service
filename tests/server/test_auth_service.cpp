#include <gtest/gtest.h>
#include "rmm/server/business/auth_service.hpp"
#include "rmm/server/data/metric_repository.hpp"

TEST(AuthService, AdminCredentialsAreAccepted) {
    rmm::server::data::MetricRepository repo(":memory:");
    rmm::server::business::AuthService auth(repo);
    auto user = auth.authenticate("admin", "admin");
    ASSERT_TRUE(user.has_value());
    EXPECT_EQ(user->username, "admin");
    EXPECT_EQ(user->role, rmm::shared::Role::Admin);
}

TEST(AuthService, OtherCredentialsAreRejected) {
    rmm::server::data::MetricRepository repo(":memory:");
    rmm::server::business::AuthService auth(repo);
    EXPECT_FALSE(auth.authenticate("user", "user").has_value()); // нет такого пользователя
}

TEST(AuthService, RegularUserLogin) {
    rmm::server::data::MetricRepository repo(":memory:");
    rmm::server::business::AuthService auth(repo);
    auto user = auth.authenticate("user1", "user1");
    ASSERT_TRUE(user.has_value());
    EXPECT_EQ(user->username, "user1");
    EXPECT_EQ(user->role, rmm::shared::Role::User);
}