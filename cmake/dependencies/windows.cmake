# windows specific dependencies

set(Boost_USE_STATIC_LIBS ON)  # cmake-lint: disable=C0103
# Boost >= 1.82.0 is required for boost::json::value::set_at_pointer() support
# todo - are we actually using json? I think this was attempted to be used in a PR, but we ended up not using json
find_package(Boost 1.82.0 COMPONENTS locale log filesystem program_options json REQUIRED)
