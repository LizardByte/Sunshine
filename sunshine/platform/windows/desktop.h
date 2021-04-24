namespace platf {
    using namespace std::literals;
    inline auto pairInputDesktop(){
        auto hDesk = OpenInputDesktop(DF_ALLOWOTHERACCOUNTHOOK, FALSE, GENERIC_ALL);
        if (NULL == hDesk) {
            auto err = GetLastError();
            BOOST_LOG(error) << "Failed to OpenInputDesktop [0x"sv << util::hex(err).to_string_view() << ']';
        } else {
            BOOST_LOG(info) << std::endl << "Opened desktop [0x"sv << util::hex(hDesk).to_string_view() << ']';
            if (!SetThreadDesktop(hDesk) ) {
            auto err = GetLastError();
            BOOST_LOG(error) << "Failed to SetThreadDesktop [0x"sv << util::hex(err).to_string_view() << ']';
            }
            CloseDesktop(hDesk);
        }
        return hDesk;
    };
};