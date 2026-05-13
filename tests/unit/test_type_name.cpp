#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <cortexflow/type_name.hpp>

// --- Test types: primitives handled directly; user types defined here ---

namespace app {

struct Sensor {};

namespace nested {
struct Reading {};
} // namespace nested

template <typename T>
struct Wrapper {};

struct Outer {
    struct Inner {};
};

} // namespace app

// A message using the CRTP base
struct PingMessage : cortexflow::Identified<PingMessage> {};

namespace app {
struct StatusUpdate : cortexflow::Identified<StatusUpdate> {};
} // namespace app

using cortexflow::type_name;
using cortexflow::type_id;
using cortexflow::Identified;

// ---------------------------------------------------------------------------
// Helper: report expected vs actual on mismatch
// ---------------------------------------------------------------------------
#define CHECK_TYPE_NAME(Type, expected)                                        \
    do {                                                                        \
        constexpr std::string_view actual = type_name<Type>();                  \
        INFO("type_name<" #Type ">()");                                         \
        INFO("  expected: \"" << (expected) << "\"");                            \
        INFO("  actual:   \"" << actual << "\"");                                \
        CHECK(actual == (expected));                                             \
    } while (false)

// ---------------------------------------------------------------------------
// Canonical-form fixture — per-compiler expected strings
// ---------------------------------------------------------------------------
// A compiler upgrade that changes __PRETTY_FUNCTION__ formatting will fail
// these checks, producing a clear diff of expected vs actual for every type.
// ---------------------------------------------------------------------------

TEST_CASE("type_name canonical form — primitives") {
    CHECK_TYPE_NAME(int, "int");
    CHECK_TYPE_NAME(float, "float");
    CHECK_TYPE_NAME(double, "double");
    CHECK_TYPE_NAME(bool, "bool");
    CHECK_TYPE_NAME(char, "char");
    CHECK_TYPE_NAME(void, "void");

#if defined(__clang__)
    CHECK_TYPE_NAME(unsigned int, "unsigned int");
    CHECK_TYPE_NAME(unsigned long, "unsigned long");
    CHECK_TYPE_NAME(long long, "long long");
#elif defined(__GNUC__)
    CHECK_TYPE_NAME(unsigned int, "unsigned int");
    CHECK_TYPE_NAME(unsigned long, "long unsigned int");
    CHECK_TYPE_NAME(long long, "long long int");
#endif
}

TEST_CASE("type_name canonical form — cv-qualifiers, pointers, references") {
#if defined(__clang__)
    CHECK_TYPE_NAME(const int, "const int");
    CHECK_TYPE_NAME(int *, "int *");
    CHECK_TYPE_NAME(const int *, "const int *");
    CHECK_TYPE_NAME(const int &, "const int &");
    CHECK_TYPE_NAME(int &&, "int &&");
#elif defined(__GNUC__)
    CHECK_TYPE_NAME(const int, "const int");
    CHECK_TYPE_NAME(int *, "int*");
    CHECK_TYPE_NAME(const int *, "const int*");
    CHECK_TYPE_NAME(const int &, "const int&");
    CHECK_TYPE_NAME(int &&, "int&&");
#endif
}

TEST_CASE("type_name canonical form — user types") {
    CHECK_TYPE_NAME(app::Sensor, "app::Sensor");
    CHECK_TYPE_NAME(app::nested::Reading, "app::nested::Reading");
    CHECK_TYPE_NAME(app::Outer::Inner, "app::Outer::Inner");
}

TEST_CASE("type_name canonical form — templates") {
    CHECK_TYPE_NAME(app::Wrapper<int>, "app::Wrapper<int>");
    CHECK_TYPE_NAME(app::Wrapper<app::Sensor>, "app::Wrapper<app::Sensor>");

#if defined(__clang__)
    CHECK_TYPE_NAME(app::Wrapper<const int *>,
                    "app::Wrapper<const int *>");
#elif defined(__GNUC__)
    CHECK_TYPE_NAME(app::Wrapper<const int *>,
                    "app::Wrapper<const int*>");
#endif
}

// ---------------------------------------------------------------------------
// type_id: constexpr hash derived from the name
// ---------------------------------------------------------------------------

TEST_CASE("type_id is constexpr") {
    constexpr auto id = type_id<int>();
    static_assert(id != 0, "type_id must be nonzero");
    CHECK(id != 0);
}

TEST_CASE("distinct types produce distinct type_ids") {
    constexpr auto id_int = type_id<int>();
    constexpr auto id_float = type_id<float>();
    constexpr auto id_sensor = type_id<app::Sensor>();
    constexpr auto id_reading = type_id<app::nested::Reading>();

    CHECK(id_int != id_float);
    CHECK(id_int != id_sensor);
    CHECK(id_sensor != id_reading);
}

TEST_CASE("same type always produces same type_id") {
    constexpr auto a = type_id<app::Sensor>();
    constexpr auto b = type_id<app::Sensor>();
    static_assert(a == b, "same type must hash identically");
    CHECK(a == b);
}

// ---------------------------------------------------------------------------
// CRTP Identified base
// ---------------------------------------------------------------------------

TEST_CASE("Identified CRTP base exposes kName and kTypeId") {
    static_assert(PingMessage::kName == type_name<PingMessage>());
    static_assert(PingMessage::kTypeId == type_id<PingMessage>());

    CHECK(PingMessage::kName == type_name<PingMessage>());
    CHECK(PingMessage::kTypeId == type_id<PingMessage>());
}

TEST_CASE("Identified kName matches expected canonical form") {
    CHECK_TYPE_NAME(PingMessage, "PingMessage");
    CHECK_TYPE_NAME(app::StatusUpdate, "app::StatusUpdate");
}

TEST_CASE("Identified types have distinct kTypeId values") {
    static_assert(PingMessage::kTypeId != app::StatusUpdate::kTypeId);
    CHECK(PingMessage::kTypeId != app::StatusUpdate::kTypeId);
}

// ---------------------------------------------------------------------------
// Compile-time verification
// ---------------------------------------------------------------------------

TEST_CASE("type_name is usable in static_assert") {
    static_assert(type_name<int>() == "int");
    static_assert(type_name<app::Sensor>() == "app::Sensor");
    static_assert(type_name<void>() == "void");
    CHECK(true);
}
