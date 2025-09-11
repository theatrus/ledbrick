#pragma once

#include <iostream>
#include <iomanip>
#include <string>
#include <cmath>

/**
 * Simple C++ Test Framework
 * Provides basic test assertions and result reporting
 */
class TestRunner {
private:
    int tests_run = 0;
    int tests_passed = 0;
    std::string current_suite;
    
public:
    void start_suite(const std::string& suite_name) {
        current_suite = suite_name;
        std::cout << "\n=== " << suite_name << " ===" << std::endl;
        tests_run = 0;
        tests_passed = 0;
    }
    
    void assert_equals(float expected, float actual, float tolerance, const std::string& test_name) {
        tests_run++;
        if (std::abs(expected - actual) <= tolerance) {
            tests_passed++;
            std::cout << "✓ " << test_name << " PASSED" << std::endl;
        } else {
            std::cout << "✗ " << test_name << " FAILED: expected " << expected 
                      << ", got " << actual << " (diff: " << std::abs(expected - actual) << ")" << std::endl;
        }
    }
    
    void assert_equals(double expected, double actual, double tolerance, const std::string& test_name) {
        tests_run++;
        if (std::abs(expected - actual) <= tolerance) {
            tests_passed++;
            std::cout << "✓ " << test_name << " PASSED" << std::endl;
        } else {
            std::cout << "✗ " << test_name << " FAILED: expected " << expected 
                      << ", got " << actual << " (diff: " << std::abs(expected - actual) << ")" << std::endl;
        }
    }
    
    void assert_equals(int expected, int actual, const std::string& test_name) {
        tests_run++;
        if (expected == actual) {
            tests_passed++;
            std::cout << "✓ " << test_name << " PASSED" << std::endl;
        } else {
            std::cout << "✗ " << test_name << " FAILED: expected " << expected 
                      << ", got " << actual << std::endl;
        }
    }
    
    void assert_equals(size_t expected, size_t actual, const std::string& test_name) {
        tests_run++;
        if (expected == actual) {
            tests_passed++;
            std::cout << "✓ " << test_name << " PASSED" << std::endl;
        } else {
            std::cout << "✗ " << test_name << " FAILED: expected " << expected 
                      << ", got " << actual << std::endl;
        }
    }
    
    void assert_equals(uint16_t expected, uint16_t actual, const std::string& test_name) {
        tests_run++;
        if (expected == actual) {
            tests_passed++;
            std::cout << "✓ " << test_name << " PASSED" << std::endl;
        } else {
            std::cout << "✗ " << test_name << " FAILED: expected " << expected 
                      << ", got " << actual << std::endl;
        }
    }
    
    void assert_true(bool condition, const std::string& test_name) {
        tests_run++;
        if (condition) {
            tests_passed++;
            std::cout << "✓ " << test_name << " PASSED" << std::endl;
        } else {
            std::cout << "✗ " << test_name << " FAILED" << std::endl;
        }
    }
    
    void assert_false(bool condition, const std::string& test_name) {
        assert_true(!condition, test_name);
    }
    
    void print_suite_summary() {
        std::cout << "\n=== TEST SUMMARY ===" << std::endl;
        std::cout << "Tests run: " << tests_run << std::endl;
        std::cout << "Tests passed: " << tests_passed << std::endl;
        std::cout << "Tests failed: " << (tests_run - tests_passed) << std::endl;
        std::cout << "Success rate: " << std::fixed << std::setprecision(1) 
                  << (100.0 * tests_passed / tests_run) << "%" << std::endl;
    }
    
    int get_tests_run() const { return tests_run; }
    int get_tests_passed() const { return tests_passed; }
    int get_tests_failed() const { return tests_run - tests_passed; }
    
    bool all_passed() const { return tests_run > 0 && tests_passed == tests_run; }
};

/**
 * Global test result aggregator
 */
class TestResults {
private:
    int total_tests_run = 0;
    int total_tests_passed = 0;
    int total_suites = 0;
    
public:
    void add_suite_results(const TestRunner& runner) {
        total_tests_run += runner.get_tests_run();
        total_tests_passed += runner.get_tests_passed();
        total_suites++;
    }
    
    void print_final_summary(const std::string& component_name) {
        std::cout << "\n=== " << component_name << " TEST RESULTS ===" << std::endl;
        std::cout << "Total suites: " << total_suites << std::endl;
        std::cout << "Total tests: " << total_tests_run << std::endl;
        std::cout << "Tests passed: " << total_tests_passed << std::endl;
        std::cout << "Tests failed: " << (total_tests_run - total_tests_passed) << std::endl;
        std::cout << "Success rate: " << std::fixed << std::setprecision(1) 
                  << (100.0 * total_tests_passed / total_tests_run) << "%" << std::endl;
        
        if (total_tests_passed == total_tests_run) {
            std::cout << "\n🎉 All tests passed!" << std::endl;
            std::cout << "✅ " << component_name << " is ready for integration." << std::endl;
        } else {
            std::cout << "\n❌ Some tests failed!" << std::endl;
        }
    }
    
    bool all_passed() const { 
        return total_tests_run > 0 && total_tests_passed == total_tests_run; 
    }
    
    int get_exit_code() const {
        return all_passed() ? 0 : 1;
    }
};