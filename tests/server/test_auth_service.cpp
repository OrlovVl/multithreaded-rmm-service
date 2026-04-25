#include <gtest/gtest.h>

#include "rmm/server/business/auth_service.hpp"

TEST(AuthService, AdminCredentialsAreAccepted)
{
    rmm::server::business::AuthService auth;
    EXPECT_TRUE(auth.isAdmin("admin", "admin"));
}

TEST(AuthService, OtherCredentialsAreRejected)
{
    rmm::server::business::AuthService auth;
    EXPECT_FALSE(auth.isAdmin("user", "user"));
    EXPECT_FALSE(auth.isAdmin("admin", "123"));
}