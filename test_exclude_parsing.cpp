#include <iostream>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <sstream>

// Test the logic for parsing exclude-global-event-actions
int main() {
    namespace pt = boost::property_tree;
    
    // Test case 1: Boolean true (exclude all)
    std::cout << "Test 1: Boolean true (exclude all global actions)\n";
    try {
        pt::ptree tree;
        tree.put("exclude-global-event-actions", true);
        auto excl_global_ea = tree.get_child_optional("exclude-global-event-actions");
        
        if (excl_global_ea) {
            try {
                bool exclude_all = excl_global_ea->get_value<bool>();
                std::cout << "  Parsed as boolean: " << (exclude_all ? "true" : "false") << std::endl;
                if (exclude_all) {
                    std::cout << "  Result: Would exclude all global stages\n";
                } else {
                    std::cout << "  Result: Would include all global stages\n";
                }
            } catch (const boost::property_tree::ptree_bad_data &) {
                std::cout << "  Failed to parse as boolean, would try array parsing\n";
            }
        }
    } catch (const std::exception &e) {
        std::cout << "  Error: " << e.what() << std::endl;
    }
    
    // Test case 2: Boolean false (include all)
    std::cout << "\nTest 2: Boolean false (include all global actions)\n";
    try {
        pt::ptree tree;
        tree.put("exclude-global-event-actions", false);
        auto excl_global_ea = tree.get_child_optional("exclude-global-event-actions");
        
        if (excl_global_ea) {
            try {
                bool exclude_all = excl_global_ea->get_value<bool>();
                std::cout << "  Parsed as boolean: " << (exclude_all ? "true" : "false") << std::endl;
                if (exclude_all) {
                    std::cout << "  Result: Would exclude all global stages\n";
                } else {
                    std::cout << "  Result: Would include all global stages\n";
                }
            } catch (const boost::property_tree::ptree_bad_data &) {
                std::cout << "  Failed to parse as boolean, would try array parsing\n";
            }
        }
    } catch (const std::exception &e) {
        std::cout << "  Error: " << e.what() << std::endl;
    }
    
    // Test case 3: Array of stage names (advanced usage)
    std::cout << "\nTest 3: Array of specific stages to exclude\n";
    try {
        std::string json_str = R"({"exclude-global-event-actions": ["PRE_STREAM_START", "POST_STREAM_STOP"]})";
        std::stringstream ss(json_str);
        pt::ptree tree;
        pt::read_json(ss, tree);
        auto excl_global_ea = tree.get_child_optional("exclude-global-event-actions");
        
        if (excl_global_ea) {
            try {
                bool exclude_all = excl_global_ea->get_value<bool>();
                std::cout << "  Parsed as boolean: " << (exclude_all ? "true" : "false") << std::endl;
            } catch (const boost::property_tree::ptree_bad_data &) {
                std::cout << "  Failed to parse as boolean, parsing as array:\n";
                for (auto &[_, s] : *excl_global_ea) {
                    std::cout << "    - " << s.get_value<std::string>() << std::endl;
                }
            }
        }
    } catch (const std::exception &e) {
        std::cout << "  Error: " << e.what() << std::endl;
    }
    
    return 0;
}
